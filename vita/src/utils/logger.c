#include "logger.h"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Thread-local buffer to avoid contention
#define LOG_BUFFER_SIZE 1024
#define MUTEX_TIMEOUT_US 100000  // 100ms timeout for mutex operations

// Error codes specific to logger
#define LOGGER_ERROR_MUTEX_CREATE -1
#define LOGGER_ERROR_FILE_OPEN -2
#define LOGGER_ERROR_MUTEX_TIMEOUT -3

// Logger state
static struct {
  LogLevel log_level;
  SceUID log_file;
  SceUID mutex_id;
  bool thread_safe;
  bool initialized;
} g_logger = {.log_level = LOG_LEVEL_INFO,
              .log_file = -1,
              .mutex_id = -1,
              .thread_safe = true,  // Default to thread-safe
              .initialized = false};

void logger_init(void) { logger_init_with_thread_safety(true); }

void logger_init_with_thread_safety(bool enable_thread_safety) {
  if (g_logger.initialized) {
    return;  // Already initialized
  }

  g_logger.log_level = LOG_LEVEL_DEBUG;
  g_logger.thread_safe = enable_thread_safety;

  // Create mutex for thread safety if enabled
  if (g_logger.thread_safe) {
    g_logger.mutex_id = sceKernelCreateMutex("VitaRPS5Logger", 0, 0, NULL);
    if (g_logger.mutex_id < 0) {
      sceClibPrintf("Failed to create logger mutex: 0x%08X\n",
                    g_logger.mutex_id);
      g_logger.mutex_id = LOGGER_ERROR_MUTEX_CREATE;
      g_logger.thread_safe = false;  // Fall back to non-thread-safe mode
    }
  }

  // Create logs directory if it doesn't exist
  sceIoMkdir("ux0:data/VitaRPS5", 0777);
  sceIoMkdir("ux0:data/VitaRPS5/logs", 0777);

  // Open log file
  g_logger.log_file = sceIoOpen("ux0:data/VitaRPS5/logs/vitarps5.log",
                                SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (g_logger.log_file < 0) {
    sceClibPrintf("Failed to open log file: 0x%08X\n", g_logger.log_file);
    g_logger.log_file = LOGGER_ERROR_FILE_OPEN;
  } else {
    sceClibPrintf("Log file opened: ux0:data/VitaRPS5/logs/vitarps5.log\n");
    if (g_logger.thread_safe) {
      sceClibPrintf("Logger thread safety: ENABLED\n");
    } else {
      sceClibPrintf("Logger thread safety: DISABLED\n");
    }
  }

  g_logger.initialized = true;
}

void logger_shutdown(void) {
  if (!g_logger.initialized) {
    return;
  }

  // Lock mutex before cleanup with timeout
  if (g_logger.thread_safe && g_logger.mutex_id >= 0) {
    unsigned int timeout = MUTEX_TIMEOUT_US;
    sceKernelLockMutex(g_logger.mutex_id, 1, &timeout);
  }

  // Close log file
  if (g_logger.log_file >= 0) {
    sceIoClose(g_logger.log_file);
    g_logger.log_file = -1;
  }

  // Unlock and delete mutex
  if (g_logger.thread_safe && g_logger.mutex_id >= 0) {
    sceKernelUnlockMutex(g_logger.mutex_id, 1);
    sceKernelDeleteMutex(g_logger.mutex_id);
    g_logger.mutex_id = -1;
  }

  g_logger.initialized = false;
}

void logger_set_level(LogLevel level) {
  // Atomic assignment, no mutex needed
  g_logger.log_level = level;
}

bool logger_is_initialized(void) { return g_logger.initialized; }

void logger_log(LogLevel level, const char* file, int line, const char* fmt,
                ...) {
  if (!g_logger.initialized || level > g_logger.log_level) {
    return;
  }

  // Use stack buffer to avoid thread contention
  char log_buffer[LOG_BUFFER_SIZE];
  char message[512];

  const char* level_str;
  switch (level) {
    case LOG_LEVEL_ERROR:
      level_str = "ERROR";
      break;
    case LOG_LEVEL_WARN:
      level_str = "WARN ";
      break;
    case LOG_LEVEL_INFO:
      level_str = "INFO ";
      break;
    case LOG_LEVEL_DEBUG:
      level_str = "DEBUG";
      break;
    default:
      level_str = "?????";
      break;
  }

  // Extract filename from path
  const char* filename = strrchr(file, '/');
  if (filename) {
    filename++;
  } else {
    filename = file;
  }

  // Get timestamp
  SceDateTime time;
  sceRtcGetCurrentClockLocalTime(&time);

  // Format the message with bounds checking
  va_list args;
  va_start(args, fmt);
  int msg_len = sceClibVsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  if (msg_len < 0) {
    return;  // Formatting error
  }

  // Format the full log line
  int written = sceClibSnprintf(
      log_buffer, sizeof(log_buffer), "[%02d:%02d:%02d.%03d] [%s] %s:%d - %s\n",
      time.hour, time.minute, time.second, time.microsecond / 1000, level_str,
      filename, line, message);

  if (written <= 0 || written >= (int)sizeof(log_buffer)) {
    return;  // Buffer overflow or error
  }

  // Lock mutex for console and file operations with timeout
  if (g_logger.thread_safe && g_logger.mutex_id >= 0) {
    unsigned int timeout = MUTEX_TIMEOUT_US;
    int lock_result = sceKernelLockMutex(g_logger.mutex_id, 1, &timeout);
    if (lock_result < 0) {
      // Failed to lock or timeout occurred
      // For critical errors, still try to output to console
      if (level == LOG_LEVEL_ERROR) {
        sceClibPrintf("[LOGGER MUTEX TIMEOUT] %s", log_buffer);
      }
      return;
    }
  }

  // Output to debug console
  sceClibPrintf("%s", log_buffer);

  // Write to file if available
  if (g_logger.log_file >= 0) {
    SceSize bytes_to_write = (SceSize)strlen(log_buffer);
    int write_result =
        sceIoWrite(g_logger.log_file, log_buffer, bytes_to_write);

    if (write_result < 0 || (SceSize)write_result != bytes_to_write) {
      // Write error occurred, but we can't log it (would be recursive)
      // Just continue, as console output still worked
    } else {
      // Flush on errors or warnings for immediate persistence
      if (level <= LOG_LEVEL_WARN) {
        sceIoSyncByFd(g_logger.log_file, 0);
      }
    }
  }

  // Unlock mutex
  if (g_logger.thread_safe && g_logger.mutex_id >= 0) {
    sceKernelUnlockMutex(g_logger.mutex_id, 1);
  }
}