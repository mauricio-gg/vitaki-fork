#include "chiaki_integration.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/vitarps5.h"
#include "../psn/psn_id_utils.h"
#include "../utils/logger.h"
#include "chiaki_log.h"
#include "chiaki_regist_vitaki.h"
#include "chiaki_session.h"

// Global state
static bool chiaki_integration_initialized = false;
static ChiakiRegist global_regist = {0};
static bool registration_in_progress = false;
static VitaRPS5RegistrationCallback current_registration_callback = NULL;
static void* current_registration_user_data = NULL;
// Dedicated Chiaki logger for registration path
static ChiakiLog g_chiaki_reg_log;

// Storage paths
#define REGISTRATION_STORAGE_DIR "ux0:data/VitaRPS5/registrations/"
#define REGISTRATION_FILE_EXTENSION ".reg"

// Internal functions
static void registration_event_callback(ChiakiRegistEvent* event, void* user);
static VitaRPS5Result ensure_registration_directory(void);
static void get_registration_file_path(const char* console_id, char* path,
                                       size_t path_size);

// Route Chiaki logs to VitaRPS5 logger (mirrors session manager mapping)
static void chiaki_reg_log_to_vitarps5(ChiakiLogLevel level, const char* msg,
                                       void* user) {
  (void)user;
  switch (level) {
    case CHIAKI_LOG_LEVEL_VERBOSE:
    case CHIAKI_LOG_LEVEL_DEBUG:
      log_debug("[CHIAKI-REG] %s", msg);
      break;
    case CHIAKI_LOG_LEVEL_INFO:
      log_info("[CHIAKI-REG] %s", msg);
      break;
    case CHIAKI_LOG_LEVEL_WARNING:
      log_warn("[CHIAKI-REG] %s", msg);
      break;
    case CHIAKI_LOG_LEVEL_ERROR:
      log_error("[CHIAKI-REG] %s", msg);
      break;
    default:
      log_info("[CHIAKI-REG] %s", msg);
      break;
  }
}

// Error Code Mapping Implementation

VitaRPS5Result chiaki_to_vitarps5_error(ChiakiErrorCode chiaki_error) {
  switch (chiaki_error) {
    case CHIAKI_ERR_SUCCESS:
      return VITARPS5_SUCCESS;
    case CHIAKI_ERR_PARSE_ADDR:
    case CHIAKI_ERR_HOST_UNREACH:
    case CHIAKI_ERR_CONNECTION_REFUSED:
    case CHIAKI_ERR_NETWORK:  // VITAKI-FORK COMPATIBILITY: Map generic network
                              // errors
      return VITARPS5_ERROR_NETWORK;
    case CHIAKI_ERR_TIMEOUT:
      return VITARPS5_ERROR_TIMEOUT;
    case CHIAKI_ERR_INVALID_DATA:
      return VITARPS5_ERROR_INVALID_PARAM;
    case CHIAKI_ERR_INVALID_MAC:
    case CHIAKI_ERR_AUTH_FAILED:
      return VITARPS5_ERROR_AUTH_FAILED;
    case CHIAKI_ERR_THREAD:
      return VITARPS5_ERROR_THREAD;
    case CHIAKI_ERR_MEMORY:
      return VITARPS5_ERROR_MEMORY;
    case CHIAKI_ERR_QUIT:
    case CHIAKI_ERR_CANCELED:
      return VITARPS5_ERROR_CANCELLED;
    case CHIAKI_ERR_VERSION_MISMATCH:
      return VITARPS5_ERROR_INCOMPATIBLE;
    case CHIAKI_ERR_BUF_TOO_SMALL:
      return VITARPS5_ERROR_BUFFER_TOO_SMALL;
    default:
      log_warning(
          "Unknown ChiakiErrorCode: %d (mapping to VITARPS5_ERROR_UNKNOWN)",
          chiaki_error);
      return VITARPS5_ERROR_UNKNOWN;
  }
}

