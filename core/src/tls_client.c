/**
 * @file tls_client.c
 * @brief 运行时 dlopen 板端 OpenSSL 1.1 实现的极简 TLS 客户端。
 *
 * 只用到这些符号（OpenSSL 1.1 的稳定 ABI）：
 *   TLS_client_method, SSL_CTX_new, SSL_CTX_set_verify,
 *   SSL_CTX_load_verify_locations, SSL_new, SSL_set_fd, SSL_ctrl,
 *   SSL_connect, SSL_write, SSL_read, SSL_get_error,
 *   SSL_get_verify_result, SSL_shutdown, SSL_free, SSL_CTX_free
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_core/tls_client.h"

#include <dlfcn.h>
#include <poll.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OpenSSL 常量（避免依赖头文件） */
#define X509_V_OK 0
#define SSL_VERIFY_PEER 0x01
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3
#define SSL_ERROR_ZERO_RETURN 6
#define SSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define TLSEXT_NAMETYPE_host_name 0

/* 证书目录（板端 Buildroot 提供） */
static const char *CA_DIRS[] = { "/etc/ssl/certs", "/usr/share/ca-certificates", NULL };
static const char *CA_FILES[] = { "/etc/ssl/certs/ca-certificates.crt",
                                  "/etc/ssl/cert.pem", NULL };

typedef void SSL_CTX;
typedef void SSL;
typedef void SSL_METHOD;

struct tls_conn {
    SSL *ssl;
    SSL_CTX *ctx;
    int fd;
};

/** 等待 fd 可读/可写（socket 在 http_client 里是非阻塞的）。 */
static int wait_fd(int fd, short events, int timeout_ms)
{
    struct pollfd pfd;
    int rc;

    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);
    if (rc == 0) {
        return -ETIMEDOUT;
    }
    if (rc < 0) {
        return -errno;
    }
    return 0;
}

typedef struct {
    bool ready;
    bool tried;
    void *libssl;
    void *libcrypto;
    const SSL_METHOD *(*TLS_client_method)(void);
    SSL_CTX *(*SSL_CTX_new)(const SSL_METHOD *);
    void (*SSL_CTX_free)(SSL_CTX *);
    void (*SSL_CTX_set_verify)(SSL_CTX *, int, void *);
    int (*SSL_CTX_load_verify_locations)(SSL_CTX *, const char *, const char *);
    SSL *(*SSL_new)(SSL_CTX *);
    void (*SSL_free)(SSL *);
    int (*SSL_set_fd)(SSL *, int);
    long (*SSL_ctrl)(SSL *, int, long, void *);
    int (*SSL_connect)(SSL *);
    int (*SSL_write)(SSL *, const void *, int);
    int (*SSL_read)(SSL *, void *, int);
    int (*SSL_get_error)(const SSL *, int);
    long (*SSL_get_verify_result)(const SSL *);
    int (*SSL_shutdown)(SSL *);
} ssl_api_t;

static ssl_api_t s_api;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static char s_err[192];

static const char *tls_err(const char *msg)
{
    snprintf(s_err, sizeof(s_err), "%s", msg);
    return s_err;
}

/** 从若干候选库名里挑一个能 dlopen 的。 */
static void *dl_first(const char *const *names)
{
    int i;
    for (i = 0; names[i] != NULL; i++) {
        void *h = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
        if (h != NULL) {
            return h;
        }
    }
    return NULL;
}

bool tls_available(void)
{
    static const char *const SSL_LIBS[] = { "libssl.so.1.1", "libssl.so.3",
                                            "libssl.so", NULL };
    static const char *const CRYPTO_LIBS[] = { "libcrypto.so.1.1", "libcrypto.so.3",
                                               "libcrypto.so", NULL };
    bool ok = false;

    pthread_mutex_lock(&s_lock);
    if (!s_api.tried) {
        s_api.tried = true;
        s_api.libcrypto = dl_first(CRYPTO_LIBS);
        s_api.libssl = dl_first(SSL_LIBS);
        if (s_api.libssl == NULL) {
            tls_err("libssl not found");
        } else if ((s_api.TLS_client_method = dlsym(s_api.libssl, "TLS_client_method")) == NULL ||
                   (s_api.SSL_CTX_new = dlsym(s_api.libssl, "SSL_CTX_new")) == NULL ||
                   (s_api.SSL_CTX_free = dlsym(s_api.libssl, "SSL_CTX_free")) == NULL ||
                   (s_api.SSL_CTX_set_verify = dlsym(s_api.libssl, "SSL_CTX_set_verify")) == NULL ||
                   (s_api.SSL_CTX_load_verify_locations =
                        dlsym(s_api.libssl, "SSL_CTX_load_verify_locations")) == NULL ||
                   (s_api.SSL_new = dlsym(s_api.libssl, "SSL_new")) == NULL ||
                   (s_api.SSL_free = dlsym(s_api.libssl, "SSL_free")) == NULL ||
                   (s_api.SSL_set_fd = dlsym(s_api.libssl, "SSL_set_fd")) == NULL ||
                   (s_api.SSL_ctrl = dlsym(s_api.libssl, "SSL_ctrl")) == NULL ||
                   (s_api.SSL_connect = dlsym(s_api.libssl, "SSL_connect")) == NULL ||
                   (s_api.SSL_write = dlsym(s_api.libssl, "SSL_write")) == NULL ||
                   (s_api.SSL_read = dlsym(s_api.libssl, "SSL_read")) == NULL ||
                   (s_api.SSL_get_error = dlsym(s_api.libssl, "SSL_get_error")) == NULL ||
                   (s_api.SSL_shutdown = dlsym(s_api.libssl, "SSL_shutdown")) == NULL) {
            tls_err("libssl symbols missing");
            s_api.libssl = NULL;
        } else {
            s_api.SSL_get_verify_result =
                dlsym(s_api.libssl, "SSL_get_verify_result"); /* 可选 */
            ok = true;
        }
        s_api.ready = ok;
    } else {
        ok = s_api.ready;
    }
    pthread_mutex_unlock(&s_lock);
    return ok;
}

