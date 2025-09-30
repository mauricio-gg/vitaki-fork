#include "vita_system_info.h"

#include <psp2/apputil.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/power.h>
#include <psp2/registrymgr.h>
#include <psp2/system_param.h>
#include <stdio.h>
#include <string.h>

#include "../network/network_manager.h"
#include "../utils/logger.h"

// Static system information cache
static VitaSystemInfo cached_system_info = {0};
static VitaNetworkInfo cached_network_info = {0};
static bool system_info_initialized = false;
static bool network_initialized = false;

// Internal helper functions
static VitaRPS5Result detect_vita_model(char* model_name, size_t size);
static VitaRPS5Result get_firmware_version(char* version, size_t size);
static VitaRPS5Result get_user_info(char* user_name, size_t size);
static VitaRPS5Result get_profile_image(VitaSystemInfo* info);
static VitaRPS5Result get_system_params(VitaSystemInfo* info);
static VitaRPS5Result get_memory_info(VitaSystemInfo* info);
static VitaRPS5Result get_power_info(VitaSystemInfo* info);
static VitaRPS5Result get_network_status(VitaNetworkInfo* info);

VitaRPS5Result vita_system_info_init(void) {
  log_info("Initializing Vita system information module");

  // Use centralized network manager
  VitaRPS5Result result = network_manager_init();
  if (result != VITARPS5_SUCCESS) {
    if (result == VITARPS5_ERROR_OFFLINE) {
      log_warning("Network not available for system info module");
    } else {
      log_warning("Network manager initialization failed: %d", result);
    }
    // Continue without network - system info can work without it
  }
  network_initialized = network_manager_is_available();

  // Gather initial system information
  memset(&cached_system_info, 0, sizeof(cached_system_info));
  memset(&cached_network_info, 0, sizeof(cached_network_info));

  result = vita_system_info_get_system(&cached_system_info);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to get initial system information");
    return result;
  }

  result = vita_system_info_get_network(&cached_network_info);
  if (result != VITARPS5_SUCCESS) {
    log_warning(
        "Failed to get initial network information (continuing anyway)");
  }

  system_info_initialized = true;
  log_info("Vita system information module initialized successfully");
  return VITARPS5_SUCCESS;
}

void vita_system_info_cleanup(void) {
  log_info("Cleaning up Vita system information module");

  // Network cleanup is handled by network manager
  // No direct network cleanup needed here

  system_info_initialized = false;
  network_initialized = false;
  memset(&cached_system_info, 0, sizeof(cached_system_info));
  memset(&cached_network_info, 0, sizeof(cached_network_info));
}

VitaRPS5Result vita_system_info_get_system(VitaSystemInfo* info) {
  if (!info) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // If we have cached info and system is initialized, return cached data
  if (system_info_initialized) {
    *info = cached_system_info;
    return VITARPS5_SUCCESS;
  }

  memset(info, 0, sizeof(VitaSystemInfo));

  // Get device model
  VitaRPS5Result result =
      detect_vita_model(info->model_name, sizeof(info->model_name));
  if (result != VITARPS5_SUCCESS) {
    log_warning("Failed to detect Vita model, using fallback");
    strncpy(info->model_name, "PS Vita", sizeof(info->model_name) - 1);
  }

  // Get firmware version
  result = get_firmware_version(info->firmware_version,
                                sizeof(info->firmware_version));
  if (result != VITARPS5_SUCCESS) {
    log_warning("Failed to get firmware version, using fallback");
    strncpy(info->firmware_version, "Unknown",
            sizeof(info->firmware_version) - 1);
  }

  // Get user information
  result = get_user_info(info->user_name, sizeof(info->user_name));
  if (result != VITARPS5_SUCCESS) {
    log_warning("Failed to get user info, using fallback");
    strncpy(info->user_name, "Vita User", sizeof(info->user_name) - 1);
  }

  // Get profile image
  get_profile_image(info);

  // Get system parameters
  get_system_params(info);

  // Get memory information
  get_memory_info(info);

  // Get power information
  get_power_info(info);

  // Cache the results
  cached_system_info = *info;

  log_info("Retrieved system info - Model: %s, FW: %s, User: %s",
           info->model_name, info->firmware_version, info->user_name);

  return VITARPS5_SUCCESS;
}

