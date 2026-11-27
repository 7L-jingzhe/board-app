/**
 * @file board_error.h
 * @brief Board App core 的通用错误码。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef BOARD_ERROR_H
#define BOARD_ERROR_H

#include <errno.h>

#define BOARD_OK           0
#define BOARD_ERR_INVALID  (-EINVAL)
#define BOARD_ERR_NOMEM    (-ENOMEM)
#define BOARD_ERR_IO       (-EIO)
#define BOARD_ERR_TIMEOUT  (-ETIMEDOUT)
#define BOARD_ERR_NOENT    (-ENOENT)
#define BOARD_ERR_AGAIN    (-EAGAIN)
#define BOARD_ERR_PARSE    (-EBADMSG)
#define BOARD_ERR_FULL     (-ENOSPC)

#endif /* BOARD_ERROR_H */