ChiakiErrorCode vitarps5_to_chiaki_error(VitaRPS5Result vitarps5_result) {
  switch (vitarps5_result) {
    case VITARPS5_SUCCESS:
      return CHIAKI_ERR_SUCCESS;
    case VITARPS5_ERROR_NETWORK:
      return CHIAKI_ERR_CONNECTION_REFUSED;
    case VITARPS5_ERROR_TIMEOUT:
      return CHIAKI_ERR_TIMEOUT;
    case VITARPS5_ERROR_INVALID_PARAM:
      return CHIAKI_ERR_INVALID_DATA;
    case VITARPS5_ERROR_AUTH_FAILED:
      return CHIAKI_ERR_AUTH_FAILED;
    case VITARPS5_ERROR_THREAD:
      return CHIAKI_ERR_THREAD;
    case VITARPS5_ERROR_MEMORY:
      return CHIAKI_ERR_MEMORY;
    case VITARPS5_ERROR_CANCELLED:
      return CHIAKI_ERR_CANCELED;
    case VITARPS5_ERROR_INCOMPATIBLE:
      return CHIAKI_ERR_VERSION_MISMATCH;
    case VITARPS5_ERROR_BUFFER_TOO_SMALL:
      return CHIAKI_ERR_BUF_TOO_SMALL;
    default:
      log_warning("Unknown VitaRPS5Result: %d", vitarps5_result);
      return CHIAKI_ERR_UNKNOWN;
  }
}

VitaRPS5Result chiaki_to_vitarps5_error_with_context(
    ChiakiErrorCode chiaki_error, const char* context_message) {
  VitaRPS5Result result = chiaki_to_vitarps5_error(chiaki_error);
  if (result != VITARPS5_SUCCESS && context_message) {
    log_error("ChiakiSession error in %s: %s", context_message,
              chiaki_error_string(chiaki_error));
  }
  return result;
}

const char* chiaki_error_string(ChiakiErrorCode error) {
  switch (error) {
    case CHIAKI_ERR_SUCCESS:
      return "Success";
    case CHIAKI_ERR_PARSE_ADDR:
      return "Parse address failed";
    case CHIAKI_ERR_THREAD:
      return "Thread error";
    case CHIAKI_ERR_MEMORY:
      return "Memory allocation failed";
    case CHIAKI_ERR_NETWORK:
      return "Network error";
    case CHIAKI_ERR_CONNECTION_REFUSED:
      return "Connection refused";
    case CHIAKI_ERR_HOST_UNREACH:
      return "Host unreachable";
    case CHIAKI_ERR_TIMEOUT:
      return "Operation timed out";
    case CHIAKI_ERR_INVALID_DATA:
      return "Invalid data";
    case CHIAKI_ERR_INVALID_MAC:
      return "Invalid MAC address";
    case CHIAKI_ERR_AUTH_FAILED:
      return "Authentication failed";
    case CHIAKI_ERR_QUIT:
      return "Session quit";
    case CHIAKI_ERR_CANCELED:
      return "Operation canceled";
    case CHIAKI_ERR_VERSION_MISMATCH:
      return "Version mismatch";
    case CHIAKI_ERR_BUF_TOO_SMALL:
      return "Buffer too small";
    case CHIAKI_ERR_UNKNOWN:
    default:
      return "Unknown error";
  }
}

// Implementation

VitaRPS5Result chiaki_integration_init(void) {
  if (chiaki_integration_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing Chiaki integration subsystem");

  // Ensure registration storage directory exists
  VitaRPS5Result result = ensure_registration_directory();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create registration storage directory");
    return result;
  }

  // Initialize global registration state
  memset(&global_regist, 0, sizeof(global_regist));
  registration_in_progress = false;
  current_registration_callback = NULL;
  current_registration_user_data = NULL;

  // Initialize Chiaki logger for registration
  g_chiaki_reg_log.mask = CHIAKI_LOG_ALL;  // enable all levels for diagnostics
  g_chiaki_reg_log.cb = chiaki_reg_log_to_vitarps5;
  g_chiaki_reg_log.user = NULL;

  chiaki_integration_initialized = true;
  log_info("Chiaki integration subsystem initialized successfully");

  return VITARPS5_SUCCESS;
}

void chiaki_integration_cleanup(void) {
  if (!chiaki_integration_initialized) {
    return;
  }

  log_info("Cleaning up Chiaki integration subsystem");

  // Cancel any ongoing registration
  if (registration_in_progress) {
    chiaki_registration_cancel();
  }

  chiaki_integration_initialized = false;
  log_info("Chiaki integration cleanup complete");
}

