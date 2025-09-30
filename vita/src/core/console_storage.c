#include "console_storage.h"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../discovery/ps5_discovery.h"
#include "../ui/ui_core.h"    // For UI event emission
#include "../ui/vita2d_ui.h"  // For ConsoleStatus constants
#include "../utils/logger.h"
#include "console_registration.h"  // For registration validation

// JSON parsing constants
#define JSON_BUFFER_SIZE 8192
#define JSON_TOKEN_SIZE 256
#define JSON_MAX_DEPTH 8

// Global state
static bool console_storage_initialized = false;
static SceUID storage_mutex = -1;

// Internal functions
static VitaRPS5Result ensure_data_directory(void);
static VitaRPS5Result write_json_to_file(const char* json_data, size_t length);
static VitaRPS5Result read_json_from_file(char* buffer, size_t buffer_size,
                                          size_t* bytes_read);
static VitaRPS5Result serialize_cache_to_json(const ConsoleCacheData* cache,
                                              char* json_buffer,
                                              size_t buffer_size);
static VitaRPS5Result deserialize_cache_from_json(const char* json_data,
                                                  ConsoleCacheData* cache);
static VitaRPS5Result deserialize_console_from_json(const char* json_data,
                                                    UIConsoleInfo* console);
static uint64_t get_current_timestamp(void);
static VitaRPS5Result lock_storage(void);
static VitaRPS5Result unlock_storage(void);

// Forward declaration for conversion function
VitaRPS5Result console_storage_convert_ps5_info(const PS5ConsoleInfo* ps5_info,
                                                UIConsoleInfo* ui_info);

// Simple JSON serialization helpers
static void json_escape_string(const char* src, char* dst, size_t dst_size);
static VitaRPS5Result json_find_value(const char* json, const char* key,
                                      char* value, size_t value_size);
static VitaRPS5Result json_find_array_start(const char* json, const char* key,
                                            const char** array_start);

// API Implementation

VitaRPS5Result console_storage_init(void) {
  if (console_storage_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing console storage subsystem");

  // Create mutex for thread safety
  storage_mutex = sceKernelCreateMutex("console_storage_mutex", 0, 0, NULL);
  if (storage_mutex < 0) {
    log_error("Failed to create storage mutex: 0x%08X", storage_mutex);
    return VITARPS5_ERROR_INIT;
  }

  // Ensure data directory exists
  VitaRPS5Result result = ensure_data_directory();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create data directory: %s",
              vitarps5_result_string(result));
    sceKernelDeleteMutex(storage_mutex);
    return result;
  }

  console_storage_initialized = true;
  log_info("Console storage subsystem initialized successfully");

  return VITARPS5_SUCCESS;
}

void console_storage_cleanup(void) {
  if (!console_storage_initialized) {
    return;
  }

  log_info("Cleaning up console storage subsystem");

  if (storage_mutex >= 0) {
    sceKernelDeleteMutex(storage_mutex);
    storage_mutex = -1;
  }

  console_storage_initialized = false;
  log_info("Console storage cleanup complete");
}

VitaRPS5Result console_storage_save(const ConsoleCacheData* cache) {
  if (!console_storage_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!cache) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  VitaRPS5Result result = lock_storage();
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  char* json_buffer = malloc(JSON_BUFFER_SIZE);
  if (!json_buffer) {
    unlock_storage();
    return VITARPS5_ERROR_MEMORY;
  }

  // Serialize cache to JSON
  result = serialize_cache_to_json(cache, json_buffer, JSON_BUFFER_SIZE);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to serialize cache to JSON: %s",
              vitarps5_result_string(result));
    free(json_buffer);
    unlock_storage();
    return result;
  }

  // Write JSON to file
  result = write_json_to_file(json_buffer, strlen(json_buffer));
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to write JSON to file: %s",
              vitarps5_result_string(result));
  } else {
    log_info("Console cache saved successfully (%d consoles)",
             cache->console_count);
  }

  free(json_buffer);
  unlock_storage();
  return result;
}

