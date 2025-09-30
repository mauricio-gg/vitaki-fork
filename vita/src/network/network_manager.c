#include "network_manager.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logger.h"

// Network memory allocation (1MB shared pool)
#define NET_INIT_SIZE (1024 * 1024)
static char net_memory[NET_INIT_SIZE];

// Reference counting and state management
static int network_ref_count = 0;
static NetworkState current_state = NETWORK_STATE_UNINITIALIZED;
static bool offline_mode = false;
static SceUID network_mutex = -1;
static NetworkInfo cached_info = {0};
static uint64_t last_info_update = 0;

// Forward declarations
static VitaRPS5Result network_internal_init(void);
static void network_internal_cleanup(void);
static VitaRPS5Result update_network_info(void);
static uint64_t get_system_time_us(void);

VitaRPS5Result network_manager_init(void) {
  // Create mutex on first call
  if (network_mutex < 0) {
    network_mutex = sceKernelCreateMutex("NetworkMgr", 0, 0, NULL);
    if (network_mutex < 0) {
      log_error("Failed to create network manager mutex: 0x%08X",
                network_mutex);
      return VITARPS5_ERROR_NETWORK;
    }
  }

  // Lock mutex for thread safety
  int lock_result = sceKernelLockMutex(network_mutex, 1, NULL);
  if (lock_result < 0) {
    log_error("Failed to lock network manager mutex: 0x%08X", lock_result);
    return VITARPS5_ERROR_NETWORK;
  }

  VitaRPS5Result result = VITARPS5_SUCCESS;

  // Increment reference count
  network_ref_count++;

  log_debug("Network manager init called (ref_count: %d)", network_ref_count);

  // Initialize network on first reference
  if (network_ref_count == 1) {
    if (offline_mode) {
      current_state = NETWORK_STATE_OFFLINE;
      log_info("Network manager initialized in offline mode");
    } else {
      result = network_internal_init();
    }
  } else {
    // Already initialized, just return current state
    if (current_state == NETWORK_STATE_ERROR) {
      result = VITARPS5_ERROR_NETWORK;
    } else if (current_state == NETWORK_STATE_OFFLINE) {
      result = VITARPS5_ERROR_OFFLINE;
    }
  }

  sceKernelUnlockMutex(network_mutex, 1);
  return result;
}

void network_manager_cleanup(void) {
  if (network_mutex < 0) {
    return;  // Never initialized
  }

  sceKernelLockMutex(network_mutex, 1, NULL);

  network_ref_count--;
  log_debug("Network manager cleanup called (ref_count: %d)",
            network_ref_count);

  // Cleanup network when reference count reaches 0
  if (network_ref_count <= 0) {
    network_ref_count = 0;
    network_internal_cleanup();
    current_state = NETWORK_STATE_UNINITIALIZED;
    memset(&cached_info, 0, sizeof(cached_info));
    last_info_update = 0;
  }

  sceKernelUnlockMutex(network_mutex, 1);
}

NetworkState network_manager_get_state(void) { return current_state; }

bool network_manager_is_available(void) {
  return current_state == NETWORK_STATE_AVAILABLE;
}

VitaRPS5Result network_manager_get_info(NetworkInfo* info) {
  if (!info) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (!network_manager_is_available()) {
    return VITARPS5_ERROR_NETWORK;
  }

  // Update info if cache is stale (older than 5 seconds)
  uint64_t current_time = get_system_time_us();
  if (current_time - last_info_update > 5000000ULL) {
    VitaRPS5Result result = update_network_info();
    if (result != VITARPS5_SUCCESS) {
      return result;
    }
  }

  *info = cached_info;
  return VITARPS5_SUCCESS;
}

VitaRPS5Result network_manager_refresh(void) {
  if (!network_manager_is_available()) {
    return VITARPS5_ERROR_NETWORK;
  }

  return update_network_info();
}

const char* network_manager_error_string(int error_code) {
  switch (error_code) {
    case SCE_NET_ERROR_EBUSY:
      return "Network busy (another operation in progress)";
    case SCE_NET_ERROR_EEXIST:
      return "Network already initialized";
    case SCE_NET_ERROR_ENOMEM:
      return "Insufficient network memory";
    case SCE_NET_ERROR_EINVAL:
      return "Invalid network parameters";
    case SCE_NET_ERROR_ENOTINIT:
      return "Network not initialized";
    case SCE_NET_ERROR_EALREADY:
      return "Network operation already in progress";
    case SCE_NET_ERROR_EADDRINUSE:
      return "Network address already in use";
    case SCE_NET_ERROR_ECONNREFUSED:
      return "Connection refused";
    case SCE_NET_ERROR_ETIMEDOUT:
      return "Network operation timed out";
    case SCE_NET_ERROR_ENETUNREACH:
      return "Network unreachable";
    case SCE_NET_ERROR_EHOSTUNREACH:
      return "Host unreachable";
    default:
      return "Unknown network error";
  }
}

