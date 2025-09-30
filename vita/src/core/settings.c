#include "settings.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logger.h"

// Global settings instance
static VitaRPS5Settings g_settings = {0};
static bool g_settings_initialized = false;

// String buffers for UI display (static to ensure persistence)
static char quality_str[32] = {0};
static char resolution_str[32] = {0};
static char framerate_str[32] = {0};
static char bitrate_str[32] = {0};
static char deadzone_str[32] = {0};
static char sensitivity_str[32] = {0};
static char mtu_str[32] = {0};

// Internal helper functions
static VitaRPS5Result create_settings_directory(void);
static void update_display_strings(void);

VitaRPS5Result vitarps5_settings_init(void) {
  log_info("Initializing settings system...");

  if (g_settings_initialized) {
    log_info("Settings already initialized");
    return VITARPS5_SUCCESS;
  }

  // Set defaults first
  vitarps5_settings_set_defaults(&g_settings);

  // Try to load from file
  VitaRPS5Result load_result = vitarps5_settings_load();
  if (load_result != VITARPS5_SUCCESS) {
    log_info("Failed to load settings, using defaults");
    // Save defaults to create the file
    vitarps5_settings_save();
  }

  // Update UI display strings
  update_display_strings();

  g_settings_initialized = true;
  log_info("Settings system initialized");
  return VITARPS5_SUCCESS;
}

void vitarps5_settings_set_defaults(VitaRPS5Settings* settings) {
  if (!settings) return;

  memset(settings, 0, sizeof(VitaRPS5Settings));

  // Streaming Quality defaults
  settings->quality_preset = VITARPS5_QUALITY_BALANCED;
  settings->resolution_width = 720;
  settings->resolution_height = 480;
  settings->target_fps = 60;
  settings->target_bitrate = 8000;
  settings->hardware_decode = true;

  // Video defaults
  settings->hdr_support = false;
  settings->vsync_enabled = true;

  // Network defaults
  settings->auto_connect = false;
  settings->wake_on_lan = false;
  settings->mtu_size = 1500;

  // Controller defaults
  settings->motion_controls = true;
  settings->touch_controls = true;
  settings->deadzone_percent = 15.0f;
  settings->sensitivity_percent = 85.0f;
  strcpy(settings->button_mapping, "Default");

  // Metadata
  settings->settings_version = 1;
  settings->settings_loaded = false;

  log_info("Settings set to defaults");
}

