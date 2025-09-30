// vitaki_bridge.c
#include "vitaki_bridge.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../chiaki/chiaki_session.h"
#include "../discovery/ps5_discovery.h"
#include "../network/wake.h"
#include "../ui/ui_core.h"
#include "../utils/helpers.h"

static bool is_8digit_pin(const char* s) {
  if (!s) return false;
  if (strlen(s) != 8) return false;
  for (int i = 0; i < 8; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

// Bridge Chiaki log to VitaRPS5 logger (file-scope)
static void vitaki_bridge_chiaki_log(ChiakiLogLevel level, const char* msg,
                                     void* user) {
  (void)user;
  switch (level) {
    case CHIAKI_LOG_LEVEL_VERBOSE:
    case CHIAKI_LOG_LEVEL_DEBUG:
      log_debug("[CHIAKI] %s", msg);
      break;
    case CHIAKI_LOG_LEVEL_INFO:
      log_info("[CHIAKI] %s", msg);
      break;
    case CHIAKI_LOG_LEVEL_WARNING:
      log_warn("[CHIAKI] %s", msg);
      break;
    case CHIAKI_LOG_LEVEL_ERROR:
    default:
      log_error("[CHIAKI] %s", msg);
      break;
  }
}

static ChiakiLog g_bridge_log = {
    .mask = CHIAKI_LOG_ALL, .cb = vitaki_bridge_chiaki_log, .user = NULL};

VitaRPS5Result vitaki_register_ps5_start(const char* host, const char* pin8,
                                         const char* psn_account_b64,
                                         VitaRPS5RegistrationCallback cb,
                                         void* user_data) {
#if !VITAKI_BRIDGE_ENABLED
  (void)host;
  (void)pin8;
  (void)psn_account_b64;
  (void)cb;
  (void)user_data;
  return VITARPS5_ERROR_INCOMPATIBLE;
#else
  if (!host || !*host || !psn_account_b64 || !*psn_account_b64 || !cb) {
    log_error("[VITAKI-REG] Invalid parameters (host/psn/cb)");
    return VITARPS5_ERROR_INVALID_PARAM;
  }
  if (!is_8digit_pin(pin8)) {
    log_error("[VITAKI-REG] Invalid PIN; expected 8 digits");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Convert PIN string to integer as expected by chiaki_registration_start
  // PIN is 8 decimal digits, fits in 32-bit unsigned
  uint32_t pin_val = 0;
  for (int i = 0; i < 8; ++i) {
    pin_val = pin_val * 10u + (uint32_t)(pin8[i] - '0');
  }

  VitaRPS5Result init_res = chiaki_integration_init();
  if (init_res != VITARPS5_SUCCESS) {
    log_error("[VITAKI-REG] Failed to init chiaki integration: %s",
              vitarps5_result_string(init_res));
    return init_res;
  }

  log_info("[VITAKI-REG] Starting PS5 registration for %s with PIN %s", host,
           pin8);
  return chiaki_registration_start(host, psn_account_b64, pin_val, cb,
                                   user_data);
#endif
}

VitaRPS5Result vitaki_wake_ps5(const char* host,
                               const ConsoleRegistration* reg) {
#if !VITAKI_BRIDGE_ENABLED
  (void)host;
  (void)reg;
  return VITARPS5_ERROR_INCOMPATIBLE;
#else
  if (!host || !*host || !reg) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Prefer explicit 8-hex registration key for wake credential
  const char* cred_hex = reg->registkey_hex;
  if (!cred_hex || strlen(cred_hex) != REGKEY_HEX_LEN ||
      !is_all_hex(cred_hex, REGKEY_HEX_LEN)) {
    log_error("[VITAKI-WAKE] Invalid registkey_hex; cannot wake %s", host);
    return VITARPS5_ERROR_INVALID_CREDENTIALS;
  }

  // Use Vitaki-compatible wake: convert hex8 -> decimal and send to 9302
  WakeResult wr = wake_ps5_console_from_hex(host, cred_hex);
  switch (wr) {
    case WAKE_RESULT_SUCCESS:
      log_info("[VITAKI-WAKE] Wake packet sent to %s", host);
      return VITARPS5_SUCCESS;
    case WAKE_RESULT_ERROR_INVALID_CREDENTIAL:
      log_error("[VITAKI-WAKE] Invalid wake credential for %s", host);
      return VITARPS5_ERROR_INVALID_CREDENTIALS;
    case WAKE_RESULT_ERROR_TIMEOUT:
      log_error("[VITAKI-WAKE] Wake timeout for %s", host);
      return VITARPS5_ERROR_TIMEOUT;
    default:
      log_error("[VITAKI-WAKE] Wake network error for %s", host);
      return VITARPS5_ERROR_NETWORK;
  }
#endif
}

VitaRPS5Result vitaki_connect(const SessionConfig* cfg,
                              ChiakiSession** out_session) {
#if !VITAKI_BRIDGE_ENABLED
  (void)cfg;
  (void)out_session;
  return VITARPS5_ERROR_INCOMPATIBLE;
#else
  if (!cfg || !out_session) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }
  if (!cfg->console_ip || strlen(cfg->console_ip) == 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Retrieve credentials from registration store
  uint8_t rp_regist_key[16] = {0};
  uint8_t morning_key[16] = {0};
  VitaRPS5Result cred_res = console_registration_get_session_credentials(
      cfg->console_ip, rp_regist_key, morning_key);
  if (cred_res != VITARPS5_SUCCESS) {
    log_error("[VITAKI-CONNECT] Failed to get credentials: %s",
              vitarps5_result_string(cred_res));
    return cred_res;
  }

  ChiakiConnectInfo info = {0};
  info.ps5 = (cfg->console_version >= 12);
  info.host = cfg->console_ip;
  // Vitaki parity: use ASCII 8-hex in regist_key (first 8 bytes), zeros after.
  memset(info.regist_key, 0, sizeof(info.regist_key));
  ConsoleRegistration console_reg = {0};
  if (console_registration_find_by_ip(cfg->console_ip, &console_reg) &&
      console_reg.registkey_hex[0] != '\0') {
    for (int i = 0; i < 8; ++i)
      info.regist_key[i] = console_reg.registkey_hex[i];
  } else {
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < 4; ++i) {
      uint8_t b = rp_regist_key[i];
      info.regist_key[i * 2] = H[(b >> 4) & 0xF];
      info.regist_key[i * 2 + 1] = H[b & 0xF];
    }
  }
  memcpy(info.morning, morning_key, sizeof(info.morning));
  info.video_profile.width = cfg->video_width;
  info.video_profile.height = cfg->video_height;
  info.video_profile.max_fps = cfg->target_fps;
  info.video_profile.bitrate = cfg->max_bitrate * 1000;  // kbps
  info.video_profile.codec = CHIAKI_CODEC_H264;

  // Control port: use configured or default 9295 (Vitaki baseline)
  info.control_port = (cfg->control_port > 0) ? (int)cfg->control_port : 9295;
  info.host_header_port_override = -1;

  // Use file-scope logger

  ChiakiSession* session = (ChiakiSession*)malloc(sizeof(ChiakiSession));
  if (!session) {
    return VITARPS5_ERROR_MEMORY;
  }
  memset(session, 0, sizeof(ChiakiSession));

  VitaRPS5Result init_res =
      CHIAKI_CALL_CTX(chiaki_session_init(session, &info, &g_bridge_log),
                      "vitaki_connect:init");
  if (init_res != VITARPS5_SUCCESS) {
    free(session);
    return init_res;
  }
  // Do not start here; caller will set callbacks, prepare decoders, then start
  *out_session = session;
  return VITARPS5_SUCCESS;
#endif
}
