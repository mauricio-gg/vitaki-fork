#include "profile_storage.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logger.h"

// Profile storage configuration
#define PROFILE_DATA_DIR "ux0:data/VitaRPS5"
#define PROFILE_FILE_PATH "ux0:data/VitaRPS5/profile.json"
#define PROFILE_BACKUP_PATH "ux0:data/VitaRPS5/profile_backup.json"
#define PROFILE_VERSION 1
#define MAX_FILE_SIZE (64 * 1024)  // 64KB max profile file size

// Thread safety
static SceKernelLwMutexWork profile_mutex_work;
static bool profile_initialized = false;
static ProfileData cached_profile = {0};
static bool profile_loaded = false;

// Internal helper functions
static VitaRPS5Result create_profile_directory(void);
static VitaRPS5Result write_profile_to_file(const ProfileData* profile,
                                            const char* file_path);
static VitaRPS5Result read_profile_from_file(ProfileData* profile,
                                             const char* file_path);
static VitaRPS5Result serialize_profile_to_json(const ProfileData* profile,
                                                char* json_buffer,
                                                size_t buffer_size);
static VitaRPS5Result deserialize_profile_from_json(const char* json_data,
                                                    ProfileData* profile);
static uint64_t get_current_timestamp(void);
static void sanitize_string(char* str, size_t max_length);
static void validate_profile_strings(ProfileData* profile);

VitaRPS5Result profile_storage_init(void) {
  log_info("Initializing profile storage system");

  if (profile_initialized) {
    log_warning("Profile storage already initialized");
    return VITARPS5_SUCCESS;
  }

  // Initialize mutex for thread safety
  int ret =
      sceKernelCreateLwMutex(&profile_mutex_work, "ProfileMutex", 0, 0, NULL);
  if (ret < 0) {
    log_error("Failed to create profile mutex: 0x%08X", ret);
    return VITARPS5_ERROR_SYSTEM_CALL;
  }

  // Create profile directory if it doesn't exist
  VitaRPS5Result result = create_profile_directory();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create profile directory: %s",
              vitarps5_result_string(result));
    return result;
  }

  // Initialize cached profile data
  memset(&cached_profile, 0, sizeof(cached_profile));
  profile_loaded = false;
  profile_initialized = true;

  log_info("Profile storage system initialized successfully");
  return VITARPS5_SUCCESS;
}

void profile_storage_cleanup(void) {
  if (!profile_initialized) {
    return;
  }

  log_info("Cleaning up profile storage system");

  // Save any pending changes
  if (profile_loaded) {
    VitaRPS5Result result = profile_storage_save(&cached_profile);
    if (result != VITARPS5_SUCCESS) {
      log_warning("Failed to save profile during cleanup: %s",
                  vitarps5_result_string(result));
    }
  }

  // Cleanup mutex
  sceKernelDeleteLwMutex(&profile_mutex_work);

  profile_initialized = false;
  profile_loaded = false;
  memset(&cached_profile, 0, sizeof(cached_profile));
}

VitaRPS5Result profile_storage_load(ProfileData* profile) {
  if (!profile_initialized || !profile) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  sceKernelLockLwMutex(&profile_mutex_work, 1, NULL);

  VitaRPS5Result result = VITARPS5_SUCCESS;

  // If we have cached data, return it
  if (profile_loaded) {
    *profile = cached_profile;
    sceKernelUnlockLwMutex(&profile_mutex_work, 1);
    return VITARPS5_SUCCESS;
  }

  // Try to load from file
  result = read_profile_from_file(&cached_profile, PROFILE_FILE_PATH);
  if (result == VITARPS5_SUCCESS) {
    // Validate loaded profile
    if (!profile_storage_is_valid_profile(&cached_profile)) {
      log_warning("Loaded profile is invalid, creating default");
      result = profile_storage_create_default(&cached_profile);
    } else {
      // Check if migration is needed
      if (cached_profile.profile_version < PROFILE_VERSION) {
        log_info("Migrating profile from version %d to %d",
                 cached_profile.profile_version, PROFILE_VERSION);
        result = profile_storage_migrate_profile(
            &cached_profile, cached_profile.profile_version);
        if (result == VITARPS5_SUCCESS) {
          // Save migrated profile
          result = write_profile_to_file(&cached_profile, PROFILE_FILE_PATH);
        }
      }
    }
  } else if (result == VITARPS5_ERROR_FILE_NOT_FOUND) {
    // Create default profile for first run
    log_info("Profile not found, creating default profile");
    result = profile_storage_create_default(&cached_profile);
    if (result == VITARPS5_SUCCESS) {
      result = write_profile_to_file(&cached_profile, PROFILE_FILE_PATH);
    }
  }

  if (result == VITARPS5_SUCCESS) {
    *profile = cached_profile;
    profile_loaded = true;
  }

  sceKernelUnlockLwMutex(&profile_mutex_work, 1);
  return result;
}

