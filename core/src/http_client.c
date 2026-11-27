/**
 * @file http_client.c
 * @brief 基于明文 TCP 的精简 HTTP/1.1 客户端（GET / POST）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "board_core/http_client.h"
#include "board_core/tls_client.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define HTTP_MAX_RESPONSE (256u * 1024u)
/* 大模型的 JSON 响应（含上下文）可能超过 256 KB，POST 单独放宽。 */
#define HTTP_MAX_RESPONSE_POST (1024u * 1024u)
#define HTTP_MAX_REDIRECTS 3
#define HTTP_READ_CHUNK 8192

typedef struct {
    char host[256];
    char port[8];
    char path[2048];
    bool tls; /* https:// */
} url_t;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    size_t limit;   /* 最大可接收字节数 */
} buffer_t;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int buf_init(buffer_t *b, size_t limit)
{
    b->cap = HTTP_READ_CHUNK;
    b->len = 0;
    b->limit = limit;
    b->data = malloc(b->cap);
    if (b->data == NULL) {
        return -ENOMEM;
    }
    b->data[0] = '\0';
    return 0;
}

static int buf_reserve(buffer_t *b, size_t extra)
{
    size_t need = b->len + extra + 1;
    char *p;
    if (need <= b->cap) {
        return 0;
    }
    while (b->cap < need) {
        b->cap *= 2;
        if (b->cap > b->limit + HTTP_READ_CHUNK) {
            return -EFBIG;
        }
    }
    p = realloc(b->data, b->cap);
    if (p == NULL) {
        return -ENOMEM;
    }
    b->data = p;
    return 0;
}

static int parse_url(const char *url, url_t *out)
{
    const char *p;
    const char *host_start;
    const char *host_end;
    size_t host_len;

    if (url == NULL || out == NULL) {
        return -EINVAL;
    }
    if (strncmp(url, "https://", 8) == 0) {
        out->tls = true;
        p = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        out->tls = false;
        p = url + 7;
    } else {
        return -EINVAL;
    }
    host_start = p;
    if (*p == '[') {
        p++;
        host_start = p;
        while (*p != '\0' && *p != ']') {
            p++;
        }
        if (*p != ']') {
            return -EINVAL;
        }
        host_end = p;
        p++;
    } else {
        while (*p != '\0' && *p != ':' && *p != '/') {
            p++;
        }
        host_end = p;
    }
    host_len = (size_t)(host_end - host_start);
    if (host_len == 0 || host_len >= sizeof(out->host)) {
        return -EINVAL;
    }
    memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';

    snprintf(out->port, sizeof(out->port), out->tls ? "443" : "80");
    if (*p == ':') {
        const char *port_start = ++p;
        size_t port_len;
        while (*p >= '0' && *p <= '9') {
            p++;
        }
        port_len = (size_t)(p - port_start);
        if (port_len == 0 || port_len >= sizeof(out->port)) {
            return -EINVAL;
        }
        memcpy(out->port, port_start, port_len);
        out->port[port_len] = '\0';
    }
    if (*p == '\0') {
        snprintf(out->path, sizeof(out->path), "/");
    } else if (*p == '/') {
        snprintf(out->path, sizeof(out->path), "%s", p);
    } else {
        return -EINVAL;
    }
    {
        char *frag = strchr(out->path, '#');
        if (frag != NULL) {
            *frag = '\0';
        }
    }
    return 0;
}

static int wait_fd(int fd, short events, int64_t deadline, volatile int *cancel)
{
    while (1) {
        struct pollfd pfd;
        int64_t remain;
        int timeout;
        int n;

        if (cancel != NULL && *cancel) {
            return -ECANCELED;
        }
        remain = deadline - now_ms();
        if (remain <= 0) {
            return -ETIMEDOUT;
        }
        timeout = (int)(remain > 200 ? 200 : remain);
        pfd.fd = fd;
        pfd.events = events;
        pfd.revents = 0;
        n = poll(&pfd, 1, timeout);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        if (n > 0) {
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                return -ECONNRESET;
            }
            return 0;
        }
    }
}

