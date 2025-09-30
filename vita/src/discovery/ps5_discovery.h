#ifndef VITARPS5_PS5_DISCOVERY_H
#define VITARPS5_PS5_DISCOVERY_H

#include <psp2/net/net.h>
#include <psp2/types.h>
#include <stdbool.h>
#include <stdint.h>

#include "../core/console_types.h"
#include "../core/error_codes.h"
#include "../core/vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

// PlayStation console discovery configuration
#define PS5_DISCOVERY_PORT \
  9302  // PS5 discovery port (per CONNECTION_DEBUGGING.md - PS4 used 987, PS5
        // uses 9302)
#define PS5_DISCOVERY_MULTICAST "224.0.0.1"  // Multicast address for discovery
#define PS5_DISCOVERY_TIMEOUT_MS \
  2000  // 2 second discovery timeout (optimized for responsive UI)
#define PS5_MAX_CONSOLES 16     // Maximum discoverable consoles
#define PS5_DEVICE_NAME_MAX 64  // Maximum device name length
#define PS5_MAC_ADDRESS_LEN 6   // MAC address length

// Discovery ports (from Chiaki/Vitaki)
#define PS5_DISCOVERY_PORT_PS5 9302  // PS5 wake/discovery port
#define PS4_DISCOVERY_PORT_PS4 987   // PS4 wake/discovery port

// Wake protocol constants (Chiaki-ng/Vitaki compatible)
#define PS5_WAKE_PROTOCOL_VERSION_PS5 "00030010"  // PS5 wake protocol version
#define PS5_WAKE_PROTOCOL_VERSION_PS4 "00020020"  // PS4 wake protocol version
#define PS5_WAKE_PACKET_MAX_SIZE 512              // Maximum wake packet size
#define PS5_WAKE_TIMEOUT_MS 5000                  // Wake packet send timeout
#define PS5_WAKE_RESPONSE_WAIT_MS \
  (VITARPS5_TIMEOUT_SECONDS * 1000)  // Global timeout for wake response

// PlayStation console information
typedef struct {
  char ip_address[16];  // Console IP address (e.g., "192.168.1.100")
  char
      device_name[PS5_DEVICE_NAME_MAX];  // Console name (e.g., "PlayStation-5")
  char host_id[33];      // Unique host identifier (32-char hex + null)
  char mac_address[18];  // MAC address (XX:XX:XX:XX:XX:XX format)
  uint8_t mac_bytes[PS5_MAC_ADDRESS_LEN];  // MAC address bytes
  PSConsoleType console_type;              // Console type (PS4/PS5)
  uint16_t port;                           // Control port (usually 9295)
  bool is_awake;              // Console power state (deprecated - use state)
  ConsoleState state;         // Console state (Ready/Standby/Unknown)
  bool supports_h265;         // H.265 decoding capability
  uint32_t fw_version;        // Firmware version
  int32_t discovery_time_ms;  // Time when discovered (for sorting)
  float signal_strength;      // Network signal quality (0.0-1.0)
} PS5ConsoleInfo;

// Discovery scan results
typedef struct {
  PS5ConsoleInfo consoles[PS5_MAX_CONSOLES];
  uint32_t console_count;
  uint32_t scan_duration_ms;
  bool scan_complete;
  uint64_t last_scan_time;
} PS5DiscoveryResults;

// Discovery context
typedef struct PS5Discovery PS5Discovery;

// Discovery callbacks
typedef void (*PS5ConsoleFoundCallback)(const PS5ConsoleInfo* console,
                                        void* user_data);
typedef void (*PS5DiscoveryCompleteCallback)(const PS5DiscoveryResults* results,
                                             void* user_data);

// Discovery configuration
typedef struct {
  uint32_t scan_timeout_ms;        // Scan timeout in milliseconds
  uint32_t scan_interval_ms;       // Interval between discovery packets
  bool enable_wake_on_lan;         // Enable WoL for sleeping consoles
  bool filter_local_network_only;  // Only discover on local subnet

  // Callbacks
  PS5ConsoleFoundCallback console_found_callback;
  PS5DiscoveryCompleteCallback discovery_complete_callback;
  void* user_data;
} PS5DiscoveryConfig;

// Core PS5 Discovery API

/**
 * Initialize PS5 discovery subsystem
 */
VitaRPS5Result ps5_discovery_init(void);

/**
 * Cleanup PS5 discovery subsystem
 */
void ps5_discovery_cleanup(void);

/**
 * Create a new PS5 discovery instance
 */
VitaRPS5Result ps5_discovery_create(const PS5DiscoveryConfig* config,
                                    PS5Discovery** discovery);

/**
 * Destroy PS5 discovery instance
 */
void ps5_discovery_destroy(PS5Discovery* discovery);

/**
 * Start console discovery scan
 */
VitaRPS5Result ps5_discovery_start_scan(PS5Discovery* discovery);

/**
 * Stop active discovery scan
 */
VitaRPS5Result ps5_discovery_stop_scan(PS5Discovery* discovery);

/**
 * Get last discovery results
 */
VitaRPS5Result ps5_discovery_get_results(const PS5Discovery* discovery,
                                         PS5DiscoveryResults* results);

/**
 * Manually add a console by IP address
 */
VitaRPS5Result ps5_discovery_add_manual_console(PS5Discovery* discovery,
                                                const char* ip_address,
                                                const char* device_name);

