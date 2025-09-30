// session_init.c
#include "session_init.h"

#include <stdio.h>
#include <string.h>

#include "../utils/helpers.h"

static bool make_rp_registkey_header(const char *registkey_hex8, int format,
                                     char *out, size_t out_cap) {
  // format 0: hex, format 1: base64(raw4) - PS5 uses 8 hex chars (4 bytes raw)
  if (!is_all_hex(registkey_hex8, REGKEY_HEX_LEN)) return false;

  if (format == 0) {
    // HEX directly - use defensive formatting to prevent corruption
    // Researcher fix: prevent c->m corruption by validating each char
    char safe_hex[9] = {0};  // ensure null termination
    for (int i = 0; i < 8; i++) {
      char c = registkey_hex8[i];
      if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F')) {
        safe_hex[i] = c;
      } else {
        return false;  // invalid hex char
      }
    }
    safe_hex[8] = '\0';
    int n = snprintf(out, out_cap, "RP-Registkey: %s\r\n", safe_hex);
    return (n > 0 && (size_t)n < out_cap);
  } else {
    // Base64 of RAW 4 bytes
    uint8_t raw4[REGKEY_RAW_LEN];
    if (hex_decode(registkey_hex8, REGKEY_HEX_LEN, raw4, sizeof(raw4)) !=
        REGKEY_RAW_LEN)
      return false;
    char b64[64];
    if (b64_encode(raw4, REGKEY_RAW_LEN, b64, sizeof(b64)) <= 0) return false;
    int n = snprintf(out, out_cap, "RP-Registkey: %s\r\n", b64);
    return (n > 0 && (size_t)n < out_cap);
  }
}

int build_session_init_request(const ConsoleRegistration *r,
                               int rp_registkey_format, char *out_buf,
                               size_t out_cap) {
  if (!r || !out_buf) return -1;
  // Validate inputs
  if (!is_all_hex(r->registkey_hex, REGKEY_HEX_LEN)) return -1;
  if (!r->np_account_b64[0]) return -1;

  // Headers
  char regkey_hdr[128];
  if (!make_rp_registkey_header(r->registkey_hex, rp_registkey_format,
                                regkey_hdr, sizeof(regkey_hdr)))
    return -1;

  // Build HTTP request; PS5 requires POST not GET for session init
  // Researcher fix: GET was causing 403 Forbidden errors
  int n = snprintf(out_buf, out_cap,
                   "POST /sie/ps5/rp/sess/init HTTP/1.1\r\n"
                   "Host: ps5.local\r\n"
                   "User-Agent: VitaRPS5/1.0\r\n"
                   "%s"
                   "Np-AccountId: %s\r\n"
                   "RP-Version: 1.0\r\n"
                   "Client-Type: vitaki\r\n"
                   "Content-Length: 0\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   regkey_hdr, r->np_account_b64);

  return (n > 0 && (size_t)n < out_cap) ? n : -1;
}

bool session_init_with_fallback(const ConsoleRegistration *r,
                                int (*send_http)(const char *req,
                                                 size_t req_len,
                                                 SessionInitResponse *resp),
                                SessionInitResponse *out_resp) {
  char req[1024];
  SessionInitResponse resp = {0};

#if RP_REGISTKEY_PRIMARY_IS_HEX
  int pri = 0, sec = 1;
#else
  int pri = 1, sec = 0;
#endif

  int n = build_session_init_request(r, pri, req, sizeof(req));
  if (n < 0) return false;
  if (send_http(req, (size_t)n, &resp) != 0) return false;
  if (resp.http_status == 200) {
    if (out_resp) *out_resp = resp;
    return true;
  }
  if (resp.http_status != 403) {
    if (out_resp) *out_resp = resp;
    return false;
  }

  // One-shot fallback
  n = build_session_init_request(r, sec, req, sizeof(req));
  if (n < 0) return false;
  if (send_http(req, (size_t)n, &resp) != 0) return false;
  if (out_resp) *out_resp = resp;
  return resp.http_status == 200;
}