VitaRPS5Result console_storage_load(ConsoleCacheData* cache) {
  if (!console_storage_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!cache) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  VitaRPS5Result result = lock_storage();
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  // Initialize cache with defaults
  console_storage_init_default_cache(cache);

  // Check if file exists
  if (!console_storage_exists()) {
    log_info("Console cache file does not exist, starting with empty cache");
    unlock_storage();
    return VITARPS5_SUCCESS;  // Empty cache is valid
  }

  char* json_buffer = malloc(JSON_BUFFER_SIZE);
  if (!json_buffer) {
    unlock_storage();
    return VITARPS5_ERROR_MEMORY;
  }

  size_t bytes_read;
  result = read_json_from_file(json_buffer, JSON_BUFFER_SIZE, &bytes_read);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to read JSON from file: %s",
              vitarps5_result_string(result));
    free(json_buffer);
    unlock_storage();
    return result;
  }

  // Deserialize JSON to cache
  result = deserialize_cache_from_json(json_buffer, cache);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to deserialize cache from JSON: %s",
              vitarps5_result_string(result));
    // Initialize empty cache on parse failure
    console_storage_init_default_cache(cache);
  } else {
    log_info("Console cache loaded successfully (%d consoles)",
             cache->console_count);

    // Validate cache integrity
    result = console_storage_validate_cache(cache);
    if (result != VITARPS5_SUCCESS) {
      log_warning("Cache validation failed, starting with empty cache");
      console_storage_init_default_cache(cache);
    }
  }

  free(json_buffer);
  unlock_storage();
  return VITARPS5_SUCCESS;  // Always return success, even with empty cache
}

bool console_storage_exists(void) {
  SceIoStat stat;
  int result = sceIoGetstat(CONSOLE_STORAGE_PATH, &stat);
  return (result >= 0 && SCE_S_ISREG(stat.st_mode));
}

VitaRPS5Result console_storage_get_file_size(size_t* size) {
  if (!size) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  SceIoStat stat;
  int result = sceIoGetstat(CONSOLE_STORAGE_PATH, &stat);
  if (result < 0) {
    return VITARPS5_ERROR_FILE_NOT_FOUND;
  }

  *size = stat.st_size;
  return VITARPS5_SUCCESS;
}

VitaRPS5Result console_storage_add_console(ConsoleCacheData* cache,
                                           const PS5ConsoleInfo* console) {
  if (!cache || !console) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Check if console already exists
  if (console_storage_console_exists(cache, console->host_id)) {
    log_warning("Console with host_id %s already exists", console->host_id);
    return VITARPS5_ERROR_CONSOLE_EXISTS;
  }

  // Check if cache is full
  if (cache->console_count >= MAX_SAVED_CONSOLES) {
    log_error("Console cache is full (%d consoles)", MAX_SAVED_CONSOLES);
    return VITARPS5_ERROR_CACHE_FULL;
  }

  // Convert PS5ConsoleInfo to UIConsoleInfo
  UIConsoleInfo* ui_console = &cache->consoles[cache->console_count];
  VitaRPS5Result result = console_storage_convert_ps5_info(console, ui_console);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  // Set additional UI properties
  ui_console->added_timestamp = get_current_timestamp();
  ui_console->last_seen = get_current_timestamp();
  ui_console->is_favorite = false;
  ui_console->connection_count = 0;

  // Set initial discovery state based on console state from discovery
  if (console->is_awake) {
    ui_console->discovery_state = CONSOLE_DISCOVERY_STATE_READY;
    log_info("Setting initial state to READY (console is awake)");
  } else {
    ui_console->discovery_state = CONSOLE_DISCOVERY_STATE_STANDBY;
    log_info("Setting initial state to STANDBY (console is in standby)");
  }

  // Generate display name
  console_storage_generate_display_name(ui_console, ui_console->display_name,
                                        sizeof(ui_console->display_name));

  cache->console_count++;
  cache->last_updated = get_current_timestamp();
  cache->cache_valid = true;

  log_info("Added console: %s (%s) at %s", ui_console->display_name,
           ps5_console_type_string(ui_console->console_type),
           ui_console->ip_address);

  return VITARPS5_SUCCESS;
}

VitaRPS5Result console_storage_remove_console(ConsoleCacheData* cache,
                                              const char* host_id) {
  if (!cache || !host_id) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Find console index
  int console_index = -1;
  for (uint32_t i = 0; i < cache->console_count; i++) {
    if (strcmp(cache->consoles[i].host_id, host_id) == 0) {
      console_index = i;
      break;
    }
  }

  if (console_index == -1) {
    log_warning("Console with host_id %s not found for removal", host_id);
    return VITARPS5_ERROR_CONSOLE_NOT_FOUND;
  }

  UIConsoleInfo* console = &cache->consoles[console_index];
  log_info("Removing console: %s (%s)", console->display_name,
           console->ip_address);

  // Shift remaining consoles down
  for (uint32_t i = console_index; i < cache->console_count - 1; i++) {
    cache->consoles[i] = cache->consoles[i + 1];
  }

  cache->console_count--;
  cache->last_updated = get_current_timestamp();

  // Clear the last console slot
  memset(&cache->consoles[cache->console_count], 0, sizeof(UIConsoleInfo));

  return VITARPS5_SUCCESS;
}