static int connect_host(const url_t *url, int timeout_ms, volatile int *cancel)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *it;
    int64_t deadline = now_ms() + timeout_ms;
    int fd = -1;
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    ret = getaddrinfo(url->host, url->port, &hints, &res);
    if (ret != 0) {
        return -EHOSTUNREACH;
    }
    for (it = res; it != NULL; it = it->ai_next) {
        int flags;
        fd = socket(it->ai_family, it->ai_socktype | SOCK_CLOEXEC, it->ai_protocol);
        if (fd < 0) {
            continue;
        }
        flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(fd);
            fd = -1;
            continue;
        }
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        if (errno == EINPROGRESS) {
            int err = 0;
            socklen_t errlen = sizeof(err);
            ret = wait_fd(fd, POLLOUT, deadline, cancel);
            if (ret == 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == 0 && err == 0) {
                break;
            }
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    /* 明确区分“连不上”和 fd：原来返回 -1 会被上层当成 EPERM，
     * 日志里显示 "Operation not permitted"，非常误导。 */
    return fd >= 0 ? fd : -ECONNREFUSED;
}

static int send_all(int fd, tls_conn_t *tls, const char *data, size_t len,
                    int64_t deadline, volatile int *cancel)
{
    size_t sent = 0;

    if (tls != NULL) {
        return tls_write(tls, data, len);
    }
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            int ret = wait_fd(fd, POLLOUT, deadline, cancel);
            if (ret != 0) {
                return ret;
            }
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return -errno;
    }
    return 0;
}

/** 统一收包：>=0 字节数；0 EOF；-EAGAIN 可重试；其它负值错误。 */
static int conn_recv(int fd, tls_conn_t *tls, void *buf, size_t len)
{
    if (tls != NULL) {
        return tls_read(tls, buf, len);
    }
    {
        ssize_t n = recv(fd, buf, len, 0);
        if (n >= 0) {
            return (int)n;
        }
        return -(int)errno;
    }
}

static int header_end(const char *data, size_t len)
{
    if (len < 4) {
        return -1;
    }
    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            return (int)(i + 4);
        }
    }
    return -1;
}

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        *--end = '\0';
    }
    return s;
}

static int parse_headers(char *headers, int *status, long *content_len,
                         int *chunked, char *location, size_t loc_cap)
{
    char *save = NULL;
    char *line;
    int first = 1;

    *status = 0;
    *content_len = -1;
    *chunked = 0;
    if (loc_cap > 0) {
        location[0] = '\0';
    }
    for (line = strtok_r(headers, "\r\n", &save); line != NULL;
         line = strtok_r(NULL, "\r\n", &save)) {
        if (first) {
            if (sscanf(line, "HTTP/%*d.%*d %d", status) != 1) {
                return -EPROTO;
            }
            first = 0;
            continue;
        }
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            *content_len = strtol(trim(line + 15), NULL, 10);
        } else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0) {
            if (strcasestr(line + 18, "chunked") != NULL) {
                *chunked = 1;
            }
        } else if (strncasecmp(line, "Location:", 9) == 0) {
            snprintf(location, loc_cap, "%s", trim(line + 9));
        }
    }
    return (*status > 0) ? 0 : -EPROTO;
}