/**
 * Remove a console from discovery results
 */
VitaRPS5Result ps5_discovery_remove_console(PS5Discovery* discovery,
                                            const char* host_id);

/**
 * Clear all discovery results
 */
VitaRPS5Result ps5_discovery_clear_results(PS5Discovery* discovery);

/**
 * Set PSN account for authenticated discovery
 */
VitaRPS5Result ps5_discovery_set_psn_account(PS5Discovery* discovery,
                                             const char* psn_id_base64);

// Console Information API

/**
 * Get detailed console information by IP
 */
VitaRPS5Result ps5_discovery_get_console_info(PS5Discovery* discovery,
                                              const char* ip_address,
                                              PS5ConsoleInfo* info);

/**
 * Get discovered request port for console (from host-request-port header)
 * RESEARCHER B) PATCH: Provide accessor for session-init
 */
uint16_t ps5_discovery_get_request_port(const PS5ConsoleInfo* console);

/**
 * Check if console is reachable
 */
VitaRPS5Result ps5_discovery_ping_console(const PS5ConsoleInfo* console,
                                          float* response_time_ms);

/**
 * Wake console using Wake-on-LAN
 */
VitaRPS5Result ps5_discovery_wake_console(const PS5ConsoleInfo* console);

/**
 * Lightweight state check for specific console IP
 * Sends targeted discovery packet and returns state quickly
 * Preserves previous state for STANDBY consoles that don't respond (deep sleep)
 */
VitaRPS5Result ps5_discovery_check_single_console_state(
    const char* ip_address, PSConsoleType type,
    ConsoleDiscoveryState previous_state, ConsoleDiscoveryState* state);

/**
 * Ultra-lightweight state check for UI status updates
 * Uses single UDP packet with 1-second timeout to avoid UI blocking
 * Optimized for frequent UI status checks without performance impact
 */
VitaRPS5Result ps5_discovery_lightweight_state_check(const char* ip_address,
                                                     PSConsoleType type,
                                                     ConsoleState* state);

/**
 * Wait for console to reach ready state after wake
 */
VitaRPS5Result ps5_discovery_wait_for_ready(const char* ip_address,
                                            PSConsoleType type,
                                            uint32_t max_wait_ms,
                                            ConsoleState* final_state);

/**
 * Validate console connection capability
 */
VitaRPS5Result ps5_discovery_validate_console(const PS5ConsoleInfo* console,
                                              bool* can_connect);

// Network Discovery Utilities

/**
 * Scan specific IP range for PlayStation consoles
 */
VitaRPS5Result ps5_discovery_scan_ip_range(PS5Discovery* discovery,
                                           const char* start_ip,
                                           const char* end_ip);

/**
 * Discover consoles on local subnet
 */
VitaRPS5Result ps5_discovery_scan_local_subnet(PS5Discovery* discovery);

/**
 * Send discovery broadcast packet
 */
VitaRPS5Result ps5_discovery_send_broadcast(PS5Discovery* discovery);

/**
 * Listen for discovery responses
 */
VitaRPS5Result ps5_discovery_listen_responses(PS5Discovery* discovery,
                                              uint32_t timeout_ms);

// Console Management API

/**
 * Save discovered consoles to file
 */
VitaRPS5Result ps5_discovery_save_console_list(const PS5Discovery* discovery,
                                               const char* filename);

/**
 * Load console list from file
 */
VitaRPS5Result ps5_discovery_load_console_list(PS5Discovery* discovery,
                                               const char* filename);

/**
 * Sort consoles by discovery criteria
 */
VitaRPS5Result ps5_discovery_sort_consoles(PS5DiscoveryResults* results,
                                           bool by_signal_strength);

// Utility Functions

/**
 * Convert console type to string
 */
const char* ps5_console_type_string(PSConsoleType type);

/**
 * Parse PlayStation device announcement packet
 */
VitaRPS5Result ps5_discovery_parse_announcement(const uint8_t* packet_data,
                                                size_t packet_size,
                                                PS5ConsoleInfo* console);

/**
 * Generate Wake-on-LAN magic packet
 */
VitaRPS5Result ps5_discovery_create_wol_packet(const PS5ConsoleInfo* console,
                                               uint8_t* packet_buffer,
                                               size_t* packet_size);

/**
 * Validate IP address format
 */
bool ps5_discovery_is_valid_ip(const char* ip_address);

/**
 * Calculate network signal strength
 */
float ps5_discovery_calculate_signal_strength(int32_t response_time_ms,
                                              bool is_local_network);

// Console State Tracking (PS5 Protocol Implementation)

/**
 * Check single console state via HTTP (PS5 protocol compliant)
 * Implements HTTP state checking as documented in protocol analysis:
 * - HTTP 200 = Console ready for connections
 * - HTTP 620 = Console in standby mode
 */
VitaRPS5Result ps5_discovery_check_single_console_state(
    const char* ip_address, PSConsoleType type,
    ConsoleDiscoveryState previous_state, ConsoleDiscoveryState* state);

/**
 * Wait for console to reach ready state after wake
 * Uses HTTP polling to detect when console becomes ready for session init
 */
VitaRPS5Result ps5_discovery_wait_for_ready(const char* ip_address,
                                            PSConsoleType type,
                                            uint32_t max_wait_ms,
                                            ConsoleState* final_state);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_PS5_DISCOVERY_H