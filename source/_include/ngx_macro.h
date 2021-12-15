#pragma once

#define NGX_ERROR_LOG_PATH "error.log"

#define NGX_LOG_STDERR 0
#define NGX_LOG_EMERGENCY 1
#define NGX_LOG_ALERT 2
#define NGX_LOG_CRITICAL 3
#define NGX_LOG_ERROR 4
#define NGX_LOG_WARN 5
#define NGX_LOG_NOTICE 6
#define NGX_LOG_INFO 7
#define NGX_LOG_DEBUG 8

#define NGX_MAX_ERROR_STR 2048 /*日志最大字符长度*/
#define NGX_INT64_LEN (sizeof("-9223372036854775808") - 1)