// helpers.h
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REGKEY_HEX_LEN 8  // PS5 uses 8 hex chars (4 bytes)
#define REGKEY_RAW_LEN 4  // 4 bytes raw for 8 hex chars
#define MORNING_LEN 16
#define NP_ACCT_LEN 8    // 64-bit LE
#define MAX_WAKE_DEC 24  // signed decimal text, generous

bool is_all_hex(const char *s, size_t n);
int hex_decode(const char *hex, size_t hex_len, uint8_t *out,
               size_t out_cap);  // returns bytes or -1
int hex_encode(const uint8_t *in, size_t in_len, char *out_hex,
               size_t out_cap);  // returns chars or -1
int b64_encode(const uint8_t *in, size_t in_len, char *out,
               size_t out_cap);  // returns chars or -1

// Little-endian 64-bit -> store to 8 bytes
static inline void u64_to_le8(uint64_t v, uint8_t out[8]) {
  for (int i = 0; i < 8; ++i) out[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}