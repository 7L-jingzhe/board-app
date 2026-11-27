/**
 * @file tls_client.h
 * @brief 极简 TLS 客户端（基于板端自带的 OpenSSL 1.1，运行时 dlopen）。
 *
 * 为什么用 dlopen 而不是链接 libssl：
 *   交叉编译用的 sysroot 里没有 openssl 的开发头文件/符号库，但**板端运行时有**
 *   `/usr/lib/libssl.so.1.1` + `/usr/lib/libcrypto.so.1.1` + `/etc/ssl/certs`。
 *   所以这里自己声明所需的 OpenSSL 函数原型，运行时动态解析符号：
 *   编译期零依赖，板上有库就能用，没有就 `tls_available()` 返回 false 并退回明文。
 *
 * 只实现客户端握手 + 收发，不做会话复用/OCSP 等高级功能。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef TLS_CLIENT_H
#define TLS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tls_conn tls_conn_t;

/** 板端是否有可用的 OpenSSL（结果缓存）。 */
bool tls_available(void);

/** 最近一次失败原因（给日志用）。 */
const char *tls_last_error(void);

/**
 * 在已 connect 的 TCP fd 上完成 TLS 握手（带 SNI + 证书校验）。
 * @param fd   已连接的 socket
 * @param host 服务器名（用于 SNI 与证书校验）
 * @return 连接对象；失败返回 NULL（原因见 tls_last_error）
 */
tls_conn_t *tls_connect(int fd, const char *host);

/** 写入全部字节。@return 0 成功，负值失败。 */
int tls_write(tls_conn_t *c, const void *buf, size_t len);

/** 读取。@return >0 字节数；0 对端关闭；<0 错误（-EAGAIN 可重试）。 */
int tls_read(tls_conn_t *c, void *buf, size_t len);

/** 关闭并释放（不关闭底层 fd，由调用者关闭）。 */
void tls_close(tls_conn_t *c);

#ifdef __cplusplus
}
#endif

#endif /* TLS_CLIENT_H */