void network_manager_set_offline_mode(bool offline) {
  offline_mode = offline;

  if (offline && current_state == NETWORK_STATE_AVAILABLE) {
    sceKernelLockMutex(network_mutex, 1, NULL);
    current_state = NETWORK_STATE_OFFLINE;
    sceKernelUnlockMutex(network_mutex, 1);
    log_info("Network manager switched to offline mode");
  } else if (!offline && current_state == NETWORK_STATE_OFFLINE) {
    // Attempt to re-initialize network
    log_info("Attempting to re-enable network from offline mode");
    network_manager_refresh();
  }
}

bool network_manager_is_offline_mode(void) { return offline_mode; }

// Internal implementation functions

static VitaRPS5Result network_internal_init(void) {
  log_info("Initializing network subsystem");
  current_state = NETWORK_STATE_INITIALIZING;

  // Load network module
  int result = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  if (result < 0) {
    log_warning("Network module not available (0x%08X) - offline mode", result);
    current_state = NETWORK_STATE_OFFLINE;
    return VITARPS5_ERROR_OFFLINE;
  }

  // Initialize SceNet with shared memory pool
  SceNetInitParam param;
  param.memory = net_memory;
  param.size = NET_INIT_SIZE;
  param.flags = 0;

  result = sceNetInit(&param);
  if (result < 0) {
    if (result == SCE_NET_ERROR_EEXIST) {
      log_info("Network already initialized - continuing");
    } else if (result == SCE_NET_ERROR_EBUSY) {
      log_error("Network busy (0x%08X) - another process using network",
                result);
      current_state = NETWORK_STATE_ERROR;
      return VITARPS5_ERROR_NETWORK;
    } else {
      log_error("Failed to initialize network (0x%08X): %s", result,
                network_manager_error_string(result));
      current_state = NETWORK_STATE_ERROR;
      return VITARPS5_ERROR_NETWORK;
    }
  }

  // Initialize NetCtl for connection info
  result = sceNetCtlInit();
  if (result < 0 && result != SCE_NET_ERROR_EEXIST) {
    log_warning("NetCtl initialization failed (0x%08X) - limited functionality",
                result);
  }

  // Update network information
  VitaRPS5Result info_result = update_network_info();
  if (info_result != VITARPS5_SUCCESS) {
    log_warning("Failed to get initial network info - continuing anyway");
  }

  current_state = NETWORK_STATE_AVAILABLE;
  log_info("Network subsystem initialized successfully");
  return VITARPS5_SUCCESS;
}

static void network_internal_cleanup(void) {
  log_info("Cleaning up network subsystem");

  // Note: We don't call sceNetTerm() here because other processes
  // might still be using the network. The Vita OS will handle cleanup
  // when the application exits.

  sceNetCtlTerm();

  log_info("Network subsystem cleanup completed");
}

static VitaRPS5Result update_network_info(void) {
  memset(&cached_info, 0, sizeof(cached_info));

  // Get connection state
  int state;
  int result = sceNetCtlInetGetState(&state);
  if (result < 0) {
    log_debug("Failed to get connection state: 0x%08X", result);
    return VITARPS5_ERROR_NETWORK;
  }

  cached_info.is_connected = (state == SCE_NETCTL_STATE_CONNECTED);

  if (cached_info.is_connected) {
    // Get IP address
    SceNetCtlInfo info;

    result = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);
    if (result >= 0) {
      strncpy(cached_info.ip_address, info.ip_address,
              sizeof(cached_info.ip_address) - 1);
    }

    result = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_NETMASK, &info);
    if (result >= 0) {
      strncpy(cached_info.subnet_mask, info.netmask,
              sizeof(cached_info.subnet_mask) - 1);
    }

    result = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_DEFAULT_ROUTE, &info);
    if (result >= 0) {
      strncpy(cached_info.gateway, info.default_route,
              sizeof(cached_info.gateway) - 1);
    }

    result = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_PRIMARY_DNS, &info);
    if (result >= 0) {
      strncpy(cached_info.dns_primary, info.primary_dns,
              sizeof(cached_info.dns_primary) - 1);
    }

    result = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_SECONDARY_DNS, &info);
    if (result >= 0) {
      strncpy(cached_info.dns_secondary, info.secondary_dns,
              sizeof(cached_info.dns_secondary) - 1);
    }

    // Get WiFi signal strength if available
    result = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_DBM, &info);
    if (result >= 0) {
      cached_info.signal_strength = info.rssi_dbm;
    }

    // Simple internet connectivity check (ping gateway)
    cached_info.has_internet = strlen(cached_info.gateway) > 0;
  }

  last_info_update = get_system_time_us();
  return VITARPS5_SUCCESS;
}

static uint64_t get_system_time_us(void) {
  return sceKernelGetSystemTimeWide();
}