VitaRPS5Result vita_system_info_get_network(VitaNetworkInfo* info) {
  if (!info) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  memset(info, 0, sizeof(VitaNetworkInfo));

  VitaRPS5Result result = get_network_status(info);
  if (result != VITARPS5_SUCCESS) {
    log_warning("Failed to get network status");
    // Fill with defaults
    strncpy(info->ip_address, "Not Connected", sizeof(info->ip_address) - 1);
    strncpy(info->connection_name, "None", sizeof(info->connection_name) - 1);
    info->is_connected = false;
    info->connection_type = 0;
    info->signal_strength = -1;
    info->nat_type = -1;
  }

  // Cache the results
  cached_network_info = *info;

  return VITARPS5_SUCCESS;
}

const char* vita_system_info_get_model_display_name(void) {
  if (system_info_initialized) {
    return cached_system_info.model_name;
  }
  return "PS Vita";
}

const char* vita_system_info_get_connection_type_string(int type) {
  switch (type) {
    case 0:
      return "Disconnected";
    case 1:
      return "WiFi";
    case 2:
      return "3G/Mobile";
    case 3:
      return "Ethernet";
    default:
      return "Unknown";
  }
}

bool vita_system_info_is_pstv(void) {
  if (system_info_initialized) {
    return strstr(cached_system_info.model_name, "PS TV") != NULL;
  }

  // Try to detect if we're running on PS TV
  // PS TV typically has different memory layout and capabilities
  char model[32];
  if (detect_vita_model(model, sizeof(model)) == VITARPS5_SUCCESS) {
    return strstr(model, "PS TV") != NULL;
  }

  return false;
}

VitaRPS5Result vita_system_info_refresh_network(void) {
  return vita_system_info_get_network(&cached_network_info);
}

VitaRPS5Result vita_system_info_refresh_memory(void) {
  return get_memory_info(&cached_system_info);
}

VitaRPS5Result vita_system_info_refresh_battery(void) {
  return get_power_info(&cached_system_info);
}

// Internal helper function implementations
static VitaRPS5Result detect_vita_model(char* model_name, size_t size) {
  // Try to detect model based on system capabilities and memory layout
  // This is a best-effort detection since there's no direct API

  uint32_t total_memory = 0;
  SceKernelFreeMemorySizeInfo info;
  info.size = sizeof(info);

  int ret = sceKernelGetFreeMemorySize(&info);
  if (ret >= 0) {
    total_memory = info.size_user + info.size_cdram + info.size_phycont;
  }

  // Try to detect PS TV by checking for specific capabilities
  // PS TV typically has more memory and different hardware
  if (total_memory > 400 * 1024 * 1024) {  // > 400MB suggests PS TV
    strncpy(model_name, "PlayStation TV", size - 1);
  } else {
    // Default to PS Vita - could potentially detect PCH-1000 vs PCH-2000
    // but that requires more complex detection
    strncpy(model_name, "PS Vita", size - 1);
  }

  model_name[size - 1] = '\0';
  return VITARPS5_SUCCESS;
}

