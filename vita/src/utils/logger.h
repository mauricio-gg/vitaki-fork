#ifndef VITARPS5_LOGGER_H
#define VITARPS5_LOGGER_H

#include <stdbool.h>

typedef enum {
  LOG_LEVEL_ERROR = 0,
  LOG_LEVEL_WARN = 1,
  LOG_LEVEL_INFO = 2,
  LOG_LEVEL_DEBUG = 3
} LogLevel;

// Initialize logger with thread safety enabled/disabled
// Thread safety adds mutex overhead but prevents crashes
void logger_init(void);
void logger_init_with_thread_safety(bool enable_thread_safety);
void logger_shutdown(void);
void logger_set_level(LogLevel level);
bool logger_is_initialized(void);
void logger_log(LogLevel level, const char* file, int line, const char* fmt,
                ...);

#define log_error(...) \
  logger_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...) \
  logger_log(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_warning(...) \
  logger_log(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...) \
  logger_log(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...) \
  logger_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

#endif  // VITARPS5_LOGGER_H