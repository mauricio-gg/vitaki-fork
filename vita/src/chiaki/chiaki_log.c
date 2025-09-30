#include "chiaki_log.h"

#include <string.h>
#include <time.h>

#ifdef __vita__
#include <psp2/kernel/clib.h>
#endif

void chiaki_log_init(ChiakiLog *log, uint32_t mask, ChiakiLogCb cb,
                     void *user) {
  if (!log) return;

  log->mask = mask;
  log->cb = cb;
  log->user = user;
}

const char *chiaki_log_level_string(ChiakiLogLevel level) {
  switch (level) {
    case CHIAKI_LOG_LEVEL_VERBOSE:
      return "VERBOSE";
    case CHIAKI_LOG_LEVEL_DEBUG:
      return "DEBUG";
    case CHIAKI_LOG_LEVEL_INFO:
      return "INFO";
    case CHIAKI_LOG_LEVEL_WARNING:
      return "WARNING";
    case CHIAKI_LOG_LEVEL_ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

uint32_t chiaki_log_level_mask(ChiakiLogLevel level) {
  switch (level) {
    case CHIAKI_LOG_LEVEL_VERBOSE:
      return CHIAKI_LOG_VERBOSE;
    case CHIAKI_LOG_LEVEL_DEBUG:
      return CHIAKI_LOG_DEBUG;
    case CHIAKI_LOG_LEVEL_INFO:
      return CHIAKI_LOG_INFO;
    case CHIAKI_LOG_LEVEL_WARNING:
      return CHIAKI_LOG_WARNING;
    case CHIAKI_LOG_LEVEL_ERROR:
      return CHIAKI_LOG_ERROR;
    default:
      return 0;
  }
}

void chiaki_log_cb_print(ChiakiLogLevel level, const char *msg, void *user) {
  FILE *out = (level >= CHIAKI_LOG_LEVEL_WARNING) ? stderr : stdout;

  // Get current time
  time_t now;
  time(&now);
  struct tm *tm_info = localtime(&now);
  char time_buf[32];
  strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);

  // Print with color codes if supported
  const char *color_start = "";
  const char *color_end = "";

#ifndef __vita__
  if (isatty(fileno(out))) {
    switch (level) {
      case CHIAKI_LOG_LEVEL_VERBOSE:
        color_start = "\033[0;37m";  // Gray
        break;
      case CHIAKI_LOG_LEVEL_DEBUG:
        color_start = "\033[0;36m";  // Cyan
        break;
      case CHIAKI_LOG_LEVEL_INFO:
        color_start = "\033[0;32m";  // Green
        break;
      case CHIAKI_LOG_LEVEL_WARNING:
        color_start = "\033[0;33m";  // Yellow
        break;
      case CHIAKI_LOG_LEVEL_ERROR:
        color_start = "\033[0;31m";  // Red
        break;
    }
    color_end = "\033[0m";
  }
#endif

  fprintf(out, "%s[%s] [%-7s] %s%s\n", color_start, time_buf,
          chiaki_log_level_string(level), msg, color_end);
  fflush(out);
}

#ifdef __vita__
void chiaki_log_cb_vita(ChiakiLogLevel level, const char *msg, void *user) {
  // Get current time
  time_t now;
  time(&now);
  struct tm *tm_info = localtime(&now);
  char time_buf[32];
  strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);

  sceClibPrintf("[%s] [%-7s] %s\n", time_buf, chiaki_log_level_string(level),
                msg);
}
#endif

void chiaki_logv(ChiakiLog *log, ChiakiLogLevel level, const char *fmt,
                 va_list args) {
  if (!log || !log->cb) return;

  // Check if this level should be logged
  if (!(log->mask & chiaki_log_level_mask(level))) return;

  // Format the message
  char msg[1024];
  vsnprintf(msg, sizeof(msg), fmt, args);
  msg[sizeof(msg) - 1] = '\0';

  // Call the callback
  log->cb(level, msg, log->user);
}

void chiaki_log(ChiakiLog *log, ChiakiLogLevel level, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  chiaki_logv(log, level, fmt, args);
  va_end(args);
}

#define HEXDUMP_WIDTH 0x10

static const char hex_char[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

void chiaki_log_hexdump(ChiakiLog *log, ChiakiLogLevel level,
                        const uint8_t *buf, size_t buf_size) {
  if (log && !(chiaki_log_level_mask(level) & log->mask)) return;

  chiaki_log(log, level,
             "offset 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  "
             "0123456789abcdef");

  size_t offset = 0;

  char hex_buf[HEXDUMP_WIDTH * 3 + 1];
  char ascii_buf[HEXDUMP_WIDTH + 1];
  for (size_t i = 0; i < HEXDUMP_WIDTH; i++) hex_buf[i * 3 + 2] = ' ';
  hex_buf[HEXDUMP_WIDTH * 3] = '\0';
  ascii_buf[HEXDUMP_WIDTH] = '\0';

  while (buf_size > 0) {
    for (size_t i = 0; i < HEXDUMP_WIDTH; i++) {
      if (i < buf_size) {
        uint8_t b = buf[i];
        hex_buf[i * 3] = hex_char[b >> 4];
        hex_buf[i * 3 + 1] = hex_char[b & 0xf];

        if (b > 0x20 && b < 0x7f)
          ascii_buf[i] = (char)b;
        else
          ascii_buf[i] = '.';
      } else {
        hex_buf[i * 3] = ' ';
        hex_buf[i * 3 + 1] = ' ';
        ascii_buf[i] = ' ';
      }

      hex_buf[i * 3 + 2] = ' ';
    }

    chiaki_log(log, level, "%6x %s%s", offset, hex_buf, ascii_buf);

    if (buf_size > HEXDUMP_WIDTH) {
      buf_size -= HEXDUMP_WIDTH;
      buf += HEXDUMP_WIDTH;
      offset += HEXDUMP_WIDTH;
    } else
      break;
  }
}