static VitaRPS5Result get_firmware_version(char* version, size_t size) {
  // Try to get system software version - this function may not be available in
  // homebrew For now, provide a fallback
  strncpy(version, "3.XX", size - 1);
  version[size - 1] = '\0';

  // Note: sceKernelGetSystemSwVersion may not be available in homebrew context
  // This would require research into alternative methods to get firmware
  // version
  log_info("Firmware version detection not fully implemented - using fallback");

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result get_user_info(char* user_name, size_t size) {
  if (!user_name || size == 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Attempting to load user information from system");

  // Method 1: Try to get user info from App Util (most reliable for homebrew)
  SceAppUtilInitParam init_param;
  SceAppUtilBootParam boot_param;
  memset(&init_param, 0, sizeof(init_param));
  memset(&boot_param, 0, sizeof(boot_param));

  int ret = sceAppUtilInit(&init_param, &boot_param);
  if (ret >= 0) {
    // Try to get PlayStation Network account ID first (most likely to have real
    // name)
    SceAppUtilAppEventParam event_param;
    memset(&event_param, 0, sizeof(event_param));

    ret = sceAppUtilReceiveAppEvent(&event_param);
    if (ret >= 0) {
      log_info("App Util event received, checking for user data");
    }

    sceAppUtilShutdown();
  }

  // Method 2: Try to get user name from registry (fallback)
  char registry_user_name[64];
  memset(registry_user_name, 0, sizeof(registry_user_name));

  // Try multiple registry paths where user information might be stored
  const char* registry_paths[] = {
      "/CONFIG/SYSTEM/username", "/CONFIG/NP/np_onlineid",
      "/CONFIG/NP/account_name", "/CONFIG/SYSTEM/nickname"};

  bool user_found = false;
  for (size_t i = 0; i < sizeof(registry_paths) / sizeof(registry_paths[0]);
       i++) {
    ret = sceRegMgrGetKeyStr("/", registry_paths[i], registry_user_name,
                             sizeof(registry_user_name));
    if (ret >= 0 && strlen(registry_user_name) > 0) {
      log_info("Found user name in registry path %s: '%s'", registry_paths[i],
               registry_user_name);
      strncpy(user_name, registry_user_name, size - 1);
      user_name[size - 1] = '\0';
      user_found = true;
      break;
    }
  }

  // Method 3: Try to get PSN Online ID as username
  if (!user_found) {
    char psn_online_id[32];
    memset(psn_online_id, 0, sizeof(psn_online_id));

    ret = sceRegMgrGetKeyStr("/CONFIG/NP/", "onlineid", psn_online_id,
                             sizeof(psn_online_id));
    if (ret >= 0 && strlen(psn_online_id) > 0) {
      log_info("Found PSN Online ID: '%s'", psn_online_id);
      strncpy(user_name, psn_online_id, size - 1);
      user_name[size - 1] = '\0';
      user_found = true;
    }
  }

  // Method 4: Try system parameter for username
  if (!user_found) {
    char username_buf[64];
    memset(username_buf, 0, sizeof(username_buf));

    // Note: This may not work in homebrew context, but worth trying
    ret = sceAppUtilSystemParamGetString(SCE_SYSTEM_PARAM_ID_USERNAME,
                                         username_buf, sizeof(username_buf));
    if (ret >= 0 && strlen(username_buf) > 0) {
      log_info("Found system username: '%s'", username_buf);
      strncpy(user_name, username_buf, size - 1);
      user_name[size - 1] = '\0';
      user_found = true;
    }
  }

  if (!user_found) {
    log_warning("No user information found in system, using fallback");
    strncpy(user_name, "Vita User", size - 1);
    user_name[size - 1] = '\0';
    return VITARPS5_ERROR_NOT_FOUND;
  }

  log_info("Successfully loaded user information: '%s'", user_name);
  return VITARPS5_SUCCESS;
}

static VitaRPS5Result get_profile_image(VitaSystemInfo* info) {
  if (!info) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Attempting to locate user profile image");

  // Initialize profile image fields
  info->has_profile_image = false;
  memset(info->profile_image_path, 0, sizeof(info->profile_image_path));

  // PS Vita stores profile images in specific locations
  // Common paths where profile images might be stored:
  const char* profile_image_paths[] = {
      "ur0:user/00/np/myprofile.png",               // NPM profile picture
      "ur0:user/00/np/profile.png",                 // Alternative NPM path
      "ux0:data/profile/avatar.png",                // Custom profile path
      "ux0:data/VitaShell/profile.png",             // VitaShell custom
      "ur0:user/00/trophy/data/sce_sys/icon0.png",  // Trophy profile icon
      "ur0:appmeta/NPXS10015/icon0.png",  // Settings app icon as fallback
  };

  // Try to find profile image in known locations
  for (size_t i = 0;
       i < sizeof(profile_image_paths) / sizeof(profile_image_paths[0]); i++) {
    // Check if file exists (using SceIoStat would be ideal but requires
    // additional headers) For now, we'll just set the most likely path
    if (i == 0) {  // Try the first path as most likely
      strncpy(info->profile_image_path, profile_image_paths[i],
              sizeof(info->profile_image_path) - 1);
      info->profile_image_path[sizeof(info->profile_image_path) - 1] = '\0';
      info->has_profile_image = true;  // Assume it exists for now
      log_info("Set profile image path to: %s", info->profile_image_path);
      break;
    }
  }

  if (!info->has_profile_image) {
    log_info("No profile image path configured");
  }

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result get_system_params(VitaSystemInfo* info) {
  // Get system language - using registry manager as alternative
  // Note: sceSystemParamGetInt may not be available in homebrew
  info->language = 1;             // Default to English
  info->enter_button_assign = 1;  // Default to Cross

  // TODO: Research alternative methods to get system parameters in homebrew
  log_info("System parameters detection using fallback values");

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result get_memory_info(VitaSystemInfo* info) {
  SceKernelFreeMemorySizeInfo mem_info;
  mem_info.size = sizeof(mem_info);

  int ret = sceKernelGetFreeMemorySize(&mem_info);
  if (ret >= 0) {
    // Total memory is free + used, approximate from available info
    uint32_t free_bytes =
        mem_info.size_user + mem_info.size_cdram + mem_info.size_phycont;
    info->free_memory_mb = free_bytes / (1024 * 1024);

    // PS Vita has approximately 512MB total (varies by model)
    // PS TV has more
    info->total_memory_mb = vita_system_info_is_pstv() ? 1024 : 512;

    return VITARPS5_SUCCESS;
  } else {
    log_warning("Failed to get memory info: 0x%08X", ret);
    info->total_memory_mb = 512;  // Default assumption
    info->free_memory_mb = 256;   // Conservative estimate
    return VITARPS5_ERROR_SYSTEM_CALL;
  }
}

static VitaRPS5Result get_power_info(VitaSystemInfo* info) {
  // Get battery percentage
  info->battery_percent = scePowerGetBatteryLifePercent();
  if (info->battery_percent < 0) {
    info->battery_percent = -1;  // Not available (e.g., PS TV)
  }

  // Check if charging
  info->is_charging = scePowerIsPowerOnline() != 0;

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result get_network_status(VitaNetworkInfo* info) {
  if (!network_manager_is_available()) {
    log_warning("Network manager not available");
    info->is_connected = false;
    strncpy(info->ip_address, "Not Available", sizeof(info->ip_address) - 1);
    strncpy(info->connection_name, "None", sizeof(info->connection_name) - 1);
    info->connection_type = 0;
    info->signal_strength = -1;
    info->nat_type = -1;
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  // Use network manager to get network status
  NetworkInfo status;
  VitaRPS5Result result = network_manager_get_info(&status);
  if (result != VITARPS5_SUCCESS) {
    log_warning("Failed to get network status from network manager: %d",
                result);
    info->is_connected = false;
    strncpy(info->ip_address, "Error", sizeof(info->ip_address) - 1);
    strncpy(info->connection_name, "Error", sizeof(info->connection_name) - 1);
    info->connection_type = 0;
    info->signal_strength = -1;
    info->nat_type = -1;
    return result;
  }

  // Map network manager status to system info structure
  info->is_connected = status.is_connected;

  if (info->is_connected) {
    // Copy network information from network manager
    strncpy(info->ip_address, status.ip_address, sizeof(info->ip_address) - 1);
    strncpy(info->netmask, status.subnet_mask, sizeof(info->netmask) - 1);
    strncpy(info->gateway, status.gateway, sizeof(info->gateway) - 1);
    strncpy(info->dns_primary, status.dns_primary,
            sizeof(info->dns_primary) - 1);

    // Determine connection type and name
    if (vita_system_info_is_pstv()) {
      info->connection_type = 3;  // Ethernet for PS TV
      strncpy(info->connection_name, "Ethernet",
              sizeof(info->connection_name) - 1);
    } else {
      info->connection_type = 1;  // WiFi for PS Vita
      strncpy(info->connection_name, "WiFi", sizeof(info->connection_name) - 1);
    }

    // Use signal strength from network manager if available
    info->signal_strength =
        status.signal_strength >= 0 ? status.signal_strength : 75;

    // NAT type detection would require more complex network testing
    info->nat_type = -1;  // Unknown for now
  } else {
    strncpy(info->ip_address, "Not Connected", sizeof(info->ip_address) - 1);
    strncpy(info->connection_name, "None", sizeof(info->connection_name) - 1);
    info->connection_type = 0;
    info->signal_strength = -1;
    info->nat_type = -1;
  }

  // Ensure all strings are null-terminated
  info->ip_address[sizeof(info->ip_address) - 1] = '\0';
  info->netmask[sizeof(info->netmask) - 1] = '\0';
  info->gateway[sizeof(info->gateway) - 1] = '\0';
  info->dns_primary[sizeof(info->dns_primary) - 1] = '\0';
  info->connection_name[sizeof(info->connection_name) - 1] = '\0';

  return VITARPS5_SUCCESS;
}

VitaRPS5Result vita_system_info_get_profile_image_path(char* path,
                                                       size_t size) {
  if (!path || size == 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (system_info_initialized && cached_system_info.has_profile_image) {
    strncpy(path, cached_system_info.profile_image_path, size - 1);
    path[size - 1] = '\0';
    return VITARPS5_SUCCESS;
  }

  return VITARPS5_ERROR_NOT_FOUND;
}

bool vita_system_info_has_profile_image(void) {
  return system_info_initialized && cached_system_info.has_profile_image;
}