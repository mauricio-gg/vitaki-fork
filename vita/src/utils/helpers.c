// helpers.c
#include "helpers.h"

#include <ctype.h>
#include <string.h>

bool is_all_hex(const char *s, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (!isxdigit((unsigned char)s[i])) return false;
  }
  return true;
}

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = (char)tolower((unsigned char)c);
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  return -1;
}

int hex_decode(const char *hex, size_t hex_len, uint8_t *out, size_t out_cap) {
  if (hex_len % 2 != 0) return -1;
  size_t need = hex_len / 2;
  if (out_cap < need) return -1;
  for (size_t i = 0; i < need; ++i) {
    int hi = hex_val(hex[2 * i]);
    int lo = hex_val(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return -1;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return (int)need;
}

int hex_encode(const uint8_t *in, size_t in_len, char *out_hex,
               size_t out_cap) {
  static const char *H = "0123456789abcdef";
  size_t need = in_len * 2;
  if (out_cap < need + 1) return -1;
  for (size_t i = 0; i < in_len; ++i) {
    out_hex[2 * i] = H[(in[i] >> 4) & 0xF];
    out_hex[2 * i + 1] = H[in[i] & 0xF];
  }
  out_hex[need] = '\0';
  return (int)need;
}

// minimal base64 (RFC 4648, no line breaks)
int b64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap) {
  static const char *B64 =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t olen = 4 * ((in_len + 2) / 3);
  if (out_cap < olen + 1) return -1;
  size_t i = 0, o = 0;
  while (i + 3 <= in_len) {
    uint32_t v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
    out[o++] = B64[(v >> 18) & 63];
    out[o++] = B64[(v >> 12) & 63];
    out[o++] = B64[(v >> 6) & 63];
    out[o++] = B64[v & 63];
    i += 3;
  }
  if (i < in_len) {
    uint32_t v = in[i] << 16;
    int rem = (int)(in_len - i);
    if (rem == 2) v |= (in[i + 1] << 8);
    out[o++] = B64[(v >> 18) & 63];
    out[o++] = B64[(v >> 12) & 63];
    out[o++] = (rem == 2) ? B64[(v >> 6) & 63] : '=';
    out[o++] = '=';
  }
  out[o] = '\0';
  return (int)o;
}