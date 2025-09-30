#ifndef VITARPS5_CONSOLE_STORAGE_H
#define VITARPS5_CONSOLE_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "console_types.h"
#include "vitarps5.h"

// Include ps5_discovery.h to get PS5ConsoleInfo definition
// This creates a dependency but is needed for function declarations
#include "../discovery/ps5_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

// Console storage configuration
#define CONSOLE_STORAGE_PATH "ux0:data/VitaRPS5/consoles.json"
#define CONSOLE_STORAGE_DIR "ux0:data/VitaRPS5"
#define MAX_SAVED_CONSOLES 16
#define CONSOLE_CACHE_VERSION 1
#define CONSOLE_CACHE_EXPIRY_HOURS 24

// Extended console information for UI
typedef struct {
  // Basic console information (from PS5ConsoleInfo)
  char ip_address[16];
  char device_name[64];
  char host_id[33];
  char mac_address[18];
  PSConsoleType console_type;
  uint16_t port;
  bool supports_h265;
  uint32_t fw_version;

  // Extended UI information
  char display_name[64];      // User-editable console name
  bool is_favorite;           // User-marked favorite console
  uint64_t last_connected;    // Last successful connection time
  uint64_t last_seen;         // Last time console was discovered
  uint32_t connection_count;  // Total connection attempts
  uint64_t added_timestamp;   // When console was first added

  // Real-time status (not persisted, updated at runtime)
  ConsoleDiscoveryState
      discovery_state;  // Current discovery state: Ready/Standby/Unknown
  uint64_t discovery_state_timestamp;  // When discovery state was last updated
  float signal_strength;               // Network signal quality (0.0-1.0)
  bool is_reachable;                   // Current network reachability
  bool is_registered;                  // Has valid registration credentials
} UIConsoleInfo;

// Console cache data structure
typedef struct {
  UIConsoleInfo consoles[MAX_SAVED_CONSOLES];  // Array of saved consoles
  uint32_t console_count;                      // Number of cached consoles
  uint64_t last_updated;                       // Last cache update timestamp
  uint32_t cache_version;                      // For future compatibility
  bool cache_valid;                            // Cache validation flag
} ConsoleCacheData;

// Console storage result codes
typedef enum {
  CONSOLE_STORAGE_SUCCESS = 0,
  CONSOLE_STORAGE_ERROR_FILE_NOT_FOUND,
  CONSOLE_STORAGE_ERROR_PARSE_FAILED,
  CONSOLE_STORAGE_ERROR_WRITE_FAILED,
  CONSOLE_STORAGE_ERROR_INVALID_DATA,
  CONSOLE_STORAGE_ERROR_CONSOLE_EXISTS,
  CONSOLE_STORAGE_ERROR_CONSOLE_NOT_FOUND,
  CONSOLE_STORAGE_ERROR_CACHE_FULL,
  CONSOLE_STORAGE_ERROR_DIRECTORY_FAILED
} ConsoleStorageResult;

// Core Console Storage API

/**
 * Initialize console storage subsystem
 * Creates data directory if it doesn't exist
 */
VitaRPS5Result console_storage_init(void);

/**
 * Cleanup console storage subsystem
 */
void console_storage_cleanup(void);

/**
 * Save console cache to persistent storage
 */
VitaRPS5Result console_storage_save(const ConsoleCacheData* cache);

/**
 * Load console cache from persistent storage
 * Returns success even if file doesn't exist (empty cache)
 */
VitaRPS5Result console_storage_load(ConsoleCacheData* cache);

/**
 * Check if console cache file exists
 */
bool console_storage_exists(void);

/**
 * Get the size of the console cache file in bytes
 */
VitaRPS5Result console_storage_get_file_size(size_t* size);

// Console Management Operations

/**
 * Add a new console to the cache
 * Converts PS5ConsoleInfo to UIConsoleInfo and adds to cache
 */
VitaRPS5Result console_storage_add_console(ConsoleCacheData* cache,
                                           const PS5ConsoleInfo* console);

/**
 * Remove console from cache by host ID
 */
VitaRPS5Result console_storage_remove_console(ConsoleCacheData* cache,
                                              const char* host_id);

/**
 * Update existing console information
 */
VitaRPS5Result console_storage_update_console(ConsoleCacheData* cache,
                                              const UIConsoleInfo* console);

/**
 * Find console in cache by host ID
 */
VitaRPS5Result console_storage_find_console(const ConsoleCacheData* cache,
                                            const char* host_id,
                                            UIConsoleInfo** console);

/**
 * Find console by IP address (loads cache automatically)
 */
VitaRPS5Result console_storage_find_by_ip(const char* ip_address,
                                          UIConsoleInfo* console_info);

/**
 * Get console by index
 */
VitaRPS5Result console_storage_get_console(const ConsoleCacheData* cache,
                                           uint32_t index,
                                           UIConsoleInfo** console);

/**
 * Check if console exists in cache by host ID
 */
