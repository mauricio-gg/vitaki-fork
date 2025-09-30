// wake.h
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool is_valid_wake_credential(const char *s);
int build_wake_packet(const char *wake_credential, char *out,
                      size_t cap);  // returns bytes

// PS5-specific wake-up implementation using correct port protocol
typedef enum {
  WAKE_RESULT_SUCCESS = 0,
  WAKE_RESULT_ERROR_NETWORK = 1,
  WAKE_RESULT_ERROR_INVALID_CREDENTIAL = 2,
  WAKE_RESULT_ERROR_TIMEOUT = 3
} WakeResult;

WakeResult wake_ps5_console(const char *console_ip,
                            const char *wake_credential);
// Convenience: wake using 8-hex RegistKey (converts to decimal credential)
WakeResult wake_ps5_console_from_hex(const char *console_ip,
                                     const char *regkey_hex8);
