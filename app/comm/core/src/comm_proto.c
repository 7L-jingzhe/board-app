/**
 * @file comm_proto.c
 * @brief 协议工具：分帧、字段校验。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "comm/comm_proto.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

#define COMM_HEADER_LEN 5u /* len(4) + type(1) */

int comm_frame_pack(uint8_t type, const char *json, size_t json_len,
                    uint8_t *out, size_t cap, size_t *out_len)
{
    uint32_t total;

    if (out == NULL || out_len == NULL) {
        return -EINVAL;
    }
    if (json == NULL) {
        json = "";
        json_len = 0;
    }
    total = (uint32_t)(1u + json_len); /* type + body */
    if (total > COMM_MAX_FRAME || total + COMM_HEADER_LEN > cap) {
        return -ENOSPC;
    }
    out[0] = (uint8_t)(total >> 24);
    out[1] = (uint8_t)(total >> 16);
    out[2] = (uint8_t)(total >> 8);
    out[3] = (uint8_t)total;
    out[4] = type;
    if (json_len > 0) {
        memcpy(out + COMM_HEADER_LEN, json, json_len);
    }
    *out_len = COMM_HEADER_LEN + json_len;
    return 0;
}

comm_frame_result_t comm_frame_peek(const uint8_t *buf, size_t len,
                                    size_t *payload_len, size_t *body_off)
{
    uint32_t total;

    if (buf == NULL || len < COMM_HEADER_LEN) {
        return COMM_FRAME_PARTIAL;
    }
    total = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
            ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    if (total < 1u || total > COMM_MAX_FRAME) {
        return COMM_FRAME_BAD;
    }
    if (len < (size_t)total + COMM_HEADER_LEN) {
        return COMM_FRAME_PARTIAL;
    }
    if (payload_len != NULL) {
        *payload_len = total;
    }
    if (body_off != NULL) {
        *body_off = COMM_HEADER_LEN;
    }
    return COMM_FRAME_READY;
}

int comm_name_valid(const char *name)
{
    size_t n;
    size_t i;

    if (name == NULL) {
        return 0;
    }
    n = strlen(name);
    if (n == 0 || n > COMM_MAX_NAME) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c <= 0x20 || c >= 0x7F || c == ':' || c == '/' || c == '\\') {
            return 0;
        }
    }
    return 1;
}

int comm_text_valid(const char *text)
{
    size_t n;
    size_t i;

    if (text == NULL) {
        return 0;
    }
    n = strlen(text);
    if (n == 0 || n > COMM_MAX_TEXT) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        /* 允许 UTF-8 高位字节与常见空白；拒绝其它控制字符。 */
        if (c < 0x20 && c != '\t') {
            return 0;
        }
    }
    return 1;
}