VitaRPS5Result console_storage_find_console(const ConsoleCacheData* cache,
                                            const char* host_id,
                                            UIConsoleInfo** console) {
  if (!cache || !host_id || !console) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  for (uint32_t i = 0; i < cache->console_count; i++) {
    if (strcmp(cache->consoles[i].host_id, host_id) == 0) {
      *console = (UIConsoleInfo*)&cache->consoles[i];
      return VITARPS5_SUCCESS;
    }
  }

  *console = NULL;
  return VITARPS5_ERROR_CONSOLE_NOT_FOUND;
}

bool console_storage_console_exists(const ConsoleCacheData* cache,
                                    const char* host_id) {
  UIConsoleInfo* console;
  return console_storage_find_console(cache, host_id, &console) ==
         VITARPS5_SUCCESS;
}

VitaRPS5Result console_storage_validate_cache(ConsoleCacheData* cache) {
  if (!cache) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Check version compatibility
  if (cache->cache_version != CONSOLE_CACHE_VERSION) {
    log_warning("Cache version mismatch: expected %d, got %d",
                CONSOLE_CACHE_VERSION, cache->cache_version);
    return VITARPS5_ERROR_INVALID_DATA;
  }

  // Check console count bounds
  if (cache->console_count > MAX_SAVED_CONSOLES) {
    log_error("Invalid console count: %d (max: %d)", cache->console_count,
              MAX_SAVED_CONSOLES);
    return VITARPS5_ERROR_INVALID_DATA;
  }

  // Validate individual consoles
  for (uint32_t i = 0; i < cache->console_count; i++) {
    UIConsoleInfo* console = &cache->consoles[i];

    // Check required fields
    if (strlen(console->host_id) == 0 || strlen(console->ip_address) == 0) {
      log_error("Console %d has invalid host_id or ip_address", i);
      return VITARPS5_ERROR_INVALID_DATA;
    }

    // Check for duplicate host_ids
    for (uint32_t j = i + 1; j < cache->console_count; j++) {
      if (strcmp(console->host_id, cache->consoles[j].host_id) == 0) {
        log_error("Duplicate host_id found: %s", console->host_id);
        return VITARPS5_ERROR_INVALID_DATA;
      }
    }

    // Hard registration preflight: if stored registration is invalid, mark as
    // not registered
    if ((console->console_type == PS_CONSOLE_PS5 ||
         console->console_type == PS_CONSOLE_PS5_DIGITAL) &&
        console->is_registered) {
      ConsoleRegistration reg;
      if (console_registration_find_by_ip(console->ip_address, &reg)) {
        RegInvariants inv;
        bool ok = console_registration_check_invariants(&reg, &inv);
        if (!ok) {
          log_error(
              "REG PREFLIGHT: invalid store for %s (hex8=%d, morning16=%d). "
              "Forcing re-pair.",
              console->ip_address, inv.has_hex8, inv.has_morning16);
          console->is_registered = false;  // force re-pair banner upstream
        }
      }
    }
  }

  cache->cache_valid = true;
  return VITARPS5_SUCCESS;
}

VitaRPS5Result console_storage_init_default_cache(ConsoleCacheData* cache) {
  if (!cache) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  memset(cache, 0, sizeof(ConsoleCacheData));
  cache->cache_version = CONSOLE_CACHE_VERSION;
  cache->last_updated = get_current_timestamp();
  cache->cache_valid = true;
  cache->console_count = 0;

  return VITARPS5_SUCCESS;
}

