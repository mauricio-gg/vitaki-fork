// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_INTEGRATION_H
#define CHIAKI_INTEGRATION_H

#include "../core/vitarps5.h"
#include "chiaki_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error code mapping between ChiakiSession and VitaRPS5
 *
 * This module provides bidirectional error code conversion to allow
 * seamless integration between vitaki-fork ChiakiSession implementation
 * and VitaRPS5's error handling system.
 */

// Registration types and constants
typedef enum {
  REGISTRATION_EVENT_PIN_REQUEST,
  REGISTRATION_EVENT_SUCCESS,
  REGISTRATION_EVENT_FAILED,
  REGISTRATION_EVENT_CANCELLED
} VitaRPS5RegistrationEvent;

typedef struct {
  char server_nickname[64];
  uint8_t rp_regist_key[16];
  uint8_t rp_key[16];
  uint8_t server_mac[6];
  uint32_t console_pin;
  bool is_ps5;
} VitaRPS5RegistrationData;

typedef void (*VitaRPS5RegistrationCallback)(
    VitaRPS5RegistrationEvent event, const VitaRPS5RegistrationData* data,
    void* user_data);

// ChiakiSession wrapper types
typedef struct {
  ChiakiSession* chiaki_session;
  char console_ip[64];
  bool is_registered;
  VitaRPS5RegistrationData registration_data;
  ChiakiLog* logger;
} VitaRPS5ChiakiSession;

/**
 * Convert ChiakiErrorCode to VitaRPS5Result
 */
VitaRPS5Result chiaki_to_vitarps5_error(ChiakiErrorCode chiaki_error);

/**
 * Convert VitaRPS5Result to ChiakiErrorCode
 */
ChiakiErrorCode vitarps5_to_chiaki_error(VitaRPS5Result vitarps5_result);

/**
 * Convert ChiakiErrorCode to VitaRPS5Result with context information
 */
VitaRPS5Result chiaki_to_vitarps5_error_with_context(
    ChiakiErrorCode chiaki_error, const char* context_message);

/**
 * Get human-readable error message for ChiakiErrorCode
 */
const char* chiaki_error_string(ChiakiErrorCode error);

/**
 * Wrapper macros for common ChiakiSession operations
 */
#define CHIAKI_CALL(chiaki_func) chiaki_to_vitarps5_error(chiaki_func)
#define CHIAKI_CALL_CTX(chiaki_func, ctx) \
  chiaki_to_vitarps5_error_with_context(chiaki_func, ctx)

// Registration API
VitaRPS5Result chiaki_integration_init(void);
void chiaki_integration_cleanup(void);
VitaRPS5Result chiaki_registration_start(const char* console_ip,
                                         const char* psn_account_id,
                                         uint32_t pin,
                                         VitaRPS5RegistrationCallback callback,
                                         void* user_data);
VitaRPS5Result chiaki_registration_cancel(void);

// Session API
VitaRPS5Result chiaki_session_create_authenticated(
    const char* console_ip, const VitaRPS5RegistrationData* registration_data,
    VitaRPS5ChiakiSession** session);
VitaRPS5Result chiaki_session_start_streaming(VitaRPS5ChiakiSession* session);
VitaRPS5Result chiaki_session_stop_streaming(VitaRPS5ChiakiSession* session);
void chiaki_session_destroy(VitaRPS5ChiakiSession* session);
VitaRPS5Result chiaki_session_send_controller_input(
    VitaRPS5ChiakiSession* session, uint32_t buttons, int16_t left_x,
    int16_t left_y, int16_t right_x, int16_t right_y, uint8_t left_trigger,
    uint8_t right_trigger);

// Registration storage API
VitaRPS5Result chiaki_registration_save(const char* console_id,
                                        const VitaRPS5RegistrationData* data);
VitaRPS5Result chiaki_registration_load(const char* console_id,
                                        VitaRPS5RegistrationData* data);
bool chiaki_registration_exists(const char* console_id);
VitaRPS5Result chiaki_registration_delete(const char* console_id);

#ifdef __cplusplus
}
#endif

#endif  // CHIAKI_INTEGRATION_H