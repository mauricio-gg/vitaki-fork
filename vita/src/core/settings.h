#ifndef VITARPS5_SETTINGS_H
#define VITARPS5_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

// Settings file path on Vita
#define VITARPS5_SETTINGS_PATH "ux0:data/VitaRPS5/settings.cfg"
#define VITARPS5_SETTINGS_DIR "ux0:data/VitaRPS5"

// Settings validation result
typedef enum {
  SETTINGS_VALID = 0,
  SETTINGS_INVALID_RESOLUTION,
  SETTINGS_INVALID_FRAMERATE,
  SETTINGS_INVALID_BITRATE,
  SETTINGS_INVALID_DEADZONE,
  SETTINGS_INVALID_SENSITIVITY,
  SETTINGS_INVALID_MTU
} SettingsValidationResult;

// Persistent settings structure that maps to UI
typedef struct {
  // Streaming Quality settings
  VitaRPS5Quality quality_preset;
  uint32_t resolution_width;   // 720, 540
  uint32_t resolution_height;  // 480, 360
  uint32_t target_fps;         // 30, 60
  uint32_t target_bitrate;     // 1000-15000 kbps
  bool hardware_decode;

  // Video settings
  bool hdr_support;
  bool vsync_enabled;

  // Network settings
  bool auto_connect;
  bool wake_on_lan;
  uint32_t mtu_size;  // 1200-1500

  // Controller settings
  bool motion_controls;
  bool touch_controls;
  float deadzone_percent;     // 0.0-50.0
  float sensitivity_percent;  // 50.0-150.0
  char button_mapping[32];    // "Default", "Custom"

  // Internal tracking
  uint32_t settings_version;  // For future compatibility
  bool settings_loaded;       // Whether settings were loaded from file
} VitaRPS5Settings;

// Settings API functions

/**
 * Initialize settings system and load from file
 */
VitaRPS5Result vitarps5_settings_init(void);

/**
 * Save settings to persistent storage
 */
VitaRPS5Result vitarps5_settings_save(void);

/**
 * Load settings from persistent storage
 */
VitaRPS5Result vitarps5_settings_load(void);

/**
 * Get current settings
 */
const VitaRPS5Settings* vitarps5_settings_get(void);

/**
 * Set default settings
 */
void vitarps5_settings_set_defaults(VitaRPS5Settings* settings);

/**
 * Validate settings values
 */
SettingsValidationResult vitarps5_settings_validate(
    const VitaRPS5Settings* settings);

/**
 * Convert settings to VitaRPS5Config
 */
void vitarps5_settings_to_config(const VitaRPS5Settings* settings,
                                 VitaRPS5Config* config);

/**
 * Update individual setting (called from UI)
 */
VitaRPS5Result vitarps5_settings_set_quality_preset(VitaRPS5Quality preset);
VitaRPS5Result vitarps5_settings_set_resolution(uint32_t width,
                                                uint32_t height);
VitaRPS5Result vitarps5_settings_set_framerate(uint32_t fps);
VitaRPS5Result vitarps5_settings_set_bitrate(uint32_t bitrate);
VitaRPS5Result vitarps5_settings_set_hardware_decode(bool enabled);
VitaRPS5Result vitarps5_settings_set_hdr(bool enabled);
VitaRPS5Result vitarps5_settings_set_vsync(bool enabled);
VitaRPS5Result vitarps5_settings_set_auto_connect(bool enabled);
VitaRPS5Result vitarps5_settings_set_wake_on_lan(bool enabled);
VitaRPS5Result vitarps5_settings_set_mtu_size(uint32_t mtu);
VitaRPS5Result vitarps5_settings_set_motion_controls(bool enabled);
VitaRPS5Result vitarps5_settings_set_touch_controls(bool enabled);
VitaRPS5Result vitarps5_settings_set_deadzone(float percent);
VitaRPS5Result vitarps5_settings_set_sensitivity(float percent);

/**
 * Get setting values as strings for UI display
 */
const char* vitarps5_settings_get_quality_string(void);
const char* vitarps5_settings_get_resolution_string(void);
const char* vitarps5_settings_get_framerate_string(void);
const char* vitarps5_settings_get_bitrate_string(void);
const char* vitarps5_settings_get_deadzone_string(void);
const char* vitarps5_settings_get_sensitivity_string(void);
const char* vitarps5_settings_get_mtu_string(void);

/**
 * Get toggle states for UI
 */
bool vitarps5_settings_get_hardware_decode(void);
bool vitarps5_settings_get_hdr_support(void);
bool vitarps5_settings_get_vsync(void);
bool vitarps5_settings_get_auto_connect(void);
bool vitarps5_settings_get_wake_on_lan(void);
bool vitarps5_settings_get_motion_controls(void);
bool vitarps5_settings_get_touch_controls(void);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_SETTINGS_H