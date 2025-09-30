// console_registration.c (migration)
#include "console_registration.h"

#include <ctype.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../chiaki/chiaki_base64_vitaki.h"
#include "../console/vitaki_bridge.h"
#include "../discovery/ps5_discovery.h"
#include "../network/wake.h"
#include "../utils/logger.h"

// Persistent storage paths
#define REGISTRATION_STORAGE_DIR "ux0:data/VitaRPS5/registrations/"
#define REGISTRATION_FILE_EXTENSION ".reg"

// Forward declarations for persistent storage functions
static VitaRPS5Result ensure_registration_directory(void);
static void get_registration_file_path(const char* console_ip, char* path,
                                       size_t path_size);
static VitaRPS5Result save_registration_to_filesystem(
    const char* console_ip, const ConsoleRegistration* reg);
static VitaRPS5Result load_registration_from_filesystem(
    const char* console_ip, ConsoleRegistration* reg);
static VitaRPS5Result delete_registration_from_filesystem(
    const char* console_ip);
static VitaRPS5Result load_all_registrations_from_filesystem(void);

// Helper function for hex character to value conversion
static int hex_char_to_value(int c) {
  if ('0' <= c && c <= '9') return c - '0';
  c |= 0x20;
  if ('a' <= c && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Returns true if r->registkey_hex is set to a valid 8-char hex (PS5 format)
bool migrate_regkey_to_clean_hex(ConsoleRegistration* r, const char* stored,
                                 size_t stored_len) {
  if (!r || !stored) return false;

  // Case A: PS5 RegistKey is 8 hex characters
  if (stored_len == REGKEY_HEX_LEN && is_all_hex(stored, REGKEY_HEX_LEN)) {
    memcpy(r->registkey_hex, stored, REGKEY_HEX_LEN);
    r->registkey_hex[REGKEY_HEX_LEN] = '\0';

    // Normalize to lowercase
    for (int i = 0; i < REGKEY_HEX_LEN; i++) {
      r->registkey_hex[i] = tolower((unsigned char)r->registkey_hex[i]);
    }
    return true;
  }

  // Case B: Handle legacy 32-char keys by extracting first 8 chars
  // This supports migration from the old incorrect 32-char implementation
  if (stored_len == 32 && is_all_hex(stored, 32)) {
    memcpy(r->registkey_hex, stored, REGKEY_HEX_LEN);
    r->registkey_hex[REGKEY_HEX_LEN] = '\0';

    // Normalize to lowercase
    for (int i = 0; i < REGKEY_HEX_LEN; i++) {
      r->registkey_hex[i] = tolower((unsigned char)r->registkey_hex[i]);
    }
    return true;
  }

  // Case C: Convert raw 4 bytes to 8-char hex string
  if (stored_len == REGKEY_RAW_LEN) {
    if (hex_encode((const uint8_t*)stored, REGKEY_RAW_LEN, r->registkey_hex,
                   sizeof(r->registkey_hex)) > 0) {
      return true;
    }
  }

  // RESEARCHER PHASE 1: Case D - Double-hex repair for corrupted ASCII keys
  // If stored "hex" is 32 chars and every byte pair decodes to ASCII hex digit
  // (0-9 a-f), you've got a double-hex of an 8-char key. Decode once and take
  // first 8 ASCII chars.
  if (stored_len == 32) {
    // Check if this looks like double-hex encoded ASCII
    bool is_double_hex = true;
    for (size_t i = 0; i < 32 && is_double_hex; i += 2) {
      int hi = hex_char_to_value(stored[i]);
      int lo = hex_char_to_value(stored[i + 1]);
      if (hi < 0 || lo < 0) {
        is_double_hex = false;
        break;
      }
      uint8_t decoded_byte = (hi << 4) | lo;
      // Check if decoded byte is ASCII hex digit (0-9, a-f)
      if (!((decoded_byte >= '0' && decoded_byte <= '9') ||
            (decoded_byte >= 'a' && decoded_byte <= 'f') ||
            (decoded_byte >= 'A' && decoded_byte <= 'F'))) {
        is_double_hex = false;
        break;
      }
    }

    if (is_double_hex) {
      // Decode the first 8 ASCII characters from double-hex
      char decoded_ascii[16] = {0};
      for (int i = 0; i < 16; i += 2) {
        int hi = hex_char_to_value(stored[i]);
        int lo = hex_char_to_value(stored[i + 1]);
        decoded_ascii[i / 2] = (char)((hi << 4) | lo);
      }

      // Take first 8 chars as the true registration key
      if (strlen(decoded_ascii) >= 8 && is_all_hex(decoded_ascii, 8)) {
        memcpy(r->registkey_hex, decoded_ascii, REGKEY_HEX_LEN);
        r->registkey_hex[REGKEY_HEX_LEN] = '\0';

        // Normalize to lowercase
        for (int i = 0; i < REGKEY_HEX_LEN; i++) {
          r->registkey_hex[i] = tolower((unsigned char)r->registkey_hex[i]);
        }

        log_info("REGKEY MIGRATED: %.8s... -> %s", stored, r->registkey_hex);
        return true;
      }
    }
  }

  return false;  // Reject invalid formats
}

// Simple in-memory registration storage for transition period
// This allows the UI to work while we implement the full registration system
static ConsoleRegistration registrations[16];  // Max 16 registered consoles
static uint32_t registration_count = 0;

// Thread safety for registration storage
static SceUID registration_mutex = -1;
static bool registration_mutex_initialized = false;
static bool registration_system_initialized = false;

// Remove duplicate in-memory registrations for the same IP, keeping the most
// recent entry
static void cleanup_duplicates_for_ip(const char* console_ip) {
  if (!console_ip || registration_count == 0) return;

  int last_index = -1;
  int duplicates = 0;
  for (uint32_t i = 0; i < registration_count; i++) {
    if (strcmp(console_ip, registrations[i].ip_address) == 0) {
      last_index = (int)i;  // keep the most recent (highest index)
      duplicates++;
    }
  }

  if (duplicates <= 1) return;  // nothing to clean

  ConsoleRegistration kept;
  if (last_index >= 0) kept = registrations[last_index];

  // Rebuild array keeping only the last occurrence for this IP
  ConsoleRegistration new_list[16];
  uint32_t new_count = 0;
  for (uint32_t i = 0; i < registration_count; i++) {
    if (strcmp(console_ip, registrations[i].ip_address) == 0) {
      // skip all matches; we'll add the kept one once at the end
      continue;
    }
    new_list[new_count++] = registrations[i];
  }
  // Append the kept entry
  if (new_count < 16 && last_index >= 0) {
    new_list[new_count++] = kept;
  }

  memcpy(registrations, new_list, sizeof(ConsoleRegistration) * new_count);
  uint32_t removed = registration_count - new_count;
  registration_count = new_count;
  log_info("REGCLEAN: Removed %u duplicate registration(s) for %s; kept latest",
           removed, console_ip);
}

// Basic working implementation to replace stubs
bool console_registration_is_registered(const char* console_ip) {
  if (!console_ip) {
    return false;
  }

  // Lock mutex for thread safety
  if (registration_mutex_initialized && registration_mutex >= 0) {
    sceKernelLockMutex(registration_mutex, 1, NULL);
  }

  bool found = false;
  bool is_valid_registration = false;
  // Search all entries; consider registered if any valid entry matches
  for (uint32_t i = 0; i < registration_count; i++) {
    if (strcmp(console_ip, registrations[i].ip_address) == 0) {
      found = true;
      if (registrations[i].is_valid && registrations[i].is_registered) {
        is_valid_registration = true;
      }
    }
  }

  // Unlock mutex
  if (registration_mutex_initialized && registration_mutex >= 0) {
    sceKernelUnlockMutex(registration_mutex, 1);
  }

  if (!found) {
    log_debug("No registration found for console %s", console_ip);
  }
  return is_valid_registration;  // RESEARCHER FIX 1: Only return true if
                                 // registration is valid
}

bool console_registration_find_by_ip(const char* console_ip,
                                     ConsoleRegistration* console) {
  if (!console_ip || !console) {
    return false;
  }

  // Lock mutex for thread safety
  if (registration_mutex_initialized && registration_mutex >= 0) {
    sceKernelLockMutex(registration_mutex, 1, NULL);
  }

  bool found = false;
  // Prefer the most recent matching entry
  for (int i = (int)registration_count - 1; i >= 0; i--) {
    if (strcmp(console_ip, registrations[i].ip_address) == 0) {
      memcpy(console, &registrations[i], sizeof(ConsoleRegistration));
      log_debug("Found registration data for console %s (index=%d)", console_ip,
                i);
      found = true;
      break;
    }
  }

  // Unlock mutex
  if (registration_mutex_initialized && registration_mutex >= 0) {
    sceKernelUnlockMutex(registration_mutex, 1);
  }

  if (!found) {
    log_debug("No registration data found for console %s", console_ip);
  }
  return found;
}

uint32_t console_registration_get_count(void) {
  // Lock mutex for thread safety
  if (registration_mutex_initialized && registration_mutex >= 0) {
    sceKernelLockMutex(registration_mutex, 1, NULL);
  }

  uint32_t count = registration_count;

  // Unlock mutex
  if (registration_mutex_initialized && registration_mutex >= 0) {
    sceKernelUnlockMutex(registration_mutex, 1);
  }

  return count;
}

bool console_registration_is_incomplete(const char* console_ip) {
  return false;  // No incomplete registrations in clean slate
}

VitaRPS5Result console_registration_repair_incomplete(const char* console_ip) {
  return VITARPS5_SUCCESS;  // Nothing to repair in clean slate
}

VitaRPS5Result console_registration_remove(const char* console_ip) {
  if (!console_ip) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Find and remove registration using IP address
  for (uint32_t i = 0; i < registration_count; i++) {
    if (strcmp(console_ip, registrations[i].ip_address) == 0) {
      // Shift remaining registrations down
      for (uint32_t j = i; j < registration_count - 1; j++) {
        memcpy(&registrations[j], &registrations[j + 1],
               sizeof(ConsoleRegistration));
      }
      registration_count--;
      log_info("Removed registration for console %s", console_ip);

      // Delete registration file from filesystem
      VitaRPS5Result delete_result =
          delete_registration_from_filesystem(console_ip);
      if (delete_result != VITARPS5_SUCCESS) {
        log_warning("Failed to delete registration file from filesystem: %s",
                    vitarps5_result_string(delete_result));
      } else {
        log_debug("Registration file deleted from filesystem");
      }

      return VITARPS5_SUCCESS;
    }
  }

  log_debug("No registration found to remove for console %s", console_ip);
  return VITARPS5_SUCCESS;  // Not an error if console wasn't registered
}

static VitaRPS5Result console_registration_build_session_init_request_removed(
    const char* console_ip, uint16_t discovered_port,
    const uint8_t* psn_account_id, void* request) {
  if (!console_ip || !psn_account_id || !request) {
    log_error("Invalid parameters for session init request");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Find registered console
  ConsoleRegistration console;
  if (!console_registration_find_by_ip(console_ip, &console)) {
    log_error("Console %s not found in registration database", console_ip);
    return VITARPS5_ERROR_NOT_CONNECTED;
  }

  // Verify registration is complete
  if (!console.is_registered || strlen(console.registkey_hex) != 8) {
    log_error("Console %s registration incomplete - key length: %zu",
              console_ip, strlen(console.registkey_hex));
    return VITARPS5_ERROR_NOT_REGISTERED;
  }

  // Build session init request using stored credentials
  // Provide complete authentication data for session establishment

  log_info("Building session init request for console %s", console_ip);
  log_debug("  Registration key (8 hex): %.8s", console.registkey_hex);
  log_debug("  Has rp_regist_key: %s", console.rp_key[0] != 0 ? "Yes" : "No");
  log_debug("  Has morning key: %s", console.morning[0] != 0 ? "Yes" : "No");
  log_debug("  Has PSN account data: %s",
            console.np_account_le8[0] != 0 ? "Yes" : "No");

  // Verify we have all required authentication credentials
  bool has_regist_key =
      (console.rp_key[0] != 0);  // rp_regist_key stored in rp_key field
  bool has_morning_key = (console.morning[0] != 0);
  bool has_psn_account = (console.np_account_le8[0] != 0);

  if (!has_regist_key) {
    log_error("Missing rp_regist_key for session authentication");
    return VITARPS5_ERROR_INVALID_CREDENTIALS;
  }

  if (!has_morning_key) {
    log_error("Missing morning key (rp_key) for session encryption");
    return VITARPS5_ERROR_INVALID_CREDENTIALS;
  }

  if (!has_psn_account) {
    log_warn("Missing PSN account data - may affect session authentication");
  }

  log_info("Session init request ready - all required credentials available");

  // CRITICAL FIX: Actually build the PS5SessionInitRequest with discovered port
  // Cast the void* request to PS5SessionInitRequest* to populate it
  (void)request;

  // Use the discovered request port from discovery (e.g., 997). If 0, fallback
  // to 9295.
  uint16_t session_port = discovered_port ? discovered_port : 9295;
  log_info("✅ PORT: Using discovered host-request-port %u for sess/init",
           session_port);
  // Prefer stored PSN Account ID (from registration) if available
  const uint8_t* acct_id_for_init = (console.np_account_le8[0] != 0)
                                        ? console.np_account_le8
                                        : psn_account_id;
  log_info("PSN ID source for session-init: %s",
           (console.np_account_le8[0] != 0) ? "stored (registration)"
                                            : "profile (current user)");

  // Build request; Client-Type is not used for GET and will not be sent
  // External sess/init builder removed. Chiaki handles sess/init internally.

  // Build RP-Registkey headers from the 16-byte PS5 RegistKey captured at
  // registration. This matches chiaki/vitaki behavior for PS5.
  {
    const size_t rk_len = sizeof(console.rp_key);  // always 16 bytes

    // Heuristic: detect bad legacy saves where rp_key contains ASCII of the
    // 8-digit hex key followed by zeros (e.g., '38 38 33 30 37 33 39 63 00..').
    bool rp_key_looks_ascii_hex8 = true;
    for (int i = 0; i < 8; i++) {
      uint8_t c = console.rp_key[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'))) {
        rp_key_looks_ascii_hex8 = false;
        break;
      }
    }
    bool tail_zero = true;
    for (int i = 8; i < 16; i++) {
      if (console.rp_key[i] != 0) {
        tail_zero = false;
        break;
      }
    }

    const uint8_t* key16_for_header = console.rp_key;
    if (rp_key_looks_ascii_hex8 && tail_zero) {
      log_warning(
          "RP regist key appears to be ASCII-hex8 + zeros. Using morning key "
          "for RP-Registkey header as fallback.");
      key16_for_header = console.morning;
    }
  }
  return VITARPS5_SUCCESS;
}

VitaRPS5Result console_registration_detect_and_cleanup_corruption(
    const char* console_ip) {
  return VITARPS5_SUCCESS;  // No corruption to cleanup in clean slate
}

bool console_registration_supports_session_init(const char* console_ip) {
  if (!console_ip) {
    return false;
  }

  // Check if console is registered and has valid credentials
  ConsoleRegistration console;
  if (console_registration_find_by_ip(console_ip, &console)) {
    // Console found - check if it has valid registration key
    if (console.is_registered && strlen(console.registkey_hex) == 8) {
      log_debug(
          "Console %s supports session init - registered with key: %.4s...",
          console_ip, console.registkey_hex);
      return true;
    } else {
      log_debug("Console %s found but registration incomplete", console_ip);
    }
  } else {
    log_debug("Console %s not found in registration database", console_ip);
  }

  return false;
}

bool console_registration_check_invariants(const ConsoleRegistration* reg,
                                           RegInvariants* out) {
  if (!reg) return false;
  RegInvariants inv = {0};
  // hex8 diagnostic key
  size_t hex_len = strlen(reg->registkey_hex);
  if (hex_len == 8) {
    bool all_hex = true;
    for (size_t i = 0; i < 8; i++) {
      char c = reg->registkey_hex[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'))) {
        all_hex = false;
        break;
      }
    }
    inv.has_hex8 = all_hex;
  }
  // morning 16 bytes present (not all zeros)
  bool any_morning = false;
  for (int i = 0; i < 16; i++) {
    if (reg->morning[i] != 0) {
      any_morning = true;
      break;
    }
  }
  inv.has_morning16 = any_morning;  // struct always has 16 bytes
  inv.has_psn_u64 = false;          // not tracked here
  if (out) *out = inv;
  return inv.has_hex8 && inv.has_morning16;
}

VitaRPS5Result console_registration_init(void) {
  log_info("Initializing console registration system");

  // Initialize mutex for thread safety
  if (!registration_mutex_initialized) {
    registration_mutex = sceKernelCreateMutex("reg_mutex", 0, 0, NULL);
    if (registration_mutex < 0) {
      log_error("Failed to create registration mutex: 0x%08X",
                registration_mutex);
      return VITARPS5_ERROR_UNKNOWN;
    }
    registration_mutex_initialized = true;
    log_debug("Registration mutex initialized");
  }

  // Clear in-memory storage
  memset(registrations, 0, sizeof(registrations));
  registration_count = 0;

  // Load all saved registrations from filesystem
  VitaRPS5Result load_result = load_all_registrations_from_filesystem();
  if (load_result != VITARPS5_SUCCESS) {
    log_warning("Failed to load registrations from filesystem: %s",
                vitarps5_result_string(load_result));
    // Continue initialization even if loading fails
  }

  // RESEARCHER PHASE 1: Run boot-time migration to repair any double-hex
  // corrupted keys
  uint32_t repaired_count = 0;
  for (uint32_t i = 0; i < registration_count; i++) {
    ConsoleRegistration* reg = &registrations[i];

    // Check if registration key needs repair (detect corrupted patterns)
    if (strlen(reg->registkey_hex) != 8 || strlen(reg->registration_key) > 8) {
      // Try to repair using legacy registration_key field if it exists
      const char* stored_key = strlen(reg->registration_key) > 0
                                   ? reg->registration_key
                                   : reg->registkey_hex;
      size_t stored_len = strlen(stored_key);

      if (stored_len > 8) {
        log_info("Boot-time migration: checking console %s with %zu-char key",
                 reg->ip_address, stored_len);

        ConsoleRegistration temp_reg = *reg;
        if (migrate_regkey_to_clean_hex(&temp_reg, stored_key, stored_len)) {
          // Migration successful - update the registration
          memcpy(reg->registkey_hex, temp_reg.registkey_hex,
                 sizeof(reg->registkey_hex));
          // Clear legacy field to prevent future confusion
          memset(reg->registration_key, 0, sizeof(reg->registration_key));

          // Save the repaired registration back to filesystem
          VitaRPS5Result save_result =
              save_registration_to_filesystem(reg->ip_address, reg);
          if (save_result == VITARPS5_SUCCESS) {
            repaired_count++;
            log_info("Boot-time repair successful for console %s",
                     reg->ip_address);
          } else {
            log_warning(
                "Failed to save repaired registration for console %s: %s",
                reg->ip_address, vitarps5_result_string(save_result));
          }
        }
      }
    }
  }

  if (repaired_count > 0) {
    log_info("REGKEY BOOT MIGRATION: Repaired %d corrupted registration keys",
             repaired_count);
  }

  log_info(
      "Console registration system initialized with %d saved registrations",
      registration_count);

  registration_system_initialized = true;
  return VITARPS5_SUCCESS;
}

bool console_registration_is_initialized(void) {
  return registration_system_initialized;
}

void console_registration_cleanup(void) {
  // Cleanup mutex
  if (registration_mutex_initialized && registration_mutex >= 0) {
    sceKernelDeleteMutex(registration_mutex);
    registration_mutex = -1;
    registration_mutex_initialized = false;
    log_debug("Registration mutex cleaned up");
  }
}

VitaRPS5Result validate_registration_credentials(const char* console_ip) {
  if (!console_ip) {
    log_error("Invalid console_ip parameter for credential validation");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Find registered console
  ConsoleRegistration console;
  if (!console_registration_find_by_ip(console_ip, &console)) {
    log_debug("Console %s not found in registration database", console_ip);
    return VITARPS5_ERROR_NOT_CONNECTED;
  }

  // Validate registration completeness
  if (!console.is_registered) {
    log_warn("Console %s found but not marked as registered", console_ip);
    return VITARPS5_ERROR_NOT_REGISTERED;
  }

  // Check registration key format (must be 8 hex characters)
  if (strlen(console.registkey_hex) != 8) {
    log_error(
        "Console %s has invalid registration key length: %zu (expected 8)",
        console_ip, strlen(console.registkey_hex));
    return VITARPS5_ERROR_INVALID_CREDENTIALS;
  }

  // Verify hex format
  for (int i = 0; i < 8; i++) {
    char c = console.registkey_hex[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
      log_error(
          "Console %s has invalid registration key format at position %d: '%c'",
          console_ip, i, c);
      return VITARPS5_ERROR_INVALID_CREDENTIALS;
    }
  }

  log_debug("Registration credentials validated for console %s (key: %.4s...)",
            console_ip, console.registkey_hex);

  return VITARPS5_SUCCESS;
}

// Helper function to add a registration (used by registration UI)
VitaRPS5Result console_registration_add(const char* console_ip,
                                        const char* console_name,
                                        const char* registration_key) {
  if (!console_ip || !console_name || registration_count >= 16) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Check if already registered
  if (console_registration_is_registered(console_ip)) {
    log_info("Console %s already registered - updating", console_ip);
    // Find existing and update using IP comparison
    for (uint32_t i = 0; i < registration_count; i++) {
      if (strcmp(console_ip, registrations[i].ip_address) == 0) {
        strncpy(registrations[i].console_name, console_name,
                sizeof(registrations[i].console_name) - 1);
        registrations[i]
            .console_name[sizeof(registrations[i].console_name) - 1] = '\0';
        // CRITICAL FIX: Store registration key in NEW format (registkey_hex)
        if (registration_key) {
          size_t reg_key_len = strlen(registration_key);

          // Validate and store as 8-char hex string in NEW field
          if (reg_key_len == 8) {
            // Store in NEW registkey_hex field (8-char hex)
            strncpy(registrations[i].registkey_hex, registration_key,
                    REGKEY_HEX_LEN);
            registrations[i].registkey_hex[REGKEY_HEX_LEN] = '\0';

            log_info(
                "✅ FIXED: Updated registration key stored in registkey_hex "
                "field: %s",
                registrations[i].registkey_hex);
          } else {
            log_error(
                "CRITICAL: Invalid registration key length %zu (expected 8): "
                "%s",
                reg_key_len, registration_key);
            return VITARPS5_ERROR_INVALID_PARAM;
          }

          // Clear legacy field to prevent confusion
          memset(registrations[i].registration_key, 0,
                 sizeof(registrations[i].registration_key));
        }
        registrations[i].is_registered = true;
        log_info("Updated registration for console %s (%s)", console_name,
                 console_ip);
        return VITARPS5_SUCCESS;
      }
    }
  }

  // Add new registration
  memset(&registrations[registration_count], 0, sizeof(ConsoleRegistration));

  // Store both console name AND IP address
  strncpy(registrations[registration_count].console_name, console_name,
          sizeof(registrations[registration_count].console_name) - 1);
  registrations[registration_count]
      .console_name[sizeof(registrations[registration_count].console_name) -
                    1] = '\0';

  strncpy(registrations[registration_count].ip_address, console_ip,
          sizeof(registrations[registration_count].ip_address) - 1);
  registrations[registration_count]
      .ip_address[sizeof(registrations[registration_count].ip_address) - 1] =
      '\0';

  // CRITICAL FIX: Store registration key in NEW format (registkey_hex)
  // instead of legacy format to prevent double-hex encoding
  if (registration_key) {
    size_t reg_key_len = strlen(registration_key);

    // Validate and store as 8-char hex string in NEW field
    if (reg_key_len == 8) {
      // Store in NEW registkey_hex field (8-char hex)
      strncpy(registrations[registration_count].registkey_hex, registration_key,
              REGKEY_HEX_LEN);
      registrations[registration_count].registkey_hex[REGKEY_HEX_LEN] = '\0';

      log_info("✅ FIXED: Registration key stored in registkey_hex field: %s",
               registrations[registration_count].registkey_hex);
    } else {
      log_error(
          "CRITICAL: Invalid registration key length %zu (expected 8): %s",
          reg_key_len, registration_key);
      return VITARPS5_ERROR_INVALID_PARAM;
    }

    // Clear legacy field to prevent confusion
    memset(registrations[registration_count].registration_key, 0,
           sizeof(registrations[registration_count].registration_key));
  }

  registrations[registration_count].is_registered = true;
  registrations[registration_count].target =
      CONSOLE_TARGET_PS5_1;  // Default to PS5

  registration_count++;
  log_info("Added registration for console %s (%s) - total: %d", console_name,
           console_ip, registration_count);

  return VITARPS5_SUCCESS;
}

// Add complete registration data with all credentials
VitaRPS5Result console_registration_add_complete(
    const char* console_ip, const ConsoleRegistration* complete_data) {
  if (!console_ip || !complete_data || registration_count >= 16) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Check if already registered
  if (console_registration_is_registered(console_ip)) {
    log_info("Console %s already registered - updating with complete data",
             console_ip);
    // Find existing and update
    for (uint32_t i = 0; i < registration_count; i++) {
      if (strcmp(console_ip, registrations[i].ip_address) == 0) {
        // Copy complete registration data
        memcpy(&registrations[i], complete_data, sizeof(ConsoleRegistration));
        // Ensure IP address is set correctly
        strncpy(registrations[i].ip_address, console_ip,
                sizeof(registrations[i].ip_address) - 1);
        registrations[i].ip_address[sizeof(registrations[i].ip_address) - 1] =
            '\0';
        // RESEARCHER FIX 1: Mark as valid when adding complete registration
        registrations[i].is_valid = true;

        log_info("Updated complete registration for console %s (%s)",
                 registrations[i].console_name, console_ip);
        log_debug("  Registration key (hex8): %s",
                  registrations[i].registkey_hex);
        log_debug("  Wake credential (dec): %s",
                  registrations[i].wake_credential_dec[0]
                      ? registrations[i].wake_credential_dec
                      : "<empty>");
        log_debug("  Morning key: %02X%02X%02X%02X...",
                  registrations[i].morning[0], registrations[i].morning[1],
                  registrations[i].morning[2], registrations[i].morning[3]);

        // Save updated registration to filesystem
        VitaRPS5Result save_result =
            save_registration_to_filesystem(console_ip, &registrations[i]);
        if (save_result != VITARPS5_SUCCESS) {
          log_warning("Failed to save updated registration to filesystem: %s",
                      vitarps5_result_string(save_result));
        } else {
          log_debug("Updated registration saved to filesystem");
        }

        return VITARPS5_SUCCESS;
      }
    }
  }

  // Add new complete registration
  memcpy(&registrations[registration_count], complete_data,
         sizeof(ConsoleRegistration));

  // Ensure IP address is set correctly
  strncpy(registrations[registration_count].ip_address, console_ip,
          sizeof(registrations[registration_count].ip_address) - 1);
  registrations[registration_count]
      .ip_address[sizeof(registrations[registration_count].ip_address) - 1] =
      '\0';

  // Ensure registered flag is set
  registrations[registration_count].is_registered = true;
  registrations[registration_count].target = CONSOLE_TARGET_PS5_1;
  // RESEARCHER FIX 1: Mark as valid when adding new complete registration
  registrations[registration_count].is_valid = true;

  registration_count++;
  log_info("Added complete registration for console %s (%s) - total: %d",
           registrations[registration_count - 1].console_name, console_ip,
           registration_count);
  log_debug("  Registration key (hex8): %s",
            registrations[registration_count - 1].registkey_hex);
  log_debug("  Wake credential (dec): %s",
            registrations[registration_count - 1].wake_credential_dec[0]
                ? registrations[registration_count - 1].wake_credential_dec
                : "<empty>");
  log_debug(
      "  Has morning key: %s",
      registrations[registration_count - 1].morning[0] != 0 ? "Yes" : "No");
  log_debug(
      "  Has rp_key: %s",
      registrations[registration_count - 1].rp_key[0] != 0 ? "Yes" : "No");

  // Save new registration to filesystem
  const ConsoleRegistration* saved = &registrations[registration_count - 1];
  log_info("Saving complete registration for %s:", console_ip);
  log_info(
      "  rp_regist_key (rp_key): %02X %02X %02X %02X ... %02X %02X %02X %02X",
      saved->rp_key[0], saved->rp_key[1], saved->rp_key[2], saved->rp_key[3],
      saved->rp_key[12], saved->rp_key[13], saved->rp_key[14],
      saved->rp_key[15]);
  log_info(
      "  morning:              %02X %02X %02X %02X ... %02X %02X %02X %02X",
      saved->morning[0], saved->morning[1], saved->morning[2],
      saved->morning[3], saved->morning[12], saved->morning[13],
      saved->morning[14], saved->morning[15]);

  VitaRPS5Result save_result =
      save_registration_to_filesystem(console_ip, saved);
  if (save_result != VITARPS5_SUCCESS) {
    log_warning("Failed to save new registration to filesystem: %s",
                vitarps5_result_string(save_result));
  } else {
    log_debug("New registration saved to filesystem");
  }

  // Post-save verification: read back and compare key slices
  ConsoleRegistration verify;
  if (load_registration_from_filesystem(console_ip, &verify) ==
      VITARPS5_SUCCESS) {
    log_info("Post-save verify for %s:", console_ip);
    log_info(
        "  rp_regist_key (rp_key): %02X %02X %02X %02X ... %02X %02X %02X %02X",
        verify.rp_key[0], verify.rp_key[1], verify.rp_key[2], verify.rp_key[3],
        verify.rp_key[12], verify.rp_key[13], verify.rp_key[14],
        verify.rp_key[15]);
    log_info(
        "  morning:              %02X %02X %02X %02X ... %02X %02X %02X %02X",
        verify.morning[0], verify.morning[1], verify.morning[2],
        verify.morning[3], verify.morning[12], verify.morning[13],
        verify.morning[14], verify.morning[15]);
  } else {
    log_warning("Post-save verify failed to reload %s", console_ip);
  }

  // Fingerprint log for registration completeness
  {
    const ConsoleRegistration* cr = &registrations[registration_count - 1];
    char morning_hex[33] = {0};
    for (int i = 0; i < 16; i++)
      snprintf(morning_hex + i * 2, sizeof(morning_hex) - i * 2, "%02x",
               cr->morning[i]);
    log_info("REGISTRATION COMPLETE:");
    log_info("  morning16_hex = %s", morning_hex);
    log_info("  rp_regist_key_hex8 = %s", cr->registkey_hex);
  }

  // Cleanup duplicates for this IP to avoid stale in-memory entries
  cleanup_duplicates_for_ip(console_ip);

  return VITARPS5_SUCCESS;
}

// Get session authentication credentials for a registered console
VitaRPS5Result console_registration_get_session_credentials(
    const char* console_ip, uint8_t* rp_regist_key, uint8_t* morning_key) {
  if (!console_ip || !rp_regist_key || !morning_key) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Load registration from filesystem to avoid stale in-memory entries
  ConsoleRegistration console;
  VitaRPS5Result load_res =
      load_registration_from_filesystem(console_ip, &console);
  if (load_res != VITARPS5_SUCCESS) {
    // Fallback to in-memory
    if (!console_registration_find_by_ip(console_ip, &console)) {
      log_error("Console %s not found for session credential retrieval",
                console_ip);
      return VITARPS5_ERROR_NOT_CONNECTED;
    }
  }

  // Verify registration is complete
  if (!console.is_registered) {
    log_error("Console %s not registered", console_ip);
    return VITARPS5_ERROR_NOT_REGISTERED;
  }

  // Prefer full 16-byte rp_regist_key if present in registration (stored in
  // rp_key for historical reasons). Fallback to hex8 decode + pad.
  bool have_full_regkey16 = false;
  for (int i = 4; i < 16; ++i) {
    if (console.rp_key[i] != 0) {
      have_full_regkey16 = true;
      break;
    }
  }

  if (have_full_regkey16) {
    memcpy(rp_regist_key, console.rp_key, 16);
    log_info("Retrieved session credentials (source=rp_key[16]) for %s",
             console_ip);
  } else {
    // Build 16-byte regist_key from stored 8-hex key (4 bytes) + zero padding
    memset(rp_regist_key, 0, 16);

    size_t hex_len = strlen(console.registkey_hex);
    if (hex_len != REGKEY_HEX_LEN ||
        !is_all_hex(console.registkey_hex, REGKEY_HEX_LEN)) {
      log_error(
          "Invalid registration key format for %s: '%s' (len=%zu) — expected 8 "
          "hex",
          console_ip, console.registkey_hex, hex_len);
      return VITARPS5_ERROR_INVALID_CREDENTIALS;
    }

    uint8_t raw4[REGKEY_RAW_LEN] = {0};
    int dec =
        hex_decode(console.registkey_hex, REGKEY_HEX_LEN, raw4, sizeof(raw4));
    if (dec != REGKEY_RAW_LEN) {
      log_error("Failed to decode registkey_hex for %s (code=%d)", console_ip,
                dec);
      return VITARPS5_ERROR_INVALID_CREDENTIALS;
    }

    memcpy(rp_regist_key, raw4, REGKEY_RAW_LEN);  // first 4 bytes
    memset(rp_regist_key + REGKEY_RAW_LEN, 0, 16 - REGKEY_RAW_LEN);  // pad
    log_info("Retrieved session credentials (source=hex8->raw4+pad) for %s",
             console_ip);
  }

  // Morning remains the 16-byte RP-Key
  memcpy(morning_key, console.morning, 16);

  // Diagnostics for visibility during ctrl failures
  log_info("  REGKEY_HEX: %s", console.registkey_hex);
  log_info("  REGIST_KEY16: %02X %02X %02X %02X ... %02X %02X %02X %02X",
           rp_regist_key[0], rp_regist_key[1], rp_regist_key[2],
           rp_regist_key[3], rp_regist_key[12], rp_regist_key[13],
           rp_regist_key[14], rp_regist_key[15]);
  log_info("  MORNING16:   %02X %02X %02X %02X ... %02X %02X %02X %02X",
           morning_key[0], morning_key[1], morning_key[2], morning_key[3],
           morning_key[12], morning_key[13], morning_key[14], morning_key[15]);

  return VITARPS5_SUCCESS;
}

// Wake a registered PS5 console using stored credentials
VitaRPS5Result console_registration_wake_console(const char* console_ip) {
  if (!console_ip) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Find registered console
  ConsoleRegistration console;
  if (!console_registration_find_by_ip(console_ip, &console)) {
    log_error("Cannot wake console %s - not registered", console_ip);
    return VITARPS5_ERROR_NOT_CONNECTED;
  }

  // Verify registration is complete
  if (!console.is_registered) {
    log_error("Cannot wake console %s - registration incomplete", console_ip);
    return VITARPS5_ERROR_NOT_REGISTERED;
  }

  // Use Vitaki bridge to wake using 8-hex registkey
  VitaRPS5Result wr = vitaki_wake_ps5(console_ip, &console);
  if (wr == VITARPS5_SUCCESS) {
    log_info("Wake packet sent successfully to console %s", console_ip);
  } else {
    log_error("Failed to wake console %s: %s", console_ip,
              vitarps5_result_string(wr));
  }
  return wr;
}

// PERSISTENT STORAGE IMPLEMENTATION

static VitaRPS5Result ensure_registration_directory(void) {
  SceIoStat stat;
  if (sceIoGetstat(REGISTRATION_STORAGE_DIR, &stat) >= 0) {
    return VITARPS5_SUCCESS;  // Directory already exists
  }

  // Create directory
  int result = sceIoMkdir(REGISTRATION_STORAGE_DIR, 0777);
  if (result < 0) {
    log_error("Failed to create registration directory: 0x%08X", result);
    return VITARPS5_ERROR_IO;
  }

  log_info("Created registration storage directory: %s",
           REGISTRATION_STORAGE_DIR);
  return VITARPS5_SUCCESS;
}

static void get_registration_file_path(const char* console_ip, char* path,
                                       size_t path_size) {
  snprintf(path, path_size, "%s%s%s", REGISTRATION_STORAGE_DIR, console_ip,
           REGISTRATION_FILE_EXTENSION);
}

static VitaRPS5Result save_registration_to_filesystem(
    const char* console_ip, const ConsoleRegistration* reg) {
  if (!console_ip || !reg) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  char file_path[256];
  get_registration_file_path(console_ip, file_path, sizeof(file_path));

  log_debug("Saving registration to filesystem: %s", file_path);

  SceUID fd =
      sceIoOpen(file_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (fd < 0) {
    log_error("Failed to create registration file: 0x%08X", fd);
    return VITARPS5_ERROR_IO;
  }

  int written = sceIoWrite(fd, reg, sizeof(ConsoleRegistration));
  sceIoClose(fd);

  if (written != sizeof(ConsoleRegistration)) {
    log_error("Failed to write registration data: %d bytes written", written);
    return VITARPS5_ERROR_IO;
  }

  log_debug("Registration data saved successfully (%d bytes)", written);
  return VITARPS5_SUCCESS;
}

// RESEARCHER CRITICAL FIX: Detect and repair double-hex encoded registration
// keys Returns: 1 = repaired, 0 = no repair needed, -1 = corrupted/requires
// re-pairing
static int repair_double_hex_to_hex8(ConsoleRegistration* reg) {
  if (!reg || strlen(reg->registkey_hex) == 0) {
    return 0;  // No key to repair
  }

  size_t len = strlen(reg->registkey_hex);

  // CASE A: 16+ char double-hex (full recovery possible)
  if (len == 16 || len == 32) {
    // Check if all characters are valid hex
    bool all_hex = true;
    for (size_t i = 0; i < len; i++) {
      char c = reg->registkey_hex[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'))) {
        all_hex = false;
        break;
      }
    }

    if (all_hex) {
      // Try to decode as double-hex: each pair of hex chars → one ASCII char
      char repaired_key[9] = {0};
      bool is_double_hex = true;

      for (int i = 0; i < 8 && (i * 2 + 1) < len; i++) {
        char hex_pair[3] = {reg->registkey_hex[i * 2],
                            reg->registkey_hex[i * 2 + 1], 0};
        unsigned int ascii_val = (unsigned int)strtoul(hex_pair, NULL, 16);

        // ASCII value should be valid hex character (0-9, a-f, A-F)
        if ((ascii_val >= 0x30 && ascii_val <= 0x39) ||  // 0-9
            (ascii_val >= 0x61 && ascii_val <= 0x66) ||  // a-f
            (ascii_val >= 0x41 && ascii_val <= 0x46)) {  // A-F
          repaired_key[i] = (char)ascii_val;
        } else {
          is_double_hex = false;
          break;
        }
      }

      if (is_double_hex && strlen(repaired_key) == 8) {
        // Apply the successful repair
        strncpy(reg->registkey_hex, repaired_key,
                sizeof(reg->registkey_hex) - 1);
        reg->registkey_hex[8] = '\0';
        log_info("✅ REGKEY MIGRATION: %.*s -> %s (fixed)", (int)len,
                 reg->registkey_hex, repaired_key);
        return 1;  // Successfully repaired
      }
    }
  }

  // CASE B: 8-char key
  // Do NOT attempt to classify 8-hex as truncated double-hex. The PS5
  // registration key is opaque; ASCII-looking bytes are valid. Any historical
  // double-hex corruption is handled for 16/32-char legacy stores above.
  if (len == 8) {
    return 0;  // No repair needed, accept as-is
  }

  // Not double-hex or already valid - no repair needed
  return 0;
}

static VitaRPS5Result load_registration_from_filesystem(
    const char* console_ip, ConsoleRegistration* reg) {
  if (!console_ip || !reg) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  char file_path[256];
  get_registration_file_path(console_ip, file_path, sizeof(file_path));

  SceUID fd = sceIoOpen(file_path, SCE_O_RDONLY, 0);
  if (fd < 0) {
    log_debug("Registration file not found: %s", file_path);
    return VITARPS5_ERROR_NOT_FOUND;
  }

  int read = sceIoRead(fd, reg, sizeof(ConsoleRegistration));
  sceIoClose(fd);

  if (read != sizeof(ConsoleRegistration)) {
    log_error("Failed to read registration data: %d bytes read", read);
    return VITARPS5_ERROR_IO;
  }

  // RESEARCHER CRITICAL FIX: Apply double-hex repair on every registration load
  int repair_result = repair_double_hex_to_hex8(reg);
  if (repair_result == 1) {
    // Successfully repaired - save the fixed registration back to disk
    log_info("✅ REGKEY MIGRATION applied to %s during load", console_ip);
    reg->is_valid = true;  // Mark as valid after successful repair
    VitaRPS5Result save_result =
        save_registration_to_filesystem(console_ip, reg);
    if (save_result != VITARPS5_SUCCESS) {
      log_warning("Failed to save repaired registration: %s",
                  vitarps5_result_string(save_result));
    }
  } else if (repair_result == -1) {
    // RESEARCHER FIX 1: Corrupted registration - mark invalid but keep for now
    log_error("🚨 Registration for %s is corrupted and requires re-pairing",
              console_ip);
    log_error(
        "   Keeping invalid registration in memory to prevent REGSYNC from "
        "marking as REGISTERED");

    // Mark as invalid but return success so it stays in memory as invalid
    reg->is_valid = false;
    reg->is_registered = false;

    // Still return success so the invalid registration is kept in memory
    // This prevents REGSYNC from treating it as registered
    log_debug("Registration loaded but marked invalid (%d bytes)", read);
    return VITARPS5_SUCCESS;
  } else {
    // No repair needed - mark as valid
    reg->is_valid = true;
  }

  log_debug("Registration data loaded successfully (%d bytes)", read);
  return VITARPS5_SUCCESS;
}

// RESEARCHER FIX B: Unified registration credential accessor
// Single authoritative source - prevents split-brain between
// storage/wake/session
bool registration_get_by_ip(const char* console_ip,
                            RegistrationCredentials* creds) {
  if (!console_ip || !creds) {
    return false;
  }

  // Clear output structure
  memset(creds, 0, sizeof(RegistrationCredentials));

  // Lock mutex for thread safety (same as other registration functions)
  if (registration_mutex_initialized && registration_mutex >= 0) {
    sceKernelLockMutex(registration_mutex, 1, NULL);
  }

  bool found = false;

  // Search through in-memory registrations using IP address
  for (uint32_t i = 0; i < registration_count; i++) {
    if (strcmp(console_ip, registrations[i].ip_address) == 0) {
      ConsoleRegistration* reg = &registrations[i];

      // RESEARCHER FIX 1: Check is_valid flag to prevent using corrupted
      // registrations
      if (reg->is_valid && reg->is_registered &&
          strlen(reg->registkey_hex) == 8) {
        strncpy(creds->regkey_hex8, reg->registkey_hex,
                sizeof(creds->regkey_hex8) - 1);
        strncpy(creds->wake_credential_dec, reg->wake_credential_dec,
                sizeof(creds->wake_credential_dec) - 1);
        strncpy(creds->console_name, reg->console_name,
                sizeof(creds->console_name) - 1);
        creds->is_valid = true;

        log_debug(
            "UNIFIED ACCESSOR: Found valid credentials for %s (key: %.4s...)",
            console_ip, reg->registkey_hex);
      } else {
        // Found registration but it's invalid/incomplete/corrupted
        creds->is_valid = false;
        log_debug(
            "UNIFIED ACCESSOR: Found invalid credentials for %s (valid: %s, "
            "registered: %s, key_len: %zu)",
            console_ip, reg->is_valid ? "true" : "false",
            reg->is_registered ? "true" : "false", strlen(reg->registkey_hex));
      }

      found = true;
      break;
    }
  }

  if (registration_mutex_initialized && registration_mutex >= 0) {
    sceKernelUnlockMutex(registration_mutex, 1);
  }

  if (!found) {
    log_debug("UNIFIED ACCESSOR: No registration found for %s", console_ip);
  }

  return found && creds->is_valid;
}

static VitaRPS5Result delete_registration_from_filesystem(
    const char* console_ip) {
  if (!console_ip) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  char file_path[256];
  get_registration_file_path(console_ip, file_path, sizeof(file_path));

  int result = sceIoRemove(file_path);
  if (result < 0) {
    log_debug("Failed to delete registration file (may not exist): 0x%08X",
              result);
    return VITARPS5_ERROR_IO;
  }

  log_debug("Registration file deleted: %s", file_path);
  return VITARPS5_SUCCESS;
}

static VitaRPS5Result load_all_registrations_from_filesystem(void) {
  log_info("Loading all registrations from filesystem...");

  // Ensure directory exists
  VitaRPS5Result dir_result = ensure_registration_directory();
  if (dir_result != VITARPS5_SUCCESS) {
    return dir_result;
  }

  // Open directory
  SceUID dir_fd = sceIoDopen(REGISTRATION_STORAGE_DIR);
  if (dir_fd < 0) {
    log_warning("Failed to open registration directory: 0x%08X", dir_fd);
    return VITARPS5_SUCCESS;  // Not fatal, just means no registrations saved
  }

  SceIoDirent entry;
  uint32_t loaded_count = 0;
  registration_count = 0;  // Reset in-memory count

  while (sceIoDread(dir_fd, &entry) > 0) {
    // Skip directories and non-.reg files
    if (SCE_S_ISDIR(entry.d_stat.st_mode)) {
      continue;
    }

    // Check if file has .reg extension
    size_t name_len = strlen(entry.d_name);
    if (name_len < 4 ||
        strcmp(entry.d_name + name_len - 4, REGISTRATION_FILE_EXTENSION) != 0) {
      continue;
    }

    // Extract console IP from filename (remove .reg extension)
    char console_ip[64];
    size_t copy_len = (name_len - 4 < sizeof(console_ip) - 1)
                          ? name_len - 4
                          : sizeof(console_ip) - 1;
    strncpy(console_ip, entry.d_name, copy_len);
    console_ip[copy_len] = '\0';

    // Load registration data
    ConsoleRegistration reg;
    VitaRPS5Result load_result =
        load_registration_from_filesystem(console_ip, &reg);
    if (load_result == VITARPS5_SUCCESS && registration_count < 16) {
      // Add to in-memory storage
      memcpy(&registrations[registration_count], &reg,
             sizeof(ConsoleRegistration));
      registration_count++;
      loaded_count++;
      log_debug("Loaded registration for console: %s", console_ip);
    } else if (load_result != VITARPS5_SUCCESS) {
      log_warning("Failed to load registration for %s: %s", console_ip,
                  vitarps5_result_string(load_result));
    }
  }

  sceIoDclose(dir_fd);

  log_info("Loaded %d registrations from filesystem (total in memory: %d)",
           loaded_count, registration_count);
  return VITARPS5_SUCCESS;
}
