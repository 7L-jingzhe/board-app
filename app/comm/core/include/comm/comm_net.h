/**
 * @file comm_net.h
 * @brief POSIX socket 小工具：监听/接受/连接、整帧收发、UDP 收发。
 *
 * 只做“把系统调用包一层”的活，不含任何业务逻辑，服务器与客户端共用。
 * 全部是阻塞调用；超时/取消由调用者用非阻塞 + poll 或线程关闭来管理。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef COMM_NET_H
#define COMM_NET_H

#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 监听 TCP。@return fd，失败 -errno。 */
int comm_tcp_listen(const char *bind_ip, uint16_t port, int backlog);

/** 连接 TCP（带超时 ms）。@return fd，失败 -errno。 */
int comm_tcp_connect(const char *ip, uint16_t port, int timeout_ms);

/** 把 fd 设为非阻塞（1）或阻塞（0）。 */
int comm_set_nonblock(int fd, int on);

/** 发送全部字节（阻塞）。@return 0 或 -errno。 */
int comm_send_all(int fd, const void *data, size_t len);

/**
 * 收满 len 字节。
 * @param timeout_ms <=0 表示一直等
 * @return 0 成功；-ECONNRESET 对端关闭；-ETIMEDOUT；其它 -errno
 */
int comm_recv_all(int fd, void *data, size_t len, int timeout_ms);

/** 打开 UDP 并绑定本地端口（port=0 由内核分配）。 */
int comm_udp_bind(const char *bind_ip, uint16_t port);

/** 发送一个 UDP 数据报。 */
int comm_udp_send(int fd, const char *ip, uint16_t port, const void *data,
                  size_t len);

/**
 * 收一个 UDP 数据报（带超时）。
 * @param from_ip 输出对端 IP（至少 INET_ADDRSTRLEN 字节），可为 NULL
 * @return 收到的字节数；0 超时；<0 -errno
 */
int comm_udp_recv(int fd, void *data, size_t cap, int timeout_ms, char *from_ip,
                  uint16_t *from_port);

/** 取本地端口（用于报告 udp_port）。 */
int comm_local_port(int fd);

/** "ip:port" 解析。@return 0 成功。 */
int comm_parse_hostport(const char *text, char *ip, size_t ip_cap,
                        uint16_t *port);

/** 写一个文本文件（先写临时文件再 rename，避免断电留下半个文件）。 */
int comm_write_file_atomic(const char *path, const char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* COMM_NET_H */