VitaRPS5Result chiaki_registration_start(const char* console_ip,
                                         const char* psn_account_id,
                                         uint32_t pin,
                                         VitaRPS5RegistrationCallback callback,
                                         void* user_data) {
  if (!chiaki_integration_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!console_ip || !psn_account_id || !callback) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (registration_in_progress) {
    log_warning("Registration already in progress");
    return VITARPS5_ERROR_INVALID_STATE;
  }

  log_info("Starting PS5 registration for %s", console_ip);
  log_debug("Registration setup details:");
  log_debug("  - Target: PS5 console");
  log_debug("  - Console IP: %s", console_ip);
  log_debug("  - Mode: Direct registration (not broadcast)");

  // Setup registration info
  ChiakiRegistInfo regist_info = {0};
  regist_info.target = CHIAKI_TARGET_PS5_1;
  regist_info.host = console_ip;
  regist_info.broadcast = false;
  regist_info.psn_online_id = NULL;
  regist_info.pin = pin;  // Use the provided PIN
  regist_info.console_pin = 0;

  // Convert PSN account ID: directly decode base64 to binary bytes for Chiaki
  if (psn_account_id && strlen(psn_account_id) > 0) {
    uint8_t psn_binary[PSN_ID_BINARY_LENGTH];
    // Directly decode base64 to binary (little-endian, as provided by PSN
    // websites)
    VitaRPS5Result psn_result =
        psn_id_base64_to_binary(psn_account_id, psn_binary, false);

    if (psn_result == VITARPS5_SUCCESS) {
      // Copy the first 8 bytes (CHIAKI_PSN_ACCOUNT_ID_SIZE) for registration
      memcpy(regist_info.psn_account_id, psn_binary,
             CHIAKI_PSN_ACCOUNT_ID_SIZE);

      log_info("PSN account ID converted for registration: %s", psn_account_id);
      log_debug("PSN account ID binary (hex): %02X%02X%02X%02X%02X%02X%02X%02X",
                regist_info.psn_account_id[0], regist_info.psn_account_id[1],
                regist_info.psn_account_id[2], regist_info.psn_account_id[3],
                regist_info.psn_account_id[4], regist_info.psn_account_id[5],
                regist_info.psn_account_id[6], regist_info.psn_account_id[7]);
      log_debug(
          "PSN ID conversion: base64 '%s' -> 8 binary bytes (Chiaki will "
          "re-encode to base64)",
          psn_account_id);
      log_debug("Expected: Chiaki will send Np-AccountId: %s", psn_account_id);
    } else {
      log_error("Failed to decode PSN account ID '%s' to binary: %s",
                psn_account_id, vitarps5_result_string(psn_result));
      log_error("PSN ID Debug - Input analysis:");
      log_error("  - Input string: '%s'", psn_account_id);
      log_error("  - Input length: %zu", strlen(psn_account_id));
      log_error("  - Expected: 11-12 char base64 string");
      log_error("  - Decode result: %s", vitarps5_result_string(psn_result));
      return VITARPS5_ERROR_INVALID_PARAM;
    }
  } else {
    log_error("PSN account ID is required for PS5 registration");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Store callback info
  current_registration_callback = callback;
  current_registration_user_data = user_data;

  // Start registration process
  log_debug("Starting Chiaki registration protocol...");

  // RESEARCHER FIX 6: Clean debug logging - remove confusing unused fields
  log_info("=== CHIAKI REGISTRATION INFO DEBUG ===");
  log_info("Host: %s", regist_info.host ? regist_info.host : "NULL");
  log_info("Target: %d (CHIAKI_TARGET_PS5_1=%d)", regist_info.target,
           CHIAKI_TARGET_PS5_1);
  log_info("Registration PIN: %08u", regist_info.pin);
  log_info("PSN Account ID (binary hex): %02X%02X%02X%02X%02X%02X%02X%02X",
           regist_info.psn_account_id[0], regist_info.psn_account_id[1],
           regist_info.psn_account_id[2], regist_info.psn_account_id[3],
           regist_info.psn_account_id[4], regist_info.psn_account_id[5],
           regist_info.psn_account_id[6], regist_info.psn_account_id[7]);
  log_info("PSN Input (base64): %s", psn_account_id);
  log_info("Registration mode: Direct (non-broadcast) PS5 registration");
  log_info("=== END CHIAKI REGISTRATION DEBUG ===");

  ChiakiErrorCode result =
      chiaki_regist_start(&global_regist, &g_chiaki_reg_log, &regist_info,
                          registration_event_callback, NULL);

  if (result != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to start Chiaki registration: %s",
              chiaki_error_string(result));
    log_error("Chiaki registration start failure details:");
    log_error("  - Error code: %d", result);
    log_error("  - Check network connectivity to PS5");
    log_error("  - Verify PS5 Remote Play is enabled");
    current_registration_callback = NULL;
    current_registration_user_data = NULL;
    return VITARPS5_ERROR_NETWORK;
  }

  registration_in_progress = true;
  log_info("PS5 registration started with PIN %08u", pin);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result chiaki_registration_cancel(void) {
  if (!registration_in_progress) {
    return VITARPS5_SUCCESS;
  }

  log_info("Cancelling PS5 registration");

  chiaki_regist_stop(&global_regist);
  chiaki_regist_fini(&global_regist);

  registration_in_progress = false;

  // Notify callback of cancellation
  if (current_registration_callback) {
    current_registration_callback(REGISTRATION_EVENT_CANCELLED, NULL,
                                  current_registration_user_data);
  }

  current_registration_callback = NULL;
  current_registration_user_data = NULL;

  return VITARPS5_SUCCESS;
}

VitaRPS5Result chiaki_session_create_authenticated(
    const char* console_ip, const VitaRPS5RegistrationData* registration_data,
    VitaRPS5ChiakiSession** session) {
  if (!chiaki_integration_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!console_ip || !registration_data || !session) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Creating authenticated Chiaki session for %s", console_ip);

  // Allocate session
  VitaRPS5ChiakiSession* new_session = malloc(sizeof(VitaRPS5ChiakiSession));
  if (!new_session) {
    return VITARPS5_ERROR_MEMORY;
  }

  memset(new_session, 0, sizeof(VitaRPS5ChiakiSession));

  // Allocate Chiaki session
  new_session->chiaki_session = malloc(sizeof(ChiakiSession));
  if (!new_session->chiaki_session) {
    free(new_session);
    return VITARPS5_ERROR_MEMORY;
  }

  memset(new_session->chiaki_session, 0, sizeof(ChiakiSession));

  // Setup connection info
  strncpy(new_session->console_ip, console_ip,
          sizeof(new_session->console_ip) - 1);
  new_session->is_registered = true;
  new_session->registration_data = *registration_data;

  // Prepare Chiaki connect info
  ChiakiConnectInfo connect_info = {0};
  connect_info.ps5 = registration_data->is_ps5;
  connect_info.host = new_session->console_ip;
  connect_info.video_profile_auto_downgrade = true;
  connect_info.enable_keyboard = false;
  connect_info.enable_dualsense = false;

  // Copy authentication keys
  memcpy(connect_info.regist_key, registration_data->rp_regist_key,
         CHIAKI_SESSION_AUTH_SIZE);
  memcpy(connect_info.morning, registration_data->rp_key,
         sizeof(connect_info.morning));

  // Set video profile for PS Vita
  chiaki_connect_video_profile_preset(&connect_info.video_profile,
                                      CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
                                      CHIAKI_VIDEO_FPS_PRESET_60);

  // Override for Vita screen resolution
  connect_info.video_profile.width = 960;
  connect_info.video_profile.height = 544;
  connect_info.video_profile.bitrate = 10000;  // 10 Mbps

  // Initialize Chiaki session
  ChiakiErrorCode result = chiaki_session_init(
      new_session->chiaki_session, &connect_info, new_session->logger);
  if (result != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to initialize Chiaki session: %s",
              chiaki_error_string(result));
    free(new_session->chiaki_session);
    free(new_session);
    return VITARPS5_ERROR_INIT;
  }

  *session = new_session;
  log_info("Authenticated Chiaki session created successfully");

  return VITARPS5_SUCCESS;
}