static int decode_chunked(const char *in, size_t in_len, char **out, size_t *out_len)
{
    char *dst;
    size_t pos = 0;
    size_t used = 0;

    dst = malloc(in_len + 1);
    if (dst == NULL) {
        return -ENOMEM;
    }
    while (pos < in_len) {
        char *endp = NULL;
        unsigned long chunk;
        const char *line_end = memchr(in + pos, '\n', in_len - pos);
        size_t line_len;
        if (line_end == NULL) {
            break;
        }
        line_len = (size_t)(line_end - (in + pos));
        if (line_len > 0 && in[pos + line_len - 1] == '\r') {
            line_len--;
        }
        chunk = strtoul(in + pos, &endp, 16);
        if (endp == in + pos) {
            free(dst);
            return -EPROTO;
        }
        pos += (size_t)(line_end - (in + pos)) + 1;
        if (chunk == 0) {
            break;
        }
        if (pos + chunk > in_len) {
            free(dst);
            return -EPROTO;
        }
        memcpy(dst + used, in + pos, chunk);
        used += chunk;
        pos += chunk;
        if (pos < in_len && in[pos] == '\r') {
            pos++;
        }
        if (pos < in_len && in[pos] == '\n') {
            pos++;
        }
    }
    dst[used] = '\0';
    *out = dst;
    *out_len = used;
    return 0;
}

static int append_part(char *out, size_t cap, size_t *pos, const char *part)
{
    size_t n = strlen(part);
    if (*pos + n >= cap) {
        return -ENAMETOOLONG;
    }
    memcpy(out + *pos, part, n);
    *pos += n;
    out[*pos] = '\0';
    return 0;
}

static int build_redirect(const char *base, const char *location, char *out, size_t cap)
{
    url_t u;
    size_t pos = 0;
    if (location == NULL || location[0] == '\0' || cap == 0) {
        return -EINVAL;
    }
    out[0] = '\0';
    if (strncmp(location, "http://", 7) == 0) {
        if (strlen(location) >= cap) {
            return -ENAMETOOLONG;
        }
        memcpy(out, location, strlen(location) + 1);
        return 0;
    }
    if (parse_url(base, &u) != 0) {
        return -EINVAL;
    }
    if (location[0] != '/') {
        char *slash = strrchr(u.path, '/');
        if (slash != NULL) {
            slash[1] = '\0';
        } else {
            snprintf(u.path, sizeof(u.path), "/");
        }
    } else {
        u.path[0] = '\0';
    }
    if (append_part(out, cap, &pos, "http://") != 0 ||
        append_part(out, cap, &pos, u.host) != 0 ||
        append_part(out, cap, &pos, ":") != 0 ||
        append_part(out, cap, &pos, u.port) != 0 ||
        append_part(out, cap, &pos, u.path) != 0 ||
        append_part(out, cap, &pos, location) != 0) {
        return -ENAMETOOLONG;
    }
    return 0;
}

/* 关闭连接：先关 TLS 再关 fd（http_once 内使用） */
#define CONN_CLOSE()                    \
    do {                                \
        if (tls != NULL) {              \
            tls_close(tls);             \
            tls = NULL;                 \
        }                               \
        close(fd);                      \
    } while (0)