const char *tls_last_error(void)
{
    return s_err[0] != '\0' ? s_err : "unknown";
}

static SSL_CTX *make_ctx(void)
{
    SSL_CTX *ctx = s_api.SSL_CTX_new(s_api.TLS_client_method());
    int i;

    if (ctx == NULL) {
        tls_err("SSL_CTX_new failed");
        return NULL;
    }
    if (s_api.SSL_get_verify_result != NULL) {
        s_api.SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    }
    /* 先试 CA 文件，再试 CA 目录；都没有就放弃连接（不降级成不校验） */
    for (i = 0; CA_FILES[i] != NULL; i++) {
        if (s_api.SSL_CTX_load_verify_locations(ctx, CA_FILES[i], NULL) == 1) {
            return ctx;
        }
    }
    for (i = 0; CA_DIRS[i] != NULL; i++) {
        if (s_api.SSL_CTX_load_verify_locations(ctx, NULL, CA_DIRS[i]) == 1) {
            return ctx;
        }
    }
    tls_err("no CA bundle found (/etc/ssl/certs)");
    s_api.SSL_CTX_free(ctx);
    return NULL;
}

tls_conn_t *tls_connect(int fd, const char *host)
{
    tls_conn_t *c;
    SSL_CTX *ctx;
    SSL *ssl;

    if (!tls_available() || host == NULL) {
        return NULL;
    }
    ctx = make_ctx();
    if (ctx == NULL) {
        return NULL;
    }
    ssl = s_api.SSL_new(ctx);
    if (ssl == NULL) {
        tls_err("SSL_new failed");
        s_api.SSL_CTX_free(ctx);
        return NULL;
    }
    s_api.SSL_set_fd(ssl, fd);
    /* SNI：DeepSeek 之类的站点在 CDN 后面，不带 SNI 会握手失败 */
    s_api.SSL_ctrl(ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name,
                   (void *)host);

    for (;;) {
        int r = s_api.SSL_connect(ssl);
        int e;
        int w;

        if (r == 1) {
            break;
        }
        e = s_api.SSL_get_error(ssl, r);
        if (e == SSL_ERROR_WANT_READ) {
            w = wait_fd(fd, POLLIN, 15000);
        } else if (e == SSL_ERROR_WANT_WRITE) {
            w = wait_fd(fd, POLLOUT, 15000);
        } else {
            w = -1;
        }
        if (w != 0) {
            snprintf(s_err, sizeof(s_err), "TLS handshake failed (ssl err %d)", e);
            s_api.SSL_free(ssl);
            s_api.SSL_CTX_free(ctx);
            return NULL;
        }
    }
    if (s_api.SSL_get_verify_result != NULL &&
        s_api.SSL_get_verify_result(ssl) != X509_V_OK) {
        snprintf(s_err, sizeof(s_err), "certificate verify failed (%ld)",
                 s_api.SSL_get_verify_result(ssl));
        s_api.SSL_free(ssl);
        s_api.SSL_CTX_free(ctx);
        return NULL;
    }

    c = calloc(1, sizeof(*c));
    if (c == NULL) {
        s_api.SSL_free(ssl);
        s_api.SSL_CTX_free(ctx);
        return NULL;
    }
    c->ssl = ssl;
    c->ctx = ctx;
    c->fd = fd;
    return c;
}

int tls_write(tls_conn_t *c, const void *buf, size_t len)
{
    size_t sent = 0;

    if (c == NULL) {
        return -EINVAL;
    }
    while (sent < len) {
        int n = s_api.SSL_write(c->ssl, (const char *)buf + sent, (int)(len - sent));
        if (n <= 0) {
            int e = s_api.SSL_get_error(c->ssl, n);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                int w = wait_fd(c->fd, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT,
                                15000);
                if (w != 0) {
                    snprintf(s_err, sizeof(s_err), "SSL_write wait failed");
                    return w;
                }
                continue;
            }
            snprintf(s_err, sizeof(s_err), "SSL_write failed (%d)", e);
            return -EIO;
        }
        sent += (size_t)n;
    }
    return 0;
}

int tls_read(tls_conn_t *c, void *buf, size_t len)
{
    int n;

    if (c == NULL) {
        return -EINVAL;
    }
    n = s_api.SSL_read(c->ssl, buf, (int)len);
    if (n > 0) {
        return n;
    }
    {
        int e = s_api.SSL_get_error(c->ssl, n);
        if (e == SSL_ERROR_ZERO_RETURN) {
            return 0; /* 对端正常关闭 */
        }
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            return -EAGAIN;
        }
        snprintf(s_err, sizeof(s_err), "SSL_read failed (%d)", e);
        return -EIO;
    }
}

void tls_close(tls_conn_t *c)
{
    if (c == NULL) {
        return;
    }
    if (c->ssl != NULL) {
        s_api.SSL_shutdown(c->ssl); /* 尽力而为 */
        s_api.SSL_free(c->ssl);
    }
    if (c->ctx != NULL) {
        s_api.SSL_CTX_free(c->ctx);
    }
    free(c);
}
