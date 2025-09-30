#ifndef VITARPS5_PROFILE_STORAGE_H
#define VITARPS5_PROFILE_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "../system/vita_system_info.h"
#include "vitarps5.h"

// Profile data structure for persistent storage
typedef struct {
  // PSN Authentication
  char psn_username[64];
  char psn_email[128];
  char psn_id_base64[32];  // PSN ID in base64 format for discovery (e.g.,
                           // nD1Ho0mY7wY=)
  bool psn_authenticated;
  bool psn_remember_credentials;
  uint64_t psn_last_login;

  // User Preferences
  char display_name[64];        // User's preferred display name
  int preferred_language;       // System language preference
  int enter_button_preference;  // Cross/Circle preference
  bool first_time_setup;        // Has user completed initial setup

  // Streaming Preferences
  int default_quality_preset;      // Default streaming quality
  bool hardware_decode_preferred;  // Prefer hardware decoding
  bool show_performance_overlay;   // Show FPS/latency overlay
  bool auto_connect_last_console;  // Auto-connect to last used console

  // Console Preferences
  char last_connected_console_ip[16];  // Last successful connection
  uint64_t total_streaming_time;       // Total minutes streamed
  uint32_t successful_connections;     // Count of successful connections
  uint32_t connection_attempts;        // Total connection attempts

  // Privacy & Security
  bool save_credentials;         // Whether to save PSN credentials
  bool analytics_enabled;        // Allow usage analytics
  bool crash_reporting_enabled;  // Allow crash reports

  // System Information Cache (refreshed periodically)
  VitaSystemInfo cached_system_info;
  uint64_t system_info_last_updated;

  // Profile metadata
  uint32_t profile_version;    // For future migration compatibility
  uint64_t created_timestamp;  // When profile was first created
  uint64_t last_updated;       // Last profile modification
} ProfileData;

// Profile storage API
VitaRPS5Result profile_storage_init(void);
void profile_storage_cleanup(void);

// Profile data management
VitaRPS5Result profile_storage_load(ProfileData* profile);
VitaRPS5Result profile_storage_save(const ProfileData* profile);
VitaRPS5Result profile_storage_create_default(ProfileData* profile);

// PSN credential management
VitaRPS5Result profile_storage_set_psn_credentials(const char* username,
                                                   const char* email,
                                                   bool remember);
VitaRPS5Result profile_storage_get_psn_credentials(char* username, char* email,
                                                   bool* authenticated);
VitaRPS5Result profile_storage_clear_psn_credentials(void);

// User preferences
VitaRPS5Result profile_storage_set_display_name(const char* name);
VitaRPS5Result profile_storage_set_quality_preset(int preset);
VitaRPS5Result profile_storage_set_hardware_decode(bool enabled);
VitaRPS5Result profile_storage_set_performance_overlay(bool enabled);

// Usage statistics
VitaRPS5Result profile_storage_record_connection(const char* console_ip,
                                                 bool successful);
VitaRPS5Result profile_storage_add_streaming_time(uint32_t minutes);
VitaRPS5Result profile_storage_get_usage_stats(uint32_t* total_connections,
                                               uint32_t* successful_connections,
                                               uint64_t* total_streaming_time);

// System information caching
VitaRPS5Result profile_storage_update_system_info(
    const VitaSystemInfo* system_info);
VitaRPS5Result profile_storage_get_cached_system_info(
    VitaSystemInfo* system_info, bool* is_fresh);

// Profile validation and migration
bool profile_storage_is_valid_profile(const ProfileData* profile);
VitaRPS5Result profile_storage_migrate_profile(ProfileData* profile,
                                               uint32_t from_version);

// Utility functions
const char* profile_storage_get_profile_path(void);
bool profile_storage_profile_exists(void);
VitaRPS5Result profile_storage_backup_profile(void);
VitaRPS5Result profile_storage_restore_profile(void);

#endif  // VITARPS5_PROFILE_STORAGE_H