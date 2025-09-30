// vitaki_bridge.h
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../chiaki/chiaki_integration.h"
#include "../core/console_registration.h"
#include "../core/session_manager.h"
#include "../utils/logger.h"

#ifdef __cplusplus
extern "C" {
#endif

// Feature flag: enable Vitaki bridge paths by default for PS5
#ifndef VITAKI_BRIDGE_ENABLED
#define VITAKI_BRIDGE_ENABLED 1
#endif

// Registration (async) — thin wrapper over Vitaki (Chiaki) implementation
// Validates inputs, converts PIN, and starts registration with callback.
VitaRPS5Result vitaki_register_ps5_start(const char* host, const char* pin8,
                                         const char* psn_account_b64,
                                         VitaRPS5RegistrationCallback cb,
                                         void* user_data);

// Wake — uses the stored 8-hex registration key as wake credential.
// Converts to decimal if needed and sends wake packet.
VitaRPS5Result vitaki_wake_ps5(const char* host,
                               const ConsoleRegistration* reg);

// Connect (init + start) — builds ChiakiConnectInfo from our SessionConfig
// and stored credentials and starts the Chiaki session. This is provided for
// integration parity with Vitaki. Current PS5 flow may defer session creation;
// use only in legacy/explicit connect paths.
VitaRPS5Result vitaki_connect(const SessionConfig* cfg,
                              ChiakiSession** out_session);

#ifdef __cplusplus
}
#endif
