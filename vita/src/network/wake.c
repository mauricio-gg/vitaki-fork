// wake.c
#include "wake.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

bool is_valid_wake_credential(const char *s) {
  if (!s || !*s) return false;
  // Vitaki/Chiaki discovery WAKEUP uses a decimal user-credential derived from
  // the 8-hex RegistKey. Accept 1-20 decimal digits.
  size_t len = strlen(s);
  if (len == 0 || len > 20) return false;
  for (size_t i = 0; i < len; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

int build_wake_packet(const char *wake_credential, char *out, size_t cap) {
  if (!is_valid_wake_credential(wake_credential)) return -1;

  // PS5 Wake-up packet format (Vitaki-compatible)
  // - Transport: UDP
  // - Port: 9302 (PS5 discovery)
  // - user-credential: decimal representation of the 8-hex registration key
  // - device-discovery-protocol-version: 00030010 for PS5
  int n = snprintf(out, cap,
                   "WAKEUP * HTTP/1.1\n"
                   "client-type:vr\n"
                   "auth-type:R\n"
                   "model:w\n"
                   "app-type:r\n"
                   "user-credential:%s\n"
                   "device-discovery-protocol-version:00030010\n",
                   wake_credential);
  return (n > 0 && (size_t)n < cap) ? n : -1;
}

WakeResult wake_ps5_console(const char *console_ip,
                            const char *wake_credential) {
  if (!console_ip || !wake_credential ||
      !is_valid_wake_credential(wake_credential)) {
    return WAKE_RESULT_ERROR_INVALID_CREDENTIAL;
  }

  // PS5 Wake-up Protocol (Vitaki-compatible):
  // Send HTTP-like wake packet with decimal credential to port 9302

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    return WAKE_RESULT_ERROR_NETWORK;
  }

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  if (inet_aton(console_ip, &addr.sin_addr) == 0) {
    close(sock);
    return WAKE_RESULT_ERROR_NETWORK;
  }

  // Send wake packet to port 9302 (PS5 discovery/wake port)
  addr.sin_port = htons(9302);

  char wake_packet[256];
  int packet_len =
      build_wake_packet(wake_credential, wake_packet, sizeof(wake_packet));
  if (packet_len < 0) {
    close(sock);
    return WAKE_RESULT_ERROR_INVALID_CREDENTIAL;
  }

  // Include trailing NUL as sent by Vitaki/Chiaki discovery (packet_len+1)
  int bytes_sent = sendto(sock, wake_packet, packet_len + 1, 0,
                          (struct sockaddr *)&addr, sizeof(addr));

  close(sock);

  if (bytes_sent < 0) {
    return WAKE_RESULT_ERROR_NETWORK;
  }

  return WAKE_RESULT_SUCCESS;
}

// Helper: convert 8-hex credential to decimal string (up to 20 chars)
static bool hex8_to_decimal(const char *hex8, char *out, size_t out_cap) {
  if (!hex8 || !out || out_cap < 2) return false;
  // parse as 64-bit from hex
  unsigned long long v = 0;
  for (int i = 0; i < 8; ++i) {
    char c = hex8[i];
    unsigned digit;
    if (c >= '0' && c <= '9')
      digit = (unsigned)(c - '0');
    else if (c >= 'a' && c <= 'f')
      digit = 10u + (unsigned)(c - 'a');
    else if (c >= 'A' && c <= 'F')
      digit = 10u + (unsigned)(c - 'A');
    else
      return false;
    v = (v << 4) | digit;
  }
  // write decimal
  int n = snprintf(out, out_cap, "%llu", v);
  return n > 0 && (size_t)n < out_cap;
}

// Convenience API: wake using 8-hex registration key (PS5)
WakeResult wake_ps5_console_from_hex(const char *console_ip,
                                     const char *regkey_hex8) {
  // Validate input is proper 8-hex before converting to decimal
  if (!regkey_hex8 || strlen(regkey_hex8) != 8)
    return WAKE_RESULT_ERROR_INVALID_CREDENTIAL;
  for (int i = 0; i < 8; ++i) {
    char c = regkey_hex8[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F');
    if (!ok) return WAKE_RESULT_ERROR_INVALID_CREDENTIAL;
  }
  char dec[32];
  if (!hex8_to_decimal(regkey_hex8, dec, sizeof(dec)))
    return WAKE_RESULT_ERROR_INVALID_CREDENTIAL;
  return wake_ps5_console(console_ip, dec);
}