VitaRPS5Result chiaki_session_start_streaming(VitaRPS5ChiakiSession* session) {
  if (!session || !session->chiaki_session) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Starting Chiaki streaming session");

  ChiakiErrorCode result = chiaki_session_start(session->chiaki_session);
  if (result != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to start Chiaki session: %s",
              chiaki_error_string(result));
    return VITARPS5_ERROR_NETWORK;
  }

  log_info("Chiaki streaming session started successfully");
  return VITARPS5_SUCCESS;
}

VitaRPS5Result chiaki_session_stop_streaming(VitaRPS5ChiakiSession* session) {
  if (!session || !session->chiaki_session) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Stopping Chiaki streaming session");

  ChiakiErrorCode result = chiaki_session_stop(session->chiaki_session);
  if (result != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to stop Chiaki session: %s", chiaki_error_string(result));
    return VITARPS5_ERROR_NETWORK;
  }

  return VITARPS5_SUCCESS;
}

void chiaki_session_destroy(VitaRPS5ChiakiSession* session) {
  if (!session) {
    return;
  }

  log_info("Destroying Chiaki session");

  if (session->chiaki_session) {
    chiaki_session_fini(session->chiaki_session);
    free(session->chiaki_session);
  }

  free(session);
}

VitaRPS5Result chiaki_session_send_controller_input(
    VitaRPS5ChiakiSession* session, uint32_t buttons, int16_t left_x,
    int16_t left_y, int16_t right_x, int16_t right_y, uint8_t left_trigger,
    uint8_t right_trigger) {
  if (!session || !session->chiaki_session) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // TODO: Implement controller input sending via Chiaki
  // For now, just log the input
  log_debug(
      "Controller input: buttons=0x%08X, left=(%d,%d), right=(%d,%d), "
      "triggers=(%d,%d)",
      buttons, left_x, left_y, right_x, right_y, left_trigger, right_trigger);

  return VITARPS5_SUCCESS;
}

