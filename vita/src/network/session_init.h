// session_init.h
#pragma once
#include <stdbool.h>

#include "../core/console_registration.h"

typedef struct {
  // filled by your HTTP layer:
  int http_status;
  // (parse any JSON/fields you need beyond this)
} SessionInitResponse;

#define RP_REGISTKEY_PRIMARY_IS_HEX 1  // set to 0 to make Base64 primary

// Build request into out_buf. Returns bytes or -1.
// rp_registkey_format: 0 = hex, 1 = base64(raw16)
int build_session_init_request(const ConsoleRegistration *r,
                               int rp_registkey_format, char *out_buf,
                               size_t out_cap);

// High-level call that tries primary, falls back to secondary after 403.
// send_http is your function that POSTs/GETs to /sie/ps5/rp/sess/init and fills
// resp.http_status.
bool session_init_with_fallback(const ConsoleRegistration *r,
                                int (*send_http)(const char *req,
                                                 size_t req_len,
                                                 SessionInitResponse *resp),
                                SessionInitResponse *out_resp);