VitaRPS5Result profile_storage_save(const ProfileData* profile) {
  if (!profile_initialized || !profile) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (!profile_storage_is_valid_profile(profile)) {
    log_error("Cannot save invalid profile");
    return VITARPS5_ERROR_INVALID_DATA;
  }

  sceKernelLockLwMutex(&profile_mutex_work, 1, NULL);

  // Update cached profile
  cached_profile = *profile;
  cached_profile.last_updated = get_current_timestamp();
  profile_loaded = true;

  // Write to file
  VitaRPS5Result result =
      write_profile_to_file(&cached_profile, PROFILE_FILE_PATH);

  sceKernelUnlockLwMutex(&profile_mutex_work, 1);
  return result;
}

VitaRPS5Result profile_storage_create_default(ProfileData* profile) {
  if (!profile) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  memset(profile, 0, sizeof(ProfileData));

  // Initialize default values
  strncpy(profile->display_name, "Vita User",
          sizeof(profile->display_name) - 1);
  profile->preferred_language = 1;       // English
  profile->enter_button_preference = 1;  // Cross
  profile->first_time_setup = true;

  // Streaming defaults
  profile->default_quality_preset = 1;  // Balanced
  profile->hardware_decode_preferred = true;
  profile->show_performance_overlay = false;
  profile->auto_connect_last_console = false;

  // Privacy defaults
  profile->save_credentials = false;
  profile->analytics_enabled = true;
  profile->crash_reporting_enabled = true;

  // Metadata
  profile->profile_version = PROFILE_VERSION;
  profile->created_timestamp = get_current_timestamp();
  profile->last_updated = profile->created_timestamp;

  // Validate all strings
  validate_profile_strings(profile);

  log_info("Created default profile for new user");
  return VITARPS5_SUCCESS;
}

VitaRPS5Result profile_storage_set_psn_credentials(const char* username,
                                                   const char* email,
                                                   bool remember) {
  if (!profile_initialized || !username) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  strncpy(profile.psn_username, username, sizeof(profile.psn_username) - 1);
  if (email) {
    strncpy(profile.psn_email, email, sizeof(profile.psn_email) - 1);
  }
  profile.psn_authenticated = true;
  profile.psn_remember_credentials = remember;
  profile.psn_last_login = get_current_timestamp();

  return profile_storage_save(&profile);
}

