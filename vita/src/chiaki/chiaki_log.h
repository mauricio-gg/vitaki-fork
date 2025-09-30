#ifndef CHIAKI_LOG_H
#define CHIAKI_LOG_H

#include <stdarg.h>
#include <stdio.h>

#include "chiaki_common.h"

// Log level masks
#define CHIAKI_LOG_VERBOSE (1 << 0)
#define CHIAKI_LOG_DEBUG (1 << 1)
#define CHIAKI_LOG_INFO (1 << 2)
#define CHIAKI_LOG_WARNING (1 << 3)
#define CHIAKI_LOG_ERROR (1 << 4)
#define CHIAKI_LOG_ALL 0xffffffff

#ifndef CHIAKI_LOG_LEVEL_DEFINED
#define CHIAKI_LOG_LEVEL_DEFINED
typedef enum {
  CHIAKI_LOG_LEVEL_VERBOSE = 0,
  CHIAKI_LOG_LEVEL_DEBUG = 1,
  CHIAKI_LOG_LEVEL_INFO = 2,
  CHIAKI_LOG_LEVEL_WARNING = 3,
  CHIAKI_LOG_LEVEL_ERROR = 4
} ChiakiLogLevel;
#endif

#ifndef CHIAKI_LOG_STRUCT_DEFINED
#define CHIAKI_LOG_STRUCT_DEFINED
typedef struct chiaki_log_t ChiakiLog;
typedef void (*ChiakiLogCb)(ChiakiLogLevel level, const char *msg, void *user);

struct chiaki_log_t {
  uint32_t mask;
  ChiakiLogCb cb;
  void *user;
};
#endif

// Logging functions - only define if not already defined
#ifndef CHIAKI_LOG_FUNCTIONS_DEFINED
#define CHIAKI_LOG_FUNCTIONS_DEFINED

// Initialize log structure
void chiaki_log_init(ChiakiLog *log, uint32_t mask, ChiakiLogCb cb, void *user);

// Default callback that prints to stdout/stderr
void chiaki_log_cb_print(ChiakiLogLevel level, const char *msg, void *user);

// PS Vita specific callback that uses sceClibPrintf
#ifdef __vita__
void chiaki_log_cb_vita(ChiakiLogLevel level, const char *msg, void *user);
#endif

// Core logging function
void chiaki_log(ChiakiLog *log, ChiakiLogLevel level, const char *fmt, ...);
void chiaki_logv(ChiakiLog *log, ChiakiLogLevel level, const char *fmt,
                 va_list args);

// Utility functions
const char *chiaki_log_level_string(ChiakiLogLevel level);
uint32_t chiaki_log_level_mask(ChiakiLogLevel level);

// Hexdump function for debugging
void chiaki_log_hexdump(ChiakiLog *log, ChiakiLogLevel level,
                        const uint8_t *buf, size_t buf_size);

#endif

// Logging macros
#define CHIAKI_LOGV(log, fmt, ...) \
  chiaki_log(log, CHIAKI_LOG_LEVEL_VERBOSE, fmt, ##__VA_ARGS__)
#define CHIAKI_LOGD(log, fmt, ...) \
  chiaki_log(log, CHIAKI_LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define CHIAKI_LOGI(log, fmt, ...) \
  chiaki_log(log, CHIAKI_LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define CHIAKI_LOGW(log, fmt, ...) \
  chiaki_log(log, CHIAKI_LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define CHIAKI_LOGE(log, fmt, ...) \
  chiaki_log(log, CHIAKI_LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

#endif  // CHIAKI_LOG_H