// Storage Functions

VitaRPS5Result chiaki_registration_save(const char* console_id,
                                        const VitaRPS5RegistrationData* data) {
  if (!console_id || !data) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  char file_path[256];
  get_registration_file_path(console_id, file_path, sizeof(file_path));

  log_info("Saving registration data to %s", file_path);

  SceUID fd =
      sceIoOpen(file_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (fd < 0) {
    log_error("Failed to create registration file: 0x%08X", fd);
    return VITARPS5_ERROR_IO;
  }

  int written = sceIoWrite(fd, data, sizeof(VitaRPS5RegistrationData));
  sceIoClose(fd);

  if (written != sizeof(VitaRPS5RegistrationData)) {
    log_error("Failed to write registration data: %d bytes written", written);
    return VITARPS5_ERROR_IO;
  }

  log_info("Registration data saved successfully (%d bytes)", written);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result chiaki_registration_load(const char* console_id,
                                        VitaRPS5RegistrationData* data) {
  if (!console_id || !data) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  char file_path[256];
  get_registration_file_path(console_id, file_path, sizeof(file_path));

  SceUID fd = sceIoOpen(file_path, SCE_O_RDONLY, 0);
  if (fd < 0) {
    log_debug("Registration file not found: %s", file_path);
    return VITARPS5_ERROR_NOT_FOUND;
  }

  int read = sceIoRead(fd, data, sizeof(VitaRPS5RegistrationData));
  sceIoClose(fd);

  if (read != sizeof(VitaRPS5RegistrationData)) {
    log_error("Failed to read registration data: %d bytes read", read);
    return VITARPS5_ERROR_IO;
  }

  log_info("Registration data loaded successfully (%d bytes)", read);
  return VITARPS5_SUCCESS;
}

bool chiaki_registration_exists(const char* console_id) {
  if (!console_id) {
    return false;
  }

  char file_path[256];
  get_registration_file_path(console_id, file_path, sizeof(file_path));

  SceIoStat stat;
  return sceIoGetstat(file_path, &stat) >= 0;
}

VitaRPS5Result chiaki_registration_delete(const char* console_id) {
  if (!console_id) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  char file_path[256];
  get_registration_file_path(console_id, file_path, sizeof(file_path));

  int result = sceIoRemove(file_path);
  if (result < 0) {
    log_error("Failed to delete registration file: 0x%08X", result);
    return VITARPS5_ERROR_IO;
  }

  log_info("Registration data deleted: %s", file_path);
  return VITARPS5_SUCCESS;
}

// Internal Functions

static void registration_event_callback(ChiakiRegistEvent* event, void* user) {
  if (!event || !current_registration_callback) {
    return;
  }

  log_debug("Registration event: %d", event->type);
  log_debug(
      "Registration event details - callback triggered for PS5 registration");

  switch (event->type) {
    case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
      log_info("PS5 registration completed successfully");

      if (event->registered_host) {
        ChiakiRegisteredHost* host = event->registered_host;
        // Integration-level diagnostics: what did the backend give us?
        log_info("REG INTEGRATION: host->target=%d (PS5=%s)", host->target,
                 chiaki_target_is_ps5(host->target) ? "yes" : "no");
        // rp_regist_key is supposed to be 16-byte binary here; log slices
        log_info("REG INTEGRATION: rp_regist_key (first8 as text)=%.8s",
                 host->rp_regist_key);
        log_info(
            "REG INTEGRATION: rp_regist_key bytes=%02X %02X %02X %02X ... %02X "
            "%02X %02X %02X",
            (unsigned char)host->rp_regist_key[0],
            (unsigned char)host->rp_regist_key[1],
            (unsigned char)host->rp_regist_key[2],
            (unsigned char)host->rp_regist_key[3],
            (unsigned char)host->rp_regist_key[12],
            (unsigned char)host->rp_regist_key[13],
            (unsigned char)host->rp_regist_key[14],
            (unsigned char)host->rp_regist_key[15]);
        log_info(
            "REG INTEGRATION: rp_key bytes=%02X %02X %02X %02X ... %02X %02X "
            "%02X %02X",
            host->rp_key[0], host->rp_key[1], host->rp_key[2], host->rp_key[3],
            host->rp_key[12], host->rp_key[13], host->rp_key[14],
            host->rp_key[15]);

        // Convert Chiaki registration data to our format
        VitaRPS5RegistrationData reg_data = {0};

        strncpy(reg_data.server_nickname,
                event->registered_host->server_nickname,
                sizeof(reg_data.server_nickname) - 1);
        memcpy(reg_data.rp_regist_key, event->registered_host->rp_regist_key,
               sizeof(reg_data.rp_regist_key));
        memcpy(reg_data.rp_key, event->registered_host->rp_key,
               sizeof(reg_data.rp_key));
        memcpy(reg_data.server_mac, event->registered_host->server_mac,
               sizeof(reg_data.server_mac));
        reg_data.console_pin = event->registered_host->console_pin;
        reg_data.is_ps5 = chiaki_target_is_ps5(event->registered_host->target);

        // Accept partial RegistKey as in chiaki-ng; pad remains zero-filled.

        current_registration_callback(REGISTRATION_EVENT_SUCCESS, &reg_data,
                                      current_registration_user_data);
      } else {
        current_registration_callback(REGISTRATION_EVENT_SUCCESS, NULL,
                                      current_registration_user_data);
      }
      break;

    case CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED:
      log_error("PS5 registration failed");
      log_error("Registration failure details:");
      log_error("  - Registration attempt failed at PS5 level");
      log_error(
          "  - Possible causes: incorrect PIN, network issues, PS5 settings");
      log_error(
          "  - Check PS5 Remote Play settings: Settings > System > Remote "
          "Play");
      log_error("  - Ensure PS5 is not already paired with another device");
      current_registration_callback(REGISTRATION_EVENT_FAILED, NULL,
                                    current_registration_user_data);
      break;

    case CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED:
      log_info("PS5 registration cancelled");
      current_registration_callback(REGISTRATION_EVENT_CANCELLED, NULL,
                                    current_registration_user_data);
      break;
  }

  // Cleanup registration state
  registration_in_progress = false;
  current_registration_callback = NULL;
  current_registration_user_data = NULL;

  chiaki_regist_fini(&global_regist);
}

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

  return VITARPS5_SUCCESS;
}

static void get_registration_file_path(const char* console_id, char* path,
                                       size_t path_size) {
  snprintf(path, path_size, "%s%s%s", REGISTRATION_STORAGE_DIR, console_id,
           REGISTRATION_FILE_EXTENSION);
}