static int http_once(const char *method, const char *url, const char *body,
                     size_t body_len, const char *content_type,
                     const char *extra_headers, int timeout_ms,
                     volatile int *cancel, http_response_t *resp, size_t limit,
                     char *redirect, size_t redirect_cap)
{
    url_t u;
    int fd;
    tls_conn_t *tls = NULL;
    int64_t deadline;
    int ret;
    char *req = NULL;
    size_t req_cap;
    size_t req_len;
    buffer_t buf;
    int hdr_end = -1;
    int status = 0;
    long content_len = -1;
    int chunked = 0;
    int header_parsed = 0;
    char location[2048];

    if (parse_url(url, &u) != 0) {
        return -EINVAL;
    }
    deadline = now_ms() + timeout_ms;
    fd = connect_host(&u, timeout_ms, cancel);
    if (fd < 0) {
        return fd;
    }
    if (u.tls) {
        tls = tls_connect(fd, u.host);
        if (tls == NULL) {
            CONN_CLOSE();
            return -EIO;
        }
    }

    /* 请求 = 起始行/头 + 可选请求体；一次性拼好后分块发送。 */
    req_cap = 512 + strlen(u.path) + strlen(u.host) +
              (body_len > 0 ? body_len + 256 : 0) +
              (extra_headers != NULL ? strlen(extra_headers) : 0);
    req_cap += 512;
    req = malloc(req_cap);
    if (req == NULL) {
        CONN_CLOSE();
        return -ENOMEM;
    }
    req_len = (size_t)snprintf(req, req_cap,
                               "%s %s HTTP/1.1\r\n"
                               "Host: %s\r\n"
                               "User-Agent: board-app/1.0\r\n"
                               "Accept: application/json\r\n",
                               method, u.path, u.host);
    if (body_len > 0) {
        req_len += (size_t)snprintf(req + req_len, req_cap - req_len,
                                    "Content-Type: %s\r\n"
                                    "Content-Length: %u\r\n",
                                    (content_type != NULL && content_type[0] != '\0')
                                        ? content_type
                                        : "application/json",
                                    (unsigned)body_len);
    }
    if (extra_headers != NULL && extra_headers[0] != '\0') {
        req_len += (size_t)snprintf(req + req_len, req_cap - req_len, "%s",
                                    extra_headers);
    }
    req_len += (size_t)snprintf(req + req_len, req_cap - req_len,
                                "Connection: close\r\n\r\n");
    if (body_len > 0) {
        if (req_len + body_len >= req_cap) {
            free(req);
            CONN_CLOSE();
            return -ENOBUFS;
        }
        memcpy(req + req_len, body, body_len);
        req_len += body_len;
    }

    ret = send_all(fd, tls, req, req_len, deadline, cancel);
    free(req);
    if (ret != 0) {
        CONN_CLOSE();
        return ret;
    }

    ret = buf_init(&buf, limit);
    if (ret != 0) {
        CONN_CLOSE();
        return ret;
    }
    /* 先读到响应头结束，再读到 Content-Length 指定的长度或 EOF。
     * 请求中使用了 Connection: close，因此对于采用分块编码的服务器，
     * EOF 同样是有效的结束标记。 */
    while (1) {
        ssize_t n;
        if (buf.len > limit) {
            ret = -EFBIG;
            break;
        }
        ret = buf_reserve(&buf, HTTP_READ_CHUNK);
        if (ret != 0) {
            break;
        }
        n = conn_recv(fd, tls, buf.data + buf.len, buf.cap - buf.len - 1);
        if (n > 0) {
            buf.len += (size_t)n;
            buf.data[buf.len] = '\0';
            if (!header_parsed) {
                hdr_end = header_end(buf.data, buf.len);
                if (hdr_end > 0) {
                    char *copy = strndup(buf.data, (size_t)hdr_end);
                    if (copy == NULL) {
                        ret = -ENOMEM;
                        break;
                    }
                    ret = parse_headers(copy, &status, &content_len, &chunked,
                                        location, sizeof(location));
                    free(copy);
                    if (ret != 0) {
                        break;
                    }
                    header_parsed = 1;
                    if ((status == 301 || status == 302 || status == 303 ||
                         status == 307 || status == 308) && location[0] != '\0') {
                        snprintf(redirect, redirect_cap, "%s", location);
                        ret = 1;
                        break;
                    }
                }
            } else if (content_len >= 0 &&
                       buf.len - (size_t)hdr_end >= (size_t)content_len) {
                break;
            }
            continue;
        }
        if (n == 0) {
            break;
        }
        if (n == -EAGAIN || n == -EWOULDBLOCK) {
            ret = wait_fd(fd, POLLIN, deadline, cancel);
            if (ret != 0) {
                break;
            }
            continue;
        }
        if (n == -EINTR) {
            continue;
        }
        ret = (int)n;
        break;
    }
    CONN_CLOSE();

    if (ret != 0) {
        free(buf.data);
        return ret;
    }
    if (!header_parsed || hdr_end <= 0) {
        free(buf.data);
        return -EPROTO;
    }
    if (content_len >= 0 && (size_t)content_len > limit) {
        free(buf.data);
        return -EFBIG;
    }

    {
        size_t body_len = buf.len - (size_t)hdr_end;
        char *body = NULL;
        size_t out_len = 0;

        if (chunked) {
            ret = decode_chunked(buf.data + hdr_end, body_len, &body, &out_len);
            if (ret != 0) {
                free(buf.data);
                return ret;
            }
        } else {
            if (content_len >= 0 && (size_t)content_len < body_len) {
                body_len = (size_t)content_len;
            }
            body = malloc(body_len + 1);
            if (body == NULL) {
                free(buf.data);
                return -ENOMEM;
            }
            memcpy(body, buf.data + hdr_end, body_len);
            body[body_len] = '\0';
            out_len = body_len;
        }
        free(buf.data);
        resp->body = body;
        resp->len = out_len;
        resp->status_code = status;
    }
    return 0;
}

