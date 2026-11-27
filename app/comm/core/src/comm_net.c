/**
 * @file comm_net.c
 * @brief POSIX socket 封装实现。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "comm/comm_net.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>


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
    if (pfd.revents & (POLLERR | POLLNVAL)) {
        return -EIO;
    }
    return 0;
}

int comm_set_nonblock(int fd, int on)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -errno;
    }
    if (on) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(fd, F_SETFL, flags) < 0 ? -errno : 0;
}

int comm_tcp_listen(const char *bind_ip, uint16_t port, int backlog)
{
    int fd;
    int on = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -errno;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bind_ip == NULL || bind_ip[0] == '\0' || strcmp(bind_ip, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
        close(fd);
        return -EINVAL;
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int err = -errno;
        close(fd);
        return err;
    }
    if (listen(fd, backlog) < 0) {
        int err = -errno;
        close(fd);
        return err;
    }
    return fd;
}

int comm_tcp_connect(const char *ip, uint16_t port, int timeout_ms)
{
    int fd;
    int ret;
    struct sockaddr_in addr;

    if (ip == NULL || port == 0) {
        return -EINVAL;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -errno;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        close(fd);
        return -EINVAL;
    }

    comm_set_nonblock(fd, 1);
    ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0 && errno == EINPROGRESS) {
        ret = wait_fd(fd, POLLOUT, timeout_ms);
        if (ret == 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
                ret = -errno;
            } else if (err != 0) {
                ret = -err;
            }
        }
    } else if (ret < 0) {
        ret = -errno;
    }
    comm_set_nonblock(fd, 0);
    if (ret != 0) {
        close(fd);
        return ret;
    }
    return fd;
}

int comm_send_all(int fd, const void *data, size_t len)
{
    const uint8_t *p = data;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                int rc = wait_fd(fd, POLLOUT, 2000);
                if (rc != 0) {
                    return rc;
                }
                continue;
            }
            return -errno;
        }
        if (n == 0) {
            return -ECONNRESET;
        }
        sent += (size_t)n;
    }
    return 0;
}

int comm_recv_all(int fd, void *data, size_t len, int timeout_ms)
{
    uint8_t *p = data;
    size_t got = 0;

    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* 每收到一部分就重新计时，避免大帧被误判超时。 */
                int rc = wait_fd(fd, POLLIN, timeout_ms);
                if (rc != 0) {
                    return rc;
                }
                continue;
            }
            return -errno;
        }
        if (n == 0) {
            return -ECONNRESET;
        }
        got += (size_t)n;
    }
    return 0;
}

int comm_udp_bind(const char *bind_ip, uint16_t port)
{
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -errno;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bind_ip == NULL || bind_ip[0] == '\0' || strcmp(bind_ip, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
        close(fd);
        return -EINVAL;
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int err = -errno;
        close(fd);
        return err;
    }
    return fd;
}

int comm_udp_send(int fd, const char *ip, uint16_t port, const void *data,
                  size_t len)
{
    struct sockaddr_in addr;

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        return -EINVAL;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (sendto(fd, data, len, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        return -errno;
    }
    return 0;
}

int comm_udp_recv(int fd, void *data, size_t cap, int timeout_ms, char *from_ip,
                  uint16_t *from_port)
{
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    ssize_t n;
    int rc;

    rc = wait_fd(fd, POLLIN, timeout_ms);
    if (rc != 0) {
        return rc;
    }
    n = recvfrom(fd, data, cap, 0, (struct sockaddr *)&addr, &alen);
    if (n < 0) {
        return -errno;
    }
    if (from_ip != NULL) {
        inet_ntop(AF_INET, &addr.sin_addr, from_ip, INET_ADDRSTRLEN);
    }
    if (from_port != NULL) {
        *from_port = ntohs(addr.sin_port);
    }
    return (int)n;
}

int comm_local_port(int fd)
{
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);

    if (getsockname(fd, (struct sockaddr *)&addr, &alen) < 0) {
        return -errno;
    }
    return (int)ntohs(addr.sin_port);
}

int comm_parse_hostport(const char *text, char *ip, size_t ip_cap, uint16_t *port)
{
    const char *colon;
    size_t n;

    if (text == NULL || ip == NULL || port == NULL) {
        return -EINVAL;
    }
    colon = strrchr(text, ':');
    if (colon == NULL) {
        return -EINVAL;
    }
    n = (size_t)(colon - text);
    if (n == 0 || n >= ip_cap) {
        return -EINVAL;
    }
    memcpy(ip, text, n);
    ip[n] = '\0';
    *port = (uint16_t)atoi(colon + 1);
    return *port == 0 ? -EINVAL : 0;
}

int comm_write_file_atomic(const char *path, const char *data, size_t len)
{
    char tmp[512];
    FILE *f;

    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return -ENAMETOOLONG;
    }
    f = fopen(tmp, "wb");
    if (f == NULL) {
        return -errno;
    }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        fclose(f);
        unlink(tmp);
        return -EIO;
    }
    if (fclose(f) != 0) {
        unlink(tmp);
        return -EIO;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -errno;
    }
    return 0;
}