VitaRPS5Result profile_storage_get_psn_credentials(char* username, char* email,
                                                   bool* authenticated) {
  if (!profile_initialized) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  if (username) {
    strncpy(username, profile.psn_username, 63);
    username[63] = '\0';
  }
  if (email) {
    strncpy(email, profile.psn_email, 127);
    email[127] = '\0';
  }
  if (authenticated) {
    *authenticated = profile.psn_authenticated;
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result profile_storage_clear_psn_credentials(void) {
  if (!profile_initialized) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  memset(profile.psn_username, 0, sizeof(profile.psn_username));
  memset(profile.psn_email, 0, sizeof(profile.psn_email));
  profile.psn_authenticated = false;
  profile.psn_remember_credentials = false;

  return profile_storage_save(&profile);
}

VitaRPS5Result profile_storage_record_connection(const char* console_ip,
                                                 bool successful) {
  if (!profile_initialized || !console_ip) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  profile.connection_attempts++;
  if (successful) {
    profile.successful_connections++;
    strncpy(profile.last_connected_console_ip, console_ip,
            sizeof(profile.last_connected_console_ip) - 1);
  }

  return profile_storage_save(&profile);
}

bool profile_storage_is_valid_profile(const ProfileData* profile) {
  if (!profile) {
    return false;
  }

  // Check profile version
  if (profile->profile_version == 0 ||
      profile->profile_version > PROFILE_VERSION) {
    return false;
  }

  // Check timestamps
  if (profile->created_timestamp == 0 || profile->last_updated == 0) {
    return false;
  }

  // Check string fields for null termination
  if (strnlen(profile->display_name, sizeof(profile->display_name)) >=
      sizeof(profile->display_name)) {
    return false;
  }

  if (strnlen(profile->psn_username, sizeof(profile->psn_username)) >=
      sizeof(profile->psn_username)) {
    return false;
  }

  return true;
}

const char* profile_storage_get_profile_path(void) { return PROFILE_FILE_PATH; }

bool profile_storage_profile_exists(void) {
  SceIoStat stat;
  return sceIoGetstat(PROFILE_FILE_PATH, &stat) >= 0;
}

// Internal helper function implementations
static VitaRPS5Result create_profile_directory(void) {
  SceIoStat stat;
  if (sceIoGetstat(PROFILE_DATA_DIR, &stat) < 0) {
    int ret = sceIoMkdir(PROFILE_DATA_DIR, 0777);
    if (ret < 0) {
      log_error("Failed to create profile directory: 0x%08X", ret);
      return VITARPS5_ERROR_FILE_IO;
    }
    log_info("Created profile directory: %s", PROFILE_DATA_DIR);
  }
  return VITARPS5_SUCCESS;
}

static VitaRPS5Result write_profile_to_file(const ProfileData* profile,
                                            const char* file_path) {
  char* json_buffer = malloc(MAX_FILE_SIZE);
  if (!json_buffer) {
    log_error("Failed to allocate memory for profile serialization");
    return VITARPS5_ERROR_MEMORY;
  }

  VitaRPS5Result result =
      serialize_profile_to_json(profile, json_buffer, MAX_FILE_SIZE);
  if (result != VITARPS5_SUCCESS) {
    free(json_buffer);
    return result;
  }

  SceUID fd =
      sceIoOpen(file_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (fd < 0) {
    log_error("Failed to open profile file for writing: 0x%08X", fd);
    free(json_buffer);
    return VITARPS5_ERROR_FILE_IO;
  }

  size_t data_size = strlen(json_buffer);
  int written = sceIoWrite(fd, json_buffer, data_size);
  sceIoClose(fd);
  free(json_buffer);

  if (written != (int)data_size) {
    log_error("Failed to write complete profile data: %d/%zu bytes", written,
              data_size);
    return VITARPS5_ERROR_FILE_IO;
  }

  log_info("Profile saved successfully to %s (%zu bytes)", file_path,
           data_size);
  return VITARPS5_SUCCESS;
}

static VitaRPS5Result read_profile_from_file(ProfileData* profile,
                                             const char* file_path) {
  SceUID fd = sceIoOpen(file_path, SCE_O_RDONLY, 0);
  if (fd < 0) {
    // Check for common file not found error codes
    if (fd == -2147024894 || fd == -2147024893) {  // Common not found errors
      return VITARPS5_ERROR_FILE_NOT_FOUND;
    }
    log_error("Failed to open profile file for reading: 0x%08X", fd);
    return VITARPS5_ERROR_FILE_IO;
  }

  char* json_buffer = malloc(MAX_FILE_SIZE);
  if (!json_buffer) {
    sceIoClose(fd);
    return VITARPS5_ERROR_MEMORY;
  }

  int read_size = sceIoRead(fd, json_buffer, MAX_FILE_SIZE - 1);
  sceIoClose(fd);

  if (read_size < 0) {
    log_error("Failed to read profile file: 0x%08X", read_size);
    free(json_buffer);
    return VITARPS5_ERROR_FILE_IO;
  }

  json_buffer[read_size] = '\0';

  VitaRPS5Result result = deserialize_profile_from_json(json_buffer, profile);
  free(json_buffer);

  if (result == VITARPS5_SUCCESS) {
    log_info("Profile loaded successfully from %s (%d bytes)", file_path,
             read_size);
  }

  return result;
}

static VitaRPS5Result serialize_profile_to_json(const ProfileData* profile,
                                                char* json_buffer,
                                                size_t buffer_size) {
  // Simple JSON serialization for profile data
  int written = snprintf(
      json_buffer, buffer_size,
      "{\n"
      "  \"profile_version\": %u,\n"
      "  \"created_timestamp\": %llu,\n"
      "  \"last_updated\": %llu,\n"
      "  \"display_name\": \"%s\",\n"
      "  \"preferred_language\": %d,\n"
      "  \"enter_button_preference\": %d,\n"
      "  \"first_time_setup\": %s,\n"
      "  \"psn_username\": \"%s\",\n"
      "  \"psn_email\": \"%s\",\n"
      "  \"psn_id_base64\": \"%s\",\n"
      "  \"psn_authenticated\": %s,\n"
      "  \"psn_remember_credentials\": %s,\n"
      "  \"psn_last_login\": %llu,\n"
      "  \"default_quality_preset\": %d,\n"
      "  \"hardware_decode_preferred\": %s,\n"
      "  \"show_performance_overlay\": %s,\n"
      "  \"auto_connect_last_console\": %s,\n"
      "  \"last_connected_console_ip\": \"%s\",\n"
      "  \"total_streaming_time\": %llu,\n"
      "  \"successful_connections\": %u,\n"
      "  \"connection_attempts\": %u,\n"
      "  \"save_credentials\": %s,\n"
      "  \"analytics_enabled\": %s,\n"
      "  \"crash_reporting_enabled\": %s\n"
      "}\n",
      profile->profile_version, profile->created_timestamp,
      profile->last_updated, profile->display_name, profile->preferred_language,
      profile->enter_button_preference,
      profile->first_time_setup ? "true" : "false", profile->psn_username,
      profile->psn_email, profile->psn_id_base64,
      profile->psn_authenticated ? "true" : "false",
      profile->psn_remember_credentials ? "true" : "false",
      profile->psn_last_login, profile->default_quality_preset,
      profile->hardware_decode_preferred ? "true" : "false",
      profile->show_performance_overlay ? "true" : "false",
      profile->auto_connect_last_console ? "true" : "false",
      profile->last_connected_console_ip, profile->total_streaming_time,
      profile->successful_connections, profile->connection_attempts,
      profile->save_credentials ? "true" : "false",
      profile->analytics_enabled ? "true" : "false",
      profile->crash_reporting_enabled ? "true" : "false");

  if (written >= (int)buffer_size) {
    log_error("Profile JSON too large for buffer: %d >= %zu", written,
              buffer_size);
    return VITARPS5_ERROR_BUFFER_TOO_SMALL;
  }

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result deserialize_profile_from_json(const char* json_data,
                                                    ProfileData* profile) {
  // Simple JSON parsing for profile data
  // In a production environment, you'd want to use a proper JSON library

  memset(profile, 0, sizeof(ProfileData));

  // This is a simplified parser - parse key values we need
  char temp_str[256];

  // Parse profile version
  if (sscanf(strstr(json_data, "\"profile_version\":"),
             "\"profile_version\": %u", &profile->profile_version) != 1) {
    profile->profile_version = 1;  // Default
  }

  // Parse timestamps
  sscanf(strstr(json_data, "\"created_timestamp\":"),
         "\"created_timestamp\": %llu", &profile->created_timestamp);
  sscanf(strstr(json_data, "\"last_updated\":"), "\"last_updated\": %llu",
         &profile->last_updated);

  // Parse display name
  if (sscanf(strstr(json_data, "\"display_name\":"),
             "\"display_name\": \"%255[^\"]\"", temp_str) == 1) {
    strncpy(profile->display_name, temp_str, sizeof(profile->display_name) - 1);
  }

  // Parse PSN username
  if (sscanf(strstr(json_data, "\"psn_username\":"),
             "\"psn_username\": \"%255[^\"]\"", temp_str) == 1) {
    strncpy(profile->psn_username, temp_str, sizeof(profile->psn_username) - 1);
  }

  // Parse PSN ID base64
  if (sscanf(strstr(json_data, "\"psn_id_base64\":"),
             "\"psn_id_base64\": \"%31[^\"]\"", temp_str) == 1) {
    strncpy(profile->psn_id_base64, temp_str,
            sizeof(profile->psn_id_base64) - 1);
  }

  // Parse boolean values
  profile->psn_authenticated =
      strstr(json_data, "\"psn_authenticated\": true") != NULL;
  profile->first_time_setup =
      strstr(json_data, "\"first_time_setup\": true") != NULL;
  profile->hardware_decode_preferred =
      strstr(json_data, "\"hardware_decode_preferred\": true") != NULL;

  // Parse numeric values
  sscanf(strstr(json_data, "\"successful_connections\":"),
         "\"successful_connections\": %u", &profile->successful_connections);
  sscanf(strstr(json_data, "\"connection_attempts\":"),
         "\"connection_attempts\": %u", &profile->connection_attempts);

  return VITARPS5_SUCCESS;
}

static uint64_t get_current_timestamp(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick;
}

static void sanitize_string(char* str, size_t max_length) {
  if (!str) return;

  size_t len = strnlen(str, max_length);
  for (size_t i = 0; i < len; i++) {
    if (str[i] < 32 || str[i] > 126) {  // Non-printable characters
      str[i] = '?';
    }
  }
  str[max_length - 1] = '\0';  // Ensure null termination
}

// Use the sanitize function for string validation
static void validate_profile_strings(ProfileData* profile) {
  sanitize_string(profile->display_name, sizeof(profile->display_name));
  sanitize_string(profile->psn_username, sizeof(profile->psn_username));
  sanitize_string(profile->psn_email, sizeof(profile->psn_email));
}

VitaRPS5Result profile_storage_migrate_profile(ProfileData* profile,
                                               uint32_t from_version) {
  if (!profile) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Migrating profile from version %u to %u", from_version,
           PROFILE_VERSION);

  // For now, just update the version number
  // Future migrations would handle data structure changes
  profile->profile_version = PROFILE_VERSION;
  profile->last_updated = get_current_timestamp();

  return VITARPS5_SUCCESS;
}

// Additional API functions implementation
VitaRPS5Result profile_storage_set_display_name(const char* name) {
  if (!profile_initialized || !name) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  strncpy(profile.display_name, name, sizeof(profile.display_name) - 1);
  profile.display_name[sizeof(profile.display_name) - 1] = '\0';

  return profile_storage_save(&profile);
}

VitaRPS5Result profile_storage_set_quality_preset(int preset) {
  if (!profile_initialized || preset < 0 || preset > 2) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  profile.default_quality_preset = preset;
  return profile_storage_save(&profile);
}

VitaRPS5Result profile_storage_set_hardware_decode(bool enabled) {
  if (!profile_initialized) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  profile.hardware_decode_preferred = enabled;
  return profile_storage_save(&profile);
}

VitaRPS5Result profile_storage_set_performance_overlay(bool enabled) {
  if (!profile_initialized) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  profile.show_performance_overlay = enabled;
  return profile_storage_save(&profile);
}

VitaRPS5Result profile_storage_add_streaming_time(uint32_t minutes) {
  if (!profile_initialized) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  profile.total_streaming_time += minutes;
  return profile_storage_save(&profile);
}

VitaRPS5Result profile_storage_get_usage_stats(uint32_t* total_connections,
                                               uint32_t* successful_connections,
                                               uint64_t* total_streaming_time) {
  if (!profile_initialized) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  if (total_connections) {
    *total_connections = profile.connection_attempts;
  }
  if (successful_connections) {
    *successful_connections = profile.successful_connections;
  }
  if (total_streaming_time) {
    *total_streaming_time = profile.total_streaming_time;
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result profile_storage_update_system_info(
    const VitaSystemInfo* system_info) {
  if (!profile_initialized || !system_info) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  profile.cached_system_info = *system_info;
  profile.system_info_last_updated = get_current_timestamp();

  return profile_storage_save(&profile);
}

VitaRPS5Result profile_storage_get_cached_system_info(
    VitaSystemInfo* system_info, bool* is_fresh) {
  if (!profile_initialized || !system_info) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  *system_info = profile.cached_system_info;

  if (is_fresh) {
    // Consider fresh if updated within last hour (3600 seconds)
    uint64_t current_time = get_current_timestamp();
    uint64_t hour_in_ticks = 3600ULL * sceRtcGetTickResolution();
    *is_fresh =
        (current_time - profile.system_info_last_updated) < hour_in_ticks;
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result profile_storage_backup_profile(void) {
  if (!profile_initialized) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProfileData profile;
  VitaRPS5Result result = profile_storage_load(&profile);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  // Write to backup file
  result = write_profile_to_file(&profile, PROFILE_BACKUP_PATH);
  if (result == VITARPS5_SUCCESS) {
    log_info("Profile backed up successfully");
  }

  return result;
}

VitaRPS5Result profile_storage_restore_profile(void) {
  if (!profile_initialized) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Check if backup exists
  SceIoStat stat;
  if (sceIoGetstat(PROFILE_BACKUP_PATH, &stat) < 0) {
    log_error("No backup profile found");
    return VITARPS5_ERROR_FILE_NOT_FOUND;
  }

  ProfileData backup_profile;
  VitaRPS5Result result =
      read_profile_from_file(&backup_profile, PROFILE_BACKUP_PATH);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to read backup profile: %s",
              vitarps5_result_string(result));
    return result;
  }

  // Validate backup profile
  if (!profile_storage_is_valid_profile(&backup_profile)) {
    log_error("Backup profile is invalid");
    return VITARPS5_ERROR_INVALID_DATA;
  }

  // Save as current profile
  result = profile_storage_save(&backup_profile);
  if (result == VITARPS5_SUCCESS) {
    log_info("Profile restored from backup successfully");
  }

  return result;
}