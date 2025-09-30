// discovery.c
#include "discovery.h"

#include <stdio.h>
#include <string.h>

int build_discovery_probe(char *out, size_t cap) {
  // Keep it minimal & consistent
  const char *msg =
      "SRCH * HTTP/1.1\r\n"
      "device-discovery-protocol-version: 00030010\r\n"
      "\r\n";
  size_t n = strlen(msg);
  if (cap < n) return -1;
  memcpy(out, msg, n);
  return (int)n;
}

bool parse_discovery_response(const char *buf, size_t len, Ps5State *state) {
  if (!buf || len < 12 || !state) return false;
  // First line like: "HTTP/1.1 620 Server Standby" or "HTTP/1.1 200 OK"
  if (len < 12) return false;
  const char *p = strstr(buf, "HTTP/1.1 ");
  if (!p) return false;
  int code = 0;
  if (sscanf(p, "HTTP/1.1 %d", &code) != 1) return false;
  if (code == 200) {
    *state = PS5_STATE_READY;
    return true;
  }
  if (code == 620) {
    *state = PS5_STATE_STANDBY;
    return true;
  }
  *state = PS5_STATE_UNKNOWN;
  return true;
}