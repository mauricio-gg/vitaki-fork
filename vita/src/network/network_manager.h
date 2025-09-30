#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "../core/vitarps5.h"

/**
 * @file network_manager.h
 * @brief Centralized network management for VitaRPS5
 *
 * This module provides centralized network initialization and management
 * to prevent conflicts between multiple subsystems (Takion, Discovery, etc.)
 * that need network access.
 */

typedef enum {
  NETWORK_STATE_UNINITIALIZED = 0,
  NETWORK_STATE_INITIALIZING,
  NETWORK_STATE_AVAILABLE,
  NETWORK_STATE_ERROR,
  NETWORK_STATE_OFFLINE
} NetworkState;

typedef struct {
  bool is_connected;
  bool has_internet;
  char ip_address[16];
  char subnet_mask[16];
  char gateway[16];
  char dns_primary[16];
  char dns_secondary[16];
  int signal_strength;
} NetworkInfo;

/**
 * Initialize the network manager and underlying network subsystem
 * Uses reference counting to allow multiple subsystems to safely initialize
 *
 * @return VITARPS5_SUCCESS on success, error code on failure
 */
VitaRPS5Result network_manager_init(void);

/**
 * Cleanup network manager (decrements reference count)
 * Network is only torn down when reference count reaches 0
 */
void network_manager_cleanup(void);

/**
 * Get current network state
 *
 * @return Current NetworkState
 */
NetworkState network_manager_get_state(void);

/**
 * Check if network is available for use
 *
 * @return true if network can be used, false otherwise
 */
bool network_manager_is_available(void);

/**
 * Get detailed network information
 *
 * @param info Pointer to NetworkInfo structure to fill
 * @return VITARPS5_SUCCESS on success, error code on failure
 */
VitaRPS5Result network_manager_get_info(NetworkInfo* info);

/**
 * Force refresh of network state and information
 * Useful when network conditions may have changed
 *
 * @return VITARPS5_SUCCESS on success, error code on failure
 */
VitaRPS5Result network_manager_refresh(void);

/**
 * Get human-readable error message for network error codes
 *
 * @param error_code SCE network error code
 * @return Static string describing the error
 */
const char* network_manager_error_string(int error_code);

/**
 * Enable/disable offline mode
 * When enabled, all network operations will fail gracefully
 *
 * @param offline true to enable offline mode, false to disable
 */
void network_manager_set_offline_mode(bool offline);

/**
 * Check if currently in offline mode
 *
 * @return true if in offline mode, false otherwise
 */
bool network_manager_is_offline_mode(void);

#endif  // NETWORK_MANAGER_H