VitaRPS5Result console_storage_convert_ps5_info(const PS5ConsoleInfo* ps5_info,
                                                UIConsoleInfo* ui_info) {
  if (!ps5_info || !ui_info) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  memset(ui_info, 0, sizeof(UIConsoleInfo));

  // Copy basic information
  strncpy(ui_info->ip_address, ps5_info->ip_address,
          sizeof(ui_info->ip_address) - 1);
  strncpy(ui_info->device_name, ps5_info->device_name,
          sizeof(ui_info->device_name) - 1);
  strncpy(ui_info->host_id, ps5_info->host_id, sizeof(ui_info->host_id) - 1);
  strncpy(ui_info->mac_address, ps5_info->mac_address,
          sizeof(ui_info->mac_address) - 1);

  ui_info->console_type = ps5_info->console_type;
  ui_info->port = ps5_info->port;
  ui_info->supports_h265 = ps5_info->supports_h265;
  ui_info->fw_version = ps5_info->fw_version;

  // Initialize UI-specific fields
  ui_info->is_favorite = false;
  ui_info->last_connected = 0;
  ui_info->last_seen = get_current_timestamp();
  ui_info->connection_count = 0;
  ui_info->added_timestamp = get_current_timestamp();

  // Set default discovery state
  ui_info->discovery_state = ps5_info->is_awake
                                 ? CONSOLE_DISCOVERY_STATE_READY
                                 : CONSOLE_DISCOVERY_STATE_STANDBY;
  ui_info->signal_strength = ps5_info->signal_strength;
  ui_info->is_reachable = ps5_info->is_awake;

  return VITARPS5_SUCCESS;
}