bool console_storage_console_exists(const ConsoleCacheData* cache,
                                    const char* host_id);

// Cache Validation and Maintenance

/**
 * Validate cache integrity and version compatibility
 */
VitaRPS5Result console_storage_validate_cache(ConsoleCacheData* cache);

/**
 * Clear all consoles from cache
 */
VitaRPS5Result console_storage_clear_cache(ConsoleCacheData* cache);

/**
 * Check if cache has expired based on last_updated timestamp
 */
bool console_storage_is_cache_expired(const ConsoleCacheData* cache);

/**
 * Clean up invalid or mock-registered consoles from cache
 * Returns number of consoles removed
 */
VitaRPS5Result console_storage_cleanup_invalid_consoles(
    ConsoleCacheData* cache, uint32_t* removed_count);

/**
 * Get cache statistics
 */
VitaRPS5Result console_storage_get_stats(const ConsoleCacheData* cache,
                                         uint32_t* total_consoles,
                                         uint32_t* favorite_consoles,
                                         uint64_t* oldest_console,
                                         uint64_t* newest_console);

// Console Information Utilities

/**
 * Convert PS5ConsoleInfo to UIConsoleInfo
 * Note: Function implemented in console_storage.c, but not declared here to
 * avoid circular dependency
 */
// VitaRPS5Result console_storage_convert_ps5_info(const PS5ConsoleInfo*
// ps5_info,
//                                                 UIConsoleInfo* ui_info);

/**
 * Convert UIConsoleInfo to PS5ConsoleInfo
 * Note: Function implemented in console_storage.c, but not declared here to
 * avoid circular dependency
 */
// VitaRPS5Result console_storage_convert_to_ps5_info(const UIConsoleInfo*
// ui_info,
//                                                    PS5ConsoleInfo* ps5_info);

/**
 * Generate display name for console
 * Creates friendly name like "Living Room PS5" or "Bedroom PS4"
 */
VitaRPS5Result console_storage_generate_display_name(
    const UIConsoleInfo* console, char* display_name, size_t max_len);

/**
 * Update console last seen timestamp
 */
VitaRPS5Result console_storage_touch_console(ConsoleCacheData* cache,
                                             const char* host_id);

/**
 * Update console connection statistics
 */
VitaRPS5Result console_storage_update_connection_stats(ConsoleCacheData* cache,
                                                       const char* host_id,
                                                       bool successful);

// Console Sorting and Organization

/**
 * Sort consoles in cache by various criteria
 */
typedef enum {
  CONSOLE_SORT_BY_NAME,            // Alphabetical by display name
  CONSOLE_SORT_BY_TYPE,            // By console type (PS5, PS4, etc.)
  CONSOLE_SORT_BY_LAST_SEEN,       // Most recently discovered first
  CONSOLE_SORT_BY_LAST_CONNECTED,  // Most recently connected first
  CONSOLE_SORT_BY_FAVORITES,       // Favorites first, then by name
  CONSOLE_SORT_BY_STATUS           // Available first, then by name
} ConsoleSortCriteria;

VitaRPS5Result console_storage_sort_consoles(ConsoleCacheData* cache,
                                             ConsoleSortCriteria criteria);

/**
 * Get consoles filtered by criteria
 */
VitaRPS5Result console_storage_filter_consoles(const ConsoleCacheData* cache,
                                               ConsoleCacheData* filtered_cache,
                                               bool favorites_only,
                                               PSConsoleType type_filter);

// Backup and Recovery

/**
 * Create backup of console cache
 */
VitaRPS5Result console_storage_create_backup(const char* backup_path);

/**
 * Restore console cache from backup
 */
VitaRPS5Result console_storage_restore_backup(const char* backup_path);

/**
 * Export console list to text format for debugging
 */
VitaRPS5Result console_storage_export_text(const ConsoleCacheData* cache,
                                           const char* export_path);

// Error Handling and Debugging

/**
 * Convert ConsoleStorageResult to string for logging
 */
const char* console_storage_result_string(ConsoleStorageResult result);

/**
 * Convert VitaRPS5Result to ConsoleStorageResult
 */
ConsoleStorageResult console_storage_from_vitarps5_result(
    VitaRPS5Result result);

/**
 * Convert ConsoleStorageResult to VitaRPS5Result
 */
VitaRPS5Result console_storage_to_vitarps5_result(ConsoleStorageResult result);

/**
 * Log cache statistics for debugging
 */
void console_storage_log_cache_info(const ConsoleCacheData* cache);

// Default Configuration

/**
 * Initialize cache with default settings
 */
VitaRPS5Result console_storage_init_default_cache(ConsoleCacheData* cache);

/**
 * Create default UIConsoleInfo from PS5ConsoleInfo
 * Note: Function implemented in console_storage.c, but not declared here to
 * avoid circular dependency
 */
// VitaRPS5Result console_storage_create_default_ui_info(
//     const PS5ConsoleInfo* ps5_info, UIConsoleInfo* ui_info);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_CONSOLE_STORAGE_H