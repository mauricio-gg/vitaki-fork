// console_registration.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "../utils/helpers.h"
#include "vitarps5.h"

// Compatibility constants for transition period
#define CONSOLE_TARGET_PS5_UNKNOWN 1000000
#define CONSOLE_TARGET_PS5_1 1000100

typedef struct {
  char registkey_hex[REGKEY_HEX_LEN + 1];  // PS5 uses 8 lowercase hex chars
  uint8_t morning[MORNING_LEN];            // 16 bytes
  uint8_t np_account_le8[NP_ACCT_LEN];     // 8 bytes LE
  char np_account_b64[16];                 // 12 chars + '=' + '\0' fits
  char wake_credential_dec[MAX_WAKE_DEC];  // signed decimal, null-terminated

  // Compatibility fields for transition period
  bool is_registered;         // Always false in clean slate
  char registration_key[64];  // Legacy hex string (deprecated)
  uint8_t rp_key[16];         // Legacy binary key (deprecated)
  uint32_t rp_key_type;       // Legacy key type (deprecated)
  char console_name[64];      // Console display name
  char ip_address[16];        // Console IP address for lookup
  uint32_t target;            // Console target type (deprecated)

  // RESEARCHER FIX 1: Validity flag to distinguish corrupted from valid
  // registrations
  bool is_valid;  // True if registration is valid and usable
} ConsoleRegistration;

// Migration: normalize any legacy storage to clean registkey_hex (8 hex chars)
bool migrate_regkey_to_clean_hex(ConsoleRegistration* r, const char* stored,
                                 size_t stored_len);

// Stub functions for compatibility during transition
bool console_registration_is_registered(const char* console_ip);
bool console_registration_find_by_ip(const char* console_ip,
                                     ConsoleRegistration* console);
uint32_t console_registration_get_count(void);
bool console_registration_is_incomplete(const char* console_ip);
VitaRPS5Result console_registration_repair_incomplete(const char* console_ip);
VitaRPS5Result console_registration_remove(const char* console_ip);
VitaRPS5Result console_registration_detect_and_cleanup_corruption(
    const char* console_ip);
bool console_registration_supports_session_init(const char* console_ip);

// Registration invariant check (hex8 present, morning16 present)
typedef struct {
  bool has_hex8;
  bool has_morning16;
  bool has_psn_u64;  // not used currently
} RegInvariants;

bool console_registration_check_invariants(const ConsoleRegistration* reg,
                                           RegInvariants* out);
VitaRPS5Result console_registration_init(void);
bool console_registration_is_initialized(void);
void console_registration_cleanup(void);
VitaRPS5Result validate_registration_credentials(const char* console_ip);
VitaRPS5Result console_registration_add(const char* console_ip,
                                        const char* console_name,
                                        const char* registration_key);
VitaRPS5Result console_registration_add_complete(
    const char* console_ip, const ConsoleRegistration* complete_data);
VitaRPS5Result console_registration_get_session_credentials(
    const char* console_ip, uint8_t* rp_regist_key, uint8_t* morning_key);

// RESEARCHER FIX B: Unified registration credential accessor
// Single authoritative source for all registration data - no more split-brain!
typedef struct {
  char regkey_hex8[9];           // 8-char hex registration key
  char wake_credential_dec[16];  // Wake credential (decimal)
  char console_name[64];         // Console display name
  bool is_valid;                 // True if credentials are valid and usable
} RegistrationCredentials;

bool registration_get_by_ip(const char* console_ip,
                            RegistrationCredentials* creds);

VitaRPS5Result console_registration_wake_console(const char* console_ip);