/* 处理重定向循环。body 为 NULL 时是纯 GET。 */
static int http_request(const char *method, const char *url, const char *body,
                        size_t body_len, const char *content_type,
                        const char *extra_headers, int timeout_ms,
                        volatile int *cancel, http_response_t *resp)
{
    char current[4096];
    char redirect[2048];
    size_t limit = (body_len > 0) ? HTTP_MAX_RESPONSE_POST : HTTP_MAX_RESPONSE;

    if (url == NULL || resp == NULL) {
        return -EINVAL;
    }
    memset(resp, 0, sizeof(*resp));
    if (timeout_ms <= 0) {
        timeout_ms = 6000;
    }
    snprintf(current, sizeof(current), "%s", url);
    for (int i = 0; i <= HTTP_MAX_REDIRECTS; i++) {
        int ret = http_once(method, current, body, body_len, content_type,
                            extra_headers, timeout_ms, cancel, resp, limit,
                            redirect, sizeof(redirect));
        if (ret == 1) {
            if (i == HTTP_MAX_REDIRECTS) {
                return -ELOOP;
            }
            {
                char next[4096];
                if (build_redirect(current, redirect, next, sizeof(next)) != 0) {
                    return -EINVAL;
                }
                snprintf(current, sizeof(current), "%s", next);
            }
            continue;
        }
        return ret;
    }
    return -ELOOP;
}

int http_get_ex(const char *url, int timeout_ms, volatile int *cancel,
                http_response_t *resp)
{
    return http_request("GET", url, NULL, 0, NULL, NULL, timeout_ms, cancel, resp);
}

int http_get(const char *url, int timeout_ms, http_response_t *resp)
{
    return http_get_ex(url, timeout_ms, NULL, resp);
}

int http_post_ex(const char *url, const char *body, int body_len,
                 const char *content_type, int timeout_ms, volatile int *cancel,
                 http_response_t *resp)
{
    size_t len;
    if (url == NULL || body == NULL || resp == NULL) {
        return -EINVAL;
    }
    len = body_len < 0 ? strlen(body) : (size_t)body_len;
    return http_request("POST", url, body, len, content_type, NULL, timeout_ms,
                        cancel, resp);
}

int http_post_headers(const char *url, const char *body, int body_len,
                      const char *content_type, const char *extra_headers,
                      int timeout_ms, volatile int *cancel,
                      http_response_t *resp)
{
    size_t len;

    if (url == NULL || body == NULL || resp == NULL) {
        return -EINVAL;
    }
    len = body_len < 0 ? strlen(body) : (size_t)body_len;
    return http_request("POST", url, body, len, content_type, extra_headers,
                        timeout_ms, cancel, resp);
}

void http_response_free(http_response_t *resp)
{
    if (resp == NULL) {
        return;
    }
    free(resp->body);
    resp->body = NULL;
    resp->len = 0;
    resp->status_code = 0;
}