VitaRPS5Result vitarps5_settings_save(void) {
  log_info("Saving settings to %s", VITARPS5_SETTINGS_PATH);

  // Create directory if it doesn't exist
  VitaRPS5Result dir_result = create_settings_directory();
  if (dir_result != VITARPS5_SUCCESS) {
    log_error("Failed to create settings directory");
    return dir_result;
  }

  // Open file for writing
  SceUID fd = sceIoOpen(VITARPS5_SETTINGS_PATH,
                        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) {
    log_error("Failed to open settings file for writing: 0x%08X", fd);
    return VITARPS5_ERROR_INIT;
  }

  // Write settings structure
  int bytes_written = sceIoWrite(fd, &g_settings, sizeof(VitaRPS5Settings));
  sceIoClose(fd);

  if (bytes_written != sizeof(VitaRPS5Settings)) {
    log_error("Failed to write complete settings: %d bytes", bytes_written);
    return VITARPS5_ERROR_INIT;
  }

  log_info("Settings saved successfully (%d bytes)", bytes_written);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_load(void) {
  log_info("Loading settings from %s", VITARPS5_SETTINGS_PATH);

  // Check if file exists
  SceIoStat stat;
  if (sceIoGetstat(VITARPS5_SETTINGS_PATH, &stat) < 0) {
    log_info("Settings file does not exist");
    return VITARPS5_ERROR_INIT;
  }

  // Open file for reading
  SceUID fd = sceIoOpen(VITARPS5_SETTINGS_PATH, SCE_O_RDONLY, 0);
  if (fd < 0) {
    log_error("Failed to open settings file for reading: 0x%08X", fd);
    return VITARPS5_ERROR_INIT;
  }

  // Read settings structure
  VitaRPS5Settings loaded_settings;
  int bytes_read = sceIoRead(fd, &loaded_settings, sizeof(VitaRPS5Settings));
  sceIoClose(fd);

  if (bytes_read != sizeof(VitaRPS5Settings)) {
    log_error("Failed to read complete settings: %d bytes", bytes_read);
    return VITARPS5_ERROR_INIT;
  }

  // Validate loaded settings
  SettingsValidationResult validation =
      vitarps5_settings_validate(&loaded_settings);
  if (validation != SETTINGS_VALID) {
    log_error("Loaded settings are invalid: %d", validation);
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Copy loaded settings
  memcpy(&g_settings, &loaded_settings, sizeof(VitaRPS5Settings));
  g_settings.settings_loaded = true;

  log_info("Settings loaded successfully (%d bytes)", bytes_read);
  return VITARPS5_SUCCESS;
}

const VitaRPS5Settings* vitarps5_settings_get(void) { return &g_settings; }

SettingsValidationResult vitarps5_settings_validate(
    const VitaRPS5Settings* settings) {
  if (!settings) return SETTINGS_INVALID_RESOLUTION;

  // Validate resolution
  if (settings->resolution_width != 720 && settings->resolution_width != 540) {
    return SETTINGS_INVALID_RESOLUTION;
  }
  if (settings->resolution_height != 480 &&
      settings->resolution_height != 360) {
    return SETTINGS_INVALID_RESOLUTION;
  }

  // Validate framerate
  if (settings->target_fps != 30 && settings->target_fps != 60) {
    return SETTINGS_INVALID_FRAMERATE;
  }

  // Validate bitrate
  if (settings->target_bitrate < 1000 || settings->target_bitrate > 15000) {
    return SETTINGS_INVALID_BITRATE;
  }

  // Validate deadzone
  if (settings->deadzone_percent < 0.0f || settings->deadzone_percent > 50.0f) {
    return SETTINGS_INVALID_DEADZONE;
  }

  // Validate sensitivity
  if (settings->sensitivity_percent < 50.0f ||
      settings->sensitivity_percent > 150.0f) {
    return SETTINGS_INVALID_SENSITIVITY;
  }

  // Validate MTU
  if (settings->mtu_size < 1200 || settings->mtu_size > 1500) {
    return SETTINGS_INVALID_MTU;
  }

  return SETTINGS_VALID;
}

void vitarps5_settings_to_config(const VitaRPS5Settings* settings,
                                 VitaRPS5Config* config) {
  if (!settings || !config) return;

  // Copy video settings
  config->quality = settings->quality_preset;
  config->hardware_decode = settings->hardware_decode;
  config->target_bitrate = settings->target_bitrate;
  config->target_fps = settings->target_fps;
  config->width = settings->resolution_width;
  config->height = settings->resolution_height;

  // Copy video enhancement settings
  config->hdr_enabled = settings->hdr_support;
  config->vsync_enabled = settings->vsync_enabled;

  // Copy network settings
  config->auto_connect = settings->auto_connect;
  config->wake_on_lan = settings->wake_on_lan;
  config->mtu_size = settings->mtu_size;

  // Copy input settings
  config->motion_enabled = settings->motion_controls;
  config->rear_touch_enabled = settings->touch_controls;
  config->deadzone_percent = settings->deadzone_percent;
  config->sensitivity_percent = settings->sensitivity_percent;

  log_info(
      "Settings converted to config: %dx%d@%dfps, %dkbps, hw_decode=%d, "
      "hdr=%d, vsync=%d",
      config->width, config->height, config->target_fps, config->target_bitrate,
      config->hardware_decode, config->hdr_enabled, config->vsync_enabled);
}

// Individual setting setters
VitaRPS5Result vitarps5_settings_set_quality_preset(VitaRPS5Quality preset) {
  if (preset > VITARPS5_QUALITY_CUSTOM) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  g_settings.quality_preset = preset;

  // Auto-adjust other settings based on preset
  switch (preset) {
    case VITARPS5_QUALITY_PERFORMANCE:
      g_settings.target_fps = 30;
      g_settings.target_bitrate = 5000;
      break;
    case VITARPS5_QUALITY_BALANCED:
      g_settings.target_fps = 60;
      g_settings.target_bitrate = 8000;
      break;
    case VITARPS5_QUALITY_QUALITY:
      g_settings.target_fps = 60;
      g_settings.target_bitrate = 12000;
      break;
    case VITARPS5_QUALITY_CUSTOM:
      // Don't change other settings for custom
      break;
  }

  update_display_strings();
  log_info("Quality preset set to %d", preset);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_resolution(uint32_t width,
                                                uint32_t height) {
  if ((width != 720 && width != 540) || (height != 480 && height != 360)) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  g_settings.resolution_width = width;
  g_settings.resolution_height = height;
  update_display_strings();
  log_info("Resolution set to %dx%d", width, height);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_framerate(uint32_t fps) {
  if (fps != 30 && fps != 60) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  g_settings.target_fps = fps;
  update_display_strings();
  log_info("Framerate set to %d", fps);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_bitrate(uint32_t bitrate) {
  if (bitrate < 1000 || bitrate > 15000) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  g_settings.target_bitrate = bitrate;
  update_display_strings();
  log_info("Bitrate set to %d", bitrate);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_hardware_decode(bool enabled) {
  g_settings.hardware_decode = enabled;
  log_info("Hardware decode set to %d", enabled);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_hdr(bool enabled) {
  g_settings.hdr_support = enabled;
  log_info("HDR support set to %d", enabled);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_vsync(bool enabled) {
  g_settings.vsync_enabled = enabled;
  log_info("VSync set to %d", enabled);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_auto_connect(bool enabled) {
  g_settings.auto_connect = enabled;
  log_info("Auto connect set to %d", enabled);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_wake_on_lan(bool enabled) {
  g_settings.wake_on_lan = enabled;
  log_info("Wake on LAN set to %d", enabled);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_mtu_size(uint32_t mtu) {
  if (mtu < 1200 || mtu > 1500) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  g_settings.mtu_size = mtu;
  update_display_strings();
  log_info("MTU size set to %d", mtu);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_motion_controls(bool enabled) {
  g_settings.motion_controls = enabled;
  log_info("Motion controls set to %d", enabled);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_touch_controls(bool enabled) {
  g_settings.touch_controls = enabled;
  log_info("Touch controls set to %d", enabled);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_deadzone(float percent) {
  if (percent < 0.0f || percent > 50.0f) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  g_settings.deadzone_percent = percent;
  update_display_strings();
  log_info("Deadzone set to %.1f%%", percent);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_settings_set_sensitivity(float percent) {
  if (percent < 50.0f || percent > 150.0f) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  g_settings.sensitivity_percent = percent;
  update_display_strings();
  log_info("Sensitivity set to %.1f%%", percent);
  return VITARPS5_SUCCESS;
}

// String getters for UI display
const char* vitarps5_settings_get_quality_string(void) { return quality_str; }

const char* vitarps5_settings_get_resolution_string(void) {
  return resolution_str;
}

const char* vitarps5_settings_get_framerate_string(void) {
  return framerate_str;
}

const char* vitarps5_settings_get_bitrate_string(void) { return bitrate_str; }

const char* vitarps5_settings_get_deadzone_string(void) { return deadzone_str; }

const char* vitarps5_settings_get_sensitivity_string(void) {
  return sensitivity_str;
}

const char* vitarps5_settings_get_mtu_string(void) { return mtu_str; }

// Toggle getters
bool vitarps5_settings_get_hardware_decode(void) {
  return g_settings.hardware_decode;
}

bool vitarps5_settings_get_hdr_support(void) { return g_settings.hdr_support; }

bool vitarps5_settings_get_vsync(void) { return g_settings.vsync_enabled; }

bool vitarps5_settings_get_auto_connect(void) {
  return g_settings.auto_connect;
}

bool vitarps5_settings_get_wake_on_lan(void) { return g_settings.wake_on_lan; }

bool vitarps5_settings_get_motion_controls(void) {
  return g_settings.motion_controls;
}

bool vitarps5_settings_get_touch_controls(void) {
  return g_settings.touch_controls;
}

// Internal helper functions
static VitaRPS5Result create_settings_directory(void) {
  SceIoStat stat;
  if (sceIoGetstat(VITARPS5_SETTINGS_DIR, &stat) < 0) {
    // Directory doesn't exist, create it
    int result = sceIoMkdir(VITARPS5_SETTINGS_DIR, 0777);
    if (result < 0) {
      log_error("Failed to create settings directory: 0x%08X", result);
      return VITARPS5_ERROR_INIT;
    }
    log_info("Created settings directory: %s", VITARPS5_SETTINGS_DIR);
  }
  return VITARPS5_SUCCESS;
}

static void update_display_strings(void) {
  // Quality preset string
  switch (g_settings.quality_preset) {
    case VITARPS5_QUALITY_PERFORMANCE:
      strcpy(quality_str, "Performance");
      break;
    case VITARPS5_QUALITY_BALANCED:
      strcpy(quality_str, "Balanced");
      break;
    case VITARPS5_QUALITY_QUALITY:
      strcpy(quality_str, "Quality");
      break;
    case VITARPS5_QUALITY_CUSTOM:
      strcpy(quality_str, "Custom");
      break;
    default:
      strcpy(quality_str, "Balanced");
      break;
  }

  // Resolution string
  snprintf(resolution_str, sizeof(resolution_str), "%dx%d",
           g_settings.resolution_width, g_settings.resolution_height);

  // Framerate string
  snprintf(framerate_str, sizeof(framerate_str), "%d FPS",
           g_settings.target_fps);

  // Bitrate string
  snprintf(bitrate_str, sizeof(bitrate_str), "%d Kbps",
           g_settings.target_bitrate);

  // Deadzone string
  snprintf(deadzone_str, sizeof(deadzone_str), "%.0f%%",
           g_settings.deadzone_percent);

  // Sensitivity string
  snprintf(sensitivity_str, sizeof(sensitivity_str), "%.0f%%",
           g_settings.sensitivity_percent);

  // MTU string
  snprintf(mtu_str, sizeof(mtu_str), "%d", g_settings.mtu_size);
}