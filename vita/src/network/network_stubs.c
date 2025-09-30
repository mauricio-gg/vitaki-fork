// network_stubs.c - Stub implementations for missing network functions
#include <stdint.h>

#include "../core/vitarps5.h"
#include "session_init.h"

// Stub network functions for compilation
int udp_send(const char *ip, uint16_t port, const void *buf, size_t len) {
  // Stub implementation - replace with actual UDP send
  return -1;  // Return error for now
}

int udp_recv_timeout(char *out, size_t cap, int timeout_ms) {
  // Stub implementation - replace with actual UDP receive with timeout
  return -1;  // Return error for now
}

int http_send_and_recv_9295(const char *ip, const char *req, size_t req_len,
                            SessionInitResponse *resp) {
  // Stub implementation - replace with actual HTTP client for port 9295
  if (resp) {
    resp->http_status = 500;  // Return server error for now
  }
  return -1;  // Return error for now
}