VitaRPS5Result console_storage_generate_display_name(
    const UIConsoleInfo* console, char* display_name, size_t max_len) {
  if (!console || !display_name || max_len == 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Use device name if it's descriptive enough
  if (strlen(console->device_name) > 0 &&
      strcmp(console->device_name, "PlayStation-5") != 0 &&
      strcmp(console->device_name, "PlayStation-4") != 0) {
    strncpy(display_name, console->device_name, max_len - 1);
    display_name[max_len - 1] = '\0';
    return VITARPS5_SUCCESS;
  }

  // Generate name based on console type and IP
  const char* type_name = "PlayStation";
  switch (console->console_type) {
    case PS_CONSOLE_PS5:
      type_name = "PS5";
      break;
    case PS_CONSOLE_PS5_DIGITAL:
      type_name = "PS5 Digital";
      break;
    case PS_CONSOLE_PS4:
      type_name = "PS4";
      break;
    case PS_CONSOLE_PS4_PRO:
      type_name = "PS4 Pro";
      break;
    default:
      type_name = "PlayStation";
      break;
  }

  // Extract last octet of IP for unique identifier
  const char* last_dot = strrchr(console->ip_address, '.');
  const char* ip_suffix = last_dot ? last_dot + 1 : "000";

  snprintf(display_name, max_len, "%s-%s", type_name, ip_suffix);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result console_storage_cleanup_invalid_consoles(
    ConsoleCacheData* cache, uint32_t* removed_count) {
  if (!cache) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  uint32_t removed = 0;
  uint32_t i = 0;

  log_info(
      "Starting cleanup of invalid/mock registered consoles (found %d consoles "
      "to check)",
      cache->console_count);

  while (i < cache->console_count) {
    UIConsoleInfo* console = &cache->consoles[i];
    bool should_remove = false;

    log_debug("Checking console[%d]: %s (%s) - Type: %d", i,
              console->display_name, console->ip_address,
              console->console_type);

    // Only check PS5 consoles for mock registration cleanup
    if (console->console_type == PS_CONSOLE_PS5 ||
        console->console_type == PS_CONSOLE_PS5_DIGITAL) {
      // Check if this console has any registration
      ConsoleRegistration reg;
      VitaRPS5Result reg_result =
          console_registration_find_by_ip(console->ip_address, &reg);

      if (reg_result == VITARPS5_SUCCESS) {
        // Console has registration - validate if it's valid or needs cleanup
        if (strncmp(reg.registration_key, "MOCK", 4) == 0) {
          log_warning(
              "Found PS5 console with MOCK registration: %s (%s) - REMOVING",
              console->display_name, console->ip_address);
          log_warning("  Mock registration key: %s", reg.registration_key);
          should_remove = true;

          // Also remove the mock registration from storage
          VitaRPS5Result remove_result =
              console_registration_remove(console->ip_address);
          log_info("Mock registration removal result: %s",
                   vitarps5_result_string(remove_result));
        } else {
          // RESEARCHER C) PATCH: Replace any homegrown "is registered?" guess
          // with single authoritative call Use
          // console_registration_is_registered() under reg mutex as the only
          // truth source
          bool has_reg =
              console_registration_is_registered(console->ip_address);
          if (has_reg) {
            if (!console->is_registered) {
              console->is_registered = true;
              log_info("REGSYNC: storage state set to REGISTERED");
              ui_emit_console_state_changed(console->ip_address,
                                            CONSOLE_STATE_REGISTERED);
            }
            // never mark unauthenticated if registration exists
            i++;
            continue;
          }

          // only here: no registration found
          console->is_registered = false;
          log_info(
              "REGSYNC: storage state set to UNAUTHENTICATED (no registration "
              "found)");
        }
      } else {
        // RESEARCHER C) PATCH: Also apply authoritative check for other
        // branches
        bool has_reg = console_registration_is_registered(console->ip_address);
        if (has_reg) {
          if (!console->is_registered) {
            console->is_registered = true;
            log_info("REGSYNC: storage state set to REGISTERED");
          }
          i++;
          continue;
        }

        // only here: no registration found
        console->is_registered = false;
        log_info(
            "REGSYNC: storage state set to UNAUTHENTICATED (no registration "
            "found)");
      }
    } else {
      // PS4 consoles don't need registration cleanup
      log_debug("PS4 console %s - skipping registration check",
                console->display_name);
    }

    if (should_remove) {
      log_warning("REMOVING console: %s (%s)", console->display_name,
                  console->ip_address);

      // Remove this console by shifting remaining consoles down
      for (uint32_t j = i; j < cache->console_count - 1; j++) {
        cache->consoles[j] = cache->consoles[j + 1];
      }
      cache->console_count--;
      removed++;
      // Don't increment i since we shifted elements down
    } else {
      log_debug("KEEPING console: %s (%s)", console->display_name,
                console->ip_address);
      i++;
    }
  }

  if (removed > 0) {
    cache->last_updated = get_current_timestamp();
    log_warning(
        "Cleanup complete: removed %d mock-registered consoles, %d consoles "
        "remaining",
        removed, cache->console_count);
  } else {
    log_info(
        "Cleanup complete: no mock-registered consoles found, all %d consoles "
        "kept",
        cache->console_count);
  }

  if (removed_count) {
    *removed_count = removed;
  }

  return VITARPS5_SUCCESS;
}

// Internal Helper Functions

static VitaRPS5Result ensure_data_directory(void) {
  SceIoStat stat;
  int result = sceIoGetstat(CONSOLE_STORAGE_DIR, &stat);

  if (result < 0) {
    // Directory doesn't exist, create it
    result = sceIoMkdir(CONSOLE_STORAGE_DIR, 0777);
    if (result < 0) {
      log_error("Failed to create data directory %s: 0x%08X",
                CONSOLE_STORAGE_DIR, result);
      return VITARPS5_ERROR_FILE_IO;
    }
    log_info("Created data directory: %s", CONSOLE_STORAGE_DIR);
  } else if (!SCE_S_ISDIR(stat.st_mode)) {
    log_error("Data path exists but is not a directory: %s",
              CONSOLE_STORAGE_DIR);
    return VITARPS5_ERROR_FILE_IO;
  }

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result write_json_to_file(const char* json_data, size_t length) {
  SceUID file =
      sceIoOpen(CONSOLE_STORAGE_PATH, SCE_O_WRONLY | SCE_O_CREAT, 0777);
  if (file < 0) {
    log_error("Failed to open file for writing: %s (0x%08X)",
              CONSOLE_STORAGE_PATH, file);
    return VITARPS5_ERROR_FILE_IO;
  }

  int bytes_written = sceIoWrite(file, json_data, length);
  sceIoClose(file);

  if (bytes_written < 0 || (size_t)bytes_written != length) {
    log_error("Failed to write complete data to file: wrote %d/%zu bytes",
              bytes_written, length);
    return VITARPS5_ERROR_FILE_IO;
  }

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result read_json_from_file(char* buffer, size_t buffer_size,
                                          size_t* bytes_read) {
  SceUID file = sceIoOpen(CONSOLE_STORAGE_PATH, SCE_O_RDONLY, 0);
  if (file < 0) {
    log_error("Failed to open file for reading: %s (0x%08X)",
              CONSOLE_STORAGE_PATH, file);
    return VITARPS5_ERROR_FILE_IO;
  }

  int read_result = sceIoRead(file, buffer, buffer_size - 1);
  sceIoClose(file);

  if (read_result < 0) {
    log_error("Failed to read from file: 0x%08X", read_result);
    return VITARPS5_ERROR_FILE_IO;
  }

  buffer[read_result] = '\0';  // Null terminate
  *bytes_read = read_result;
  return VITARPS5_SUCCESS;
}

static VitaRPS5Result serialize_cache_to_json(const ConsoleCacheData* cache,
                                              char* json_buffer,
                                              size_t buffer_size) {
  if (!cache || !json_buffer) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Start JSON object
  int written =
      snprintf(json_buffer, buffer_size,
               "{\n"
               "  \"version\": %u,\n"
               "  \"last_updated\": %llu,\n"
               "  \"console_count\": %u,\n"
               "  \"consoles\": [\n",
               cache->cache_version, cache->last_updated, cache->console_count);

  if (written >= (int)buffer_size) {
    return VITARPS5_ERROR_BUFFER_TOO_SMALL;
  }

  // Serialize each console
  for (uint32_t i = 0; i < cache->console_count; i++) {
    const UIConsoleInfo* console = &cache->consoles[i];

    // Escape strings for JSON
    char escaped_device_name[128];
    char escaped_display_name[128];
    json_escape_string(console->device_name, escaped_device_name,
                       sizeof(escaped_device_name));
    json_escape_string(console->display_name, escaped_display_name,
                       sizeof(escaped_display_name));

    int console_written = snprintf(
        json_buffer + written, buffer_size - written,
        "    {\n"
        "      \"ip_address\": \"%s\",\n"
        "      \"device_name\": \"%s\",\n"
        "      \"display_name\": \"%s\",\n"
        "      \"host_id\": \"%s\",\n"
        "      \"mac_address\": \"%s\",\n"
        "      \"console_type\": %d,\n"
        "      \"port\": %u,\n"
        "      \"supports_h265\": %s,\n"
        "      \"fw_version\": %u,\n"
        "      \"is_favorite\": %s,\n"
        "      \"last_connected\": %llu,\n"
        "      \"last_seen\": %llu,\n"
        "      \"connection_count\": %u,\n"
        "      \"added_timestamp\": %llu,\n"
        "      \"discovery_state\": %d\n"
        "    }%s\n",
        console->ip_address, escaped_device_name, escaped_display_name,
        console->host_id, console->mac_address, console->console_type,
        console->port, console->supports_h265 ? "true" : "false",
        console->fw_version, console->is_favorite ? "true" : "false",
        console->last_connected, console->last_seen, console->connection_count,
        console->added_timestamp, console->discovery_state,
        (i < cache->console_count - 1) ? "," : "");

    written += console_written;
    if (written >= (int)buffer_size) {
      return VITARPS5_ERROR_BUFFER_TOO_SMALL;
    }
  }

  // Close JSON object
  int final_written = snprintf(json_buffer + written, buffer_size - written,
                               "  ]\n"
                               "}\n");
  written += final_written;

  if (written >= (int)buffer_size) {
    return VITARPS5_ERROR_BUFFER_TOO_SMALL;
  }

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result deserialize_cache_from_json(const char* json_data,
                                                  ConsoleCacheData* cache) {
  if (!json_data || !cache) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Initialize cache
  console_storage_init_default_cache(cache);

  // Parse basic fields
  char value_buffer[64];
  if (json_find_value(json_data, "version", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    cache->cache_version = atoi(value_buffer);
  }

  if (json_find_value(json_data, "last_updated", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    cache->last_updated = strtoull(value_buffer, NULL, 10);
  }

  if (json_find_value(json_data, "console_count", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    cache->console_count = atoi(value_buffer);
    if (cache->console_count > MAX_SAVED_CONSOLES) {
      cache->console_count = MAX_SAVED_CONSOLES;
    }
  }

  // Find consoles array
  const char* consoles_array;
  if (json_find_array_start(json_data, "consoles", &consoles_array) !=
      VITARPS5_SUCCESS) {
    log_warning("Failed to find consoles array in JSON");
    return VITARPS5_SUCCESS;  // Empty cache is valid
  }

  // Parse individual consoles (simplified parsing)
  // Note: This is a basic JSON parser. In a production system,
  // you might want to use a proper JSON library.
  const char* current = consoles_array;
  uint32_t parsed_count = 0;

  while (parsed_count < cache->console_count &&
         parsed_count < MAX_SAVED_CONSOLES) {
    // Find next console object
    const char* obj_start = strchr(current, '{');
    if (!obj_start) break;

    const char* obj_end = strchr(obj_start, '}');
    if (!obj_end) break;

    // Extract console object
    size_t obj_len = obj_end - obj_start + 1;
    char* console_json = malloc(obj_len + 1);
    if (!console_json) break;

    strncpy(console_json, obj_start, obj_len);
    console_json[obj_len] = '\0';

    // Deserialize this console
    VitaRPS5Result result = deserialize_console_from_json(
        console_json, &cache->consoles[parsed_count]);

    free(console_json);

    if (result == VITARPS5_SUCCESS) {
      parsed_count++;
    }

    current = obj_end + 1;
  }

  cache->console_count = parsed_count;
  cache->cache_valid = true;

  log_info("Deserialized %d consoles from JSON", parsed_count);
  return VITARPS5_SUCCESS;
}

static VitaRPS5Result deserialize_console_from_json(const char* json_data,
                                                    UIConsoleInfo* console) {
  if (!json_data || !console) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  memset(console, 0, sizeof(UIConsoleInfo));

  char value_buffer[256];

  // Parse string fields
  if (json_find_value(json_data, "ip_address", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    strncpy(console->ip_address, value_buffer, sizeof(console->ip_address) - 1);
  }

  if (json_find_value(json_data, "device_name", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    strncpy(console->device_name, value_buffer,
            sizeof(console->device_name) - 1);
  }

  if (json_find_value(json_data, "display_name", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    strncpy(console->display_name, value_buffer,
            sizeof(console->display_name) - 1);
  }

  if (json_find_value(json_data, "host_id", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    strncpy(console->host_id, value_buffer, sizeof(console->host_id) - 1);
  }

  if (json_find_value(json_data, "mac_address", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    strncpy(console->mac_address, value_buffer,
            sizeof(console->mac_address) - 1);
  }

  // Parse numeric fields
  if (json_find_value(json_data, "console_type", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    console->console_type = (PSConsoleType)atoi(value_buffer);
  }

  if (json_find_value(json_data, "port", value_buffer, sizeof(value_buffer)) ==
      VITARPS5_SUCCESS) {
    console->port = atoi(value_buffer);
  }

  if (json_find_value(json_data, "fw_version", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    console->fw_version = atoi(value_buffer);
  }

  if (json_find_value(json_data, "connection_count", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    console->connection_count = atoi(value_buffer);
  }

  // Parse timestamp fields
  if (json_find_value(json_data, "last_connected", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    console->last_connected = strtoull(value_buffer, NULL, 10);
  }

  if (json_find_value(json_data, "last_seen", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    console->last_seen = strtoull(value_buffer, NULL, 10);
  }

  if (json_find_value(json_data, "added_timestamp", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    console->added_timestamp = strtoull(value_buffer, NULL, 10);
  }

  // Parse boolean fields
  if (json_find_value(json_data, "supports_h265", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    console->supports_h265 = (strcmp(value_buffer, "true") == 0);
  }

  if (json_find_value(json_data, "is_favorite", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    console->is_favorite = (strcmp(value_buffer, "true") == 0);
  }

  // Load discovery state if available, otherwise default to unknown
  if (json_find_value(json_data, "discovery_state", value_buffer,
                      sizeof(value_buffer)) == VITARPS5_SUCCESS) {
    console->discovery_state = (ConsoleDiscoveryState)atoi(value_buffer);
    log_info("Loaded console discovery state: %d", console->discovery_state);
  } else {
    // Initialize to unknown if not present (backward compatibility)
    console->discovery_state = CONSOLE_DISCOVERY_STATE_UNKNOWN;
    log_info("No discovery state in storage, defaulting to UNKNOWN");
  }
  console->signal_strength = 0.8f;  // Assume good signal for stored consoles
  console->is_reachable = true;

  return VITARPS5_SUCCESS;
}

// Simple JSON helper functions
static void json_escape_string(const char* src, char* dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0) {
    return;
  }

  size_t src_len = strlen(src);
  size_t dst_pos = 0;

  for (size_t i = 0; i < src_len && dst_pos < dst_size - 1; i++) {
    char c = src[i];
    if (c == '"' || c == '\\') {
      if (dst_pos < dst_size - 2) {
        dst[dst_pos++] = '\\';
        dst[dst_pos++] = c;
      }
    } else {
      dst[dst_pos++] = c;
    }
  }

  dst[dst_pos] = '\0';
}

static VitaRPS5Result json_find_value(const char* json, const char* key,
                                      char* value, size_t value_size) {
  if (!json || !key || !value) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Create search pattern: "key":
  char search_pattern[128];
  snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);

  const char* found = strstr(json, search_pattern);
  if (!found) {
    return VITARPS5_ERROR_NOT_FOUND;
  }

  // Skip to value
  const char* value_start = found + strlen(search_pattern);
  while (*value_start == ' ' || *value_start == '\t') {
    value_start++;
  }

  // Parse value based on type
  if (*value_start == '"') {
    // String value
    value_start++;  // Skip opening quote
    const char* value_end = strchr(value_start, '"');
    if (!value_end) {
      return VITARPS5_ERROR_PARSE_FAILED;
    }

    size_t value_len = value_end - value_start;
    if (value_len >= value_size) {
      value_len = value_size - 1;
    }

    strncpy(value, value_start, value_len);
    value[value_len] = '\0';
  } else {
    // Numeric or boolean value
    const char* value_end = value_start;
    while (*value_end && *value_end != ',' && *value_end != '}' &&
           *value_end != '\n' && *value_end != ' ') {
      value_end++;
    }

    size_t value_len = value_end - value_start;
    if (value_len >= value_size) {
      value_len = value_size - 1;
    }

    strncpy(value, value_start, value_len);
    value[value_len] = '\0';
  }

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result json_find_array_start(const char* json, const char* key,
                                            const char** array_start) {
  if (!json || !key || !array_start) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Create search pattern: "key": [
  char search_pattern[128];
  snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);

  const char* found = strstr(json, search_pattern);
  if (!found) {
    return VITARPS5_ERROR_NOT_FOUND;
  }

  // Find opening bracket
  const char* bracket = strchr(found, '[');
  if (!bracket) {
    return VITARPS5_ERROR_PARSE_FAILED;
  }

  *array_start = bracket + 1;
  return VITARPS5_SUCCESS;
}

static uint64_t get_current_timestamp(void) {
  // Use same timestamp base as UI code for consistency
  // sceKernelGetSystemTimeWide() returns microseconds since boot
  return sceKernelGetSystemTimeWide() / 1000;  // Convert to milliseconds
}

static VitaRPS5Result lock_storage(void) {
  if (storage_mutex < 0) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  int result = sceKernelLockMutex(storage_mutex, 1, NULL);
  if (result < 0) {
    log_error("Failed to lock storage mutex: 0x%08X", result);
    return VITARPS5_ERROR_INIT;
  }

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result unlock_storage(void) {
  if (storage_mutex < 0) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  int result = sceKernelUnlockMutex(storage_mutex, 1);
  if (result < 0) {
    log_error("Failed to unlock storage mutex: 0x%08X", result);
    return VITARPS5_ERROR_INIT;
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result console_storage_find_by_ip(const char* ip_address,
                                          UIConsoleInfo* console_info) {
  if (!ip_address || !console_info) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Load current console cache
  ConsoleCacheData cache = {0};
  VitaRPS5Result result = console_storage_load(&cache);
  if (result != VITARPS5_SUCCESS) {
    log_warning("Failed to load console cache for IP lookup: %s",
                vitarps5_result_string(result));
    return VITARPS5_ERROR_NOT_FOUND;
  }

  // Search for console by IP address
  for (uint32_t i = 0; i < cache.console_count; i++) {
    if (strcmp(cache.consoles[i].ip_address, ip_address) == 0) {
      // Found matching console
      *console_info = cache.consoles[i];
      log_debug(
          "Found console in cache: %s (discovery_state: %d, registered: %s)",
          ip_address, console_info->discovery_state,
          console_info->is_registered ? "yes" : "no");
      return VITARPS5_SUCCESS;
    }
  }

  log_debug("Console %s not found in cache", ip_address);
  return VITARPS5_ERROR_NOT_FOUND;
}

const char* console_storage_result_string(ConsoleStorageResult result) {
  switch (result) {
    case CONSOLE_STORAGE_SUCCESS:
      return "Success";
    case CONSOLE_STORAGE_ERROR_FILE_NOT_FOUND:
      return "File not found";
    case CONSOLE_STORAGE_ERROR_PARSE_FAILED:
      return "Parse failed";
    case CONSOLE_STORAGE_ERROR_WRITE_FAILED:
      return "Write failed";
    case CONSOLE_STORAGE_ERROR_INVALID_DATA:
      return "Invalid data";
    case CONSOLE_STORAGE_ERROR_CONSOLE_EXISTS:
      return "Console already exists";
    case CONSOLE_STORAGE_ERROR_CONSOLE_NOT_FOUND:
      return "Console not found";
    case CONSOLE_STORAGE_ERROR_CACHE_FULL:
      return "Cache full";
    case CONSOLE_STORAGE_ERROR_DIRECTORY_FAILED:
      return "Directory creation failed";
    default:
      return "Unknown error";
  }
}
