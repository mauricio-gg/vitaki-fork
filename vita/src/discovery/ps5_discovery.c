#include "ps5_discovery.h"

#include <errno.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/console_registration.h"
#include "../network/network_manager.h"
#include "../psn/psn_account.h"
#include "../utils/logger.h"

// PlayStation discovery packet magic numbers
#define PS_DISCOVERY_MAGIC 0x50535044  // "PSPD" - PlayStation Discovery
#define PS_ANNOUNCE_MAGIC 0x50534133   // "PSA3" - PlayStation Announce
#define PS_WOL_MAGIC 0x50535257        // "PSRW" - PlayStation Remote Wake

// PlayStation device type identifiers
#define PS_DEVICE_PS4 0x00000004
#define PS_DEVICE_PS4_PRO 0x00000104
#define PS_DEVICE_PS5 0x00000005
#define PS_DEVICE_PS5_DIGITAL 0x00000105

// Internal discovery structure
struct PS5Discovery {
  PS5DiscoveryConfig config;
  PS5DiscoveryResults results;
  bool scan_active;

  // Network socket - single socket for both send and receive (Chiaki-style)
  int discovery_socket;  // Single socket bound to ephemeral port
  int broadcast_socket;  // DEPRECATED - kept for compatibility, always -1

  // Threading
  SceUID discovery_thread;
  bool thread_running;
  SceUID results_mutex;

  // Timing
  uint64_t scan_start_time;
  uint64_t last_broadcast_time;

  // PSN authentication
  PSNAccount* psn_account;
  bool psn_authenticated;

  // DISCOVERY OPTIMIZATION: Result caching to avoid redundant calls
  struct {
    char last_checked_ip[16];
    ConsoleState last_state;
    uint64_t last_check_time;
    uint32_t cache_timeout_ms;  // How long cache results are valid
  } state_cache;
};

// PlayStation discovery packet structure
typedef struct __attribute__((packed)) {
  uint32_t magic;              // PS_DISCOVERY_MAGIC
  uint32_t packet_type;        // Packet type identifier
  uint32_t device_type;        // PS4/PS5 device type
  uint32_t port;               // Control port (usually 9295)
  char device_name[64];        // Console name
  char host_id[32];            // Unique host identifier
  uint8_t mac_address[6];      // MAC address
  uint32_t fw_version;         // Firmware version
  uint32_t capabilities;       // Console capabilities (H.265 support, etc.)
  uint32_t power_state;        // 1 = awake, 0 = sleeping
  uint8_t psn_account_id[16];  // PSN account ID for authentication
  uint32_t auth_flags;         // Authentication flags
  uint32_t reserved[2];        // Reserved for future use (reduced from 4)
} PSDiscoveryPacket;

// Global state
static bool ps5_discovery_initialized = false;

// Internal functions
static int ps5_discovery_thread(SceSize args, void* argp);
static VitaRPS5Result create_discovery_socket(PS5Discovery* discovery);
static VitaRPS5Result send_discovery_broadcast(PS5Discovery* discovery);
static VitaRPS5Result process_discovery_responses(PS5Discovery* discovery);
static VitaRPS5Result parse_console_announcement(PS5Discovery* discovery,
                                                 const uint8_t* packet_data,
                                                 size_t packet_size,
                                                 const char* sender_ip);
static PSConsoleType device_type_to_console_type(uint32_t device_type);
static void add_discovered_console(PS5Discovery* discovery,
                                   const PS5ConsoleInfo* console);
static bool is_console_already_discovered(const PS5Discovery* discovery,
                                          const char* host_id);
static uint64_t get_timestamp_ms(void);

// Network interface enumeration
static VitaRPS5Result get_network_interfaces(uint32_t* local_ip,
                                             uint32_t* netmask,
                                             uint32_t* broadcast_ip);

// Simple SHA1 implementation for PSN ID hashing
static void simple_sha1(const uint8_t* data, size_t len, uint8_t* hash);

// Port conflict detection
static void check_discovery_port_conflicts(void);

// Discovery method (Chiaki-compatible only)
static VitaRPS5Result send_chiaki_style_discovery(PS5Discovery* discovery);

// Console state checking via HTTP (PS5 protocol specification)
static VitaRPS5Result check_console_state_http(const char* console_ip,
                                               ConsoleState* state);
static VitaRPS5Result send_http_state_request(const char* console_ip,
                                              int* http_status_code);
static ConsoleState parse_http_status_to_state(int http_status_code);

// static VitaRPS5Result create_wol_socket(void);
// static VitaRPS5Result send_wol_packet(const PS5ConsoleInfo* console);

// API Implementation

VitaRPS5Result ps5_discovery_init(void) {
  if (ps5_discovery_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing PS5 discovery subsystem");

  // Initialize PSN account subsystem for authentication
  VitaRPS5Result psn_result = psn_account_init();
  if (psn_result != VITARPS5_SUCCESS) {
    log_warning(
        "PSN account initialization failed: %s - discovery will work in "
        "unauthenticated mode",
        vitarps5_result_string(psn_result));
    // Don't fail discovery init - continue without PSN authentication
  } else {
    log_info("PSN account subsystem initialized for discovery");
  }

  // Use centralized network manager for discovery
  VitaRPS5Result result = network_manager_init();
  if (result != VITARPS5_SUCCESS) {
    if (result == VITARPS5_ERROR_OFFLINE) {
      log_warning("Network not available for discovery - simulated mode");
    } else {
      log_warning(
          "Network manager initialization failed (error %d) - simulated mode",
          result);
    }
    // Don't return error - continue with simulated discovery
  } else {
    if (network_manager_is_available()) {
      log_info("Network available for PS5 discovery");
    } else {
      log_warning(
          "Network manager reports network unavailable - simulated mode");
    }
  }

  ps5_discovery_initialized = true;
  log_info("PS5 discovery subsystem initialized");

  return VITARPS5_SUCCESS;
}

void ps5_discovery_cleanup(void) {
  if (!ps5_discovery_initialized) {
    return;
  }

  log_info("Cleaning up PS5 discovery subsystem");

  // Use centralized network manager cleanup
  network_manager_cleanup();

  ps5_discovery_initialized = false;
  log_info("PS5 discovery cleanup complete");
}

VitaRPS5Result ps5_discovery_create(const PS5DiscoveryConfig* config,
                                    PS5Discovery** discovery) {
  if (!ps5_discovery_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!config || !discovery) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  PS5Discovery* new_discovery = malloc(sizeof(PS5Discovery));
  if (!new_discovery) {
    return VITARPS5_ERROR_MEMORY;
  }

  // Initialize discovery
  memset(new_discovery, 0, sizeof(PS5Discovery));
  new_discovery->config = *config;
  new_discovery->scan_active = false;
  new_discovery->discovery_socket = -1;
  new_discovery->broadcast_socket = -1;
  new_discovery->thread_running = false;
  new_discovery->psn_account = NULL;
  new_discovery->psn_authenticated = false;

  // DISCOVERY OPTIMIZATION: Initialize state cache
  memset(&new_discovery->state_cache, 0, sizeof(new_discovery->state_cache));
  new_discovery->state_cache.cache_timeout_ms = 3000;  // 3-second cache timeout
  new_discovery->state_cache.last_state = CONSOLE_STATE_UNKNOWN;

  // Create mutex for thread-safe access to results
  new_discovery->results_mutex =
      sceKernelCreateMutex("ps5_discovery", 0, 0, NULL);
  if (new_discovery->results_mutex < 0) {
    log_error("Failed to create discovery mutex: 0x%08X",
              new_discovery->results_mutex);
    free(new_discovery);
    return VITARPS5_ERROR_INIT;
  }

  // RESEARCHER FIX: Create PSN account WITHOUT auto-refresh to avoid registry
  // conflicts We'll inject the frozen PSN ID immediately after creation
  PSNAccountConfig psn_config = {0};
  psn_config.enable_auto_refresh =
      false;  // CRITICAL: Don't auto-refresh during discovery
  psn_config.refresh_interval_ms = 0;  // Disable refresh timer
  psn_config.cache_account_info = true;

  VitaRPS5Result psn_result =
      psn_account_create(&psn_config, &new_discovery->psn_account);
  if (psn_result == VITARPS5_SUCCESS) {
    // Do NOT check authentication yet - we'll set the frozen PSN ID first
    new_discovery->psn_authenticated = false;

    log_info("=== PS5 DISCOVERY PSN ACCOUNT DEBUG ===");
    log_info("PSN account created successfully: %p",
             new_discovery->psn_account);
    log_info(
        "PSN authenticated: %s (will be set when frozen PSN ID is injected)",
        new_discovery->psn_authenticated ? "true" : "false");

    // RESEARCHER FIX: Don't check authentication here - wait for frozen PSN ID
    // injection
    log_info("PSN account ready for frozen PSN ID injection (no auto-refresh)");
    log_info(
        "PSN ID will be set via ps5_discovery_set_psn_account() with frozen "
        "value");
  } else {
    log_error("Failed to create PSN account for discovery: %s",
              vitarps5_result_string(psn_result));
    new_discovery->psn_account = NULL;
    new_discovery->psn_authenticated = false;
  }

  log_info("Created PS5 discovery instance (timeout: %ums, PSN auth: %s)",
           config->scan_timeout_ms,
           new_discovery->psn_authenticated ? "enabled" : "disabled");

  *discovery = new_discovery;
  return VITARPS5_SUCCESS;
}

void ps5_discovery_destroy(PS5Discovery* discovery) {
  if (!discovery) {
    return;
  }

  log_info("Destroying PS5 discovery instance");

  // Stop any active scan
  if (discovery->scan_active) {
    ps5_discovery_stop_scan(discovery);
  }

  // Close socket
  if (discovery->discovery_socket >= 0) {
    sceNetSocketClose(discovery->discovery_socket);
  }

  // Cleanup PSN account
  if (discovery->psn_account) {
    psn_account_destroy(discovery->psn_account);
    discovery->psn_account = NULL;
  }

  // Delete mutex
  if (discovery->results_mutex >= 0) {
    sceKernelDeleteMutex(discovery->results_mutex);
  }

  free(discovery);
}

VitaRPS5Result ps5_discovery_start_scan(PS5Discovery* discovery) {
  if (!discovery) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (discovery->scan_active) {
    return VITARPS5_SUCCESS;
  }

  log_info("Starting PS5 console discovery scan");

  // Check network availability before attempting socket operations
  if (!network_manager_is_available()) {
    log_warning("Network not available - cannot start discovery scan");
    return VITARPS5_ERROR_OFFLINE;
  }

  // Clear previous results
  sceKernelLockMutex(discovery->results_mutex, 1, NULL);
  memset(&discovery->results, 0, sizeof(PS5DiscoveryResults));
  sceKernelUnlockMutex(discovery->results_mutex, 1);

  // Create single discovery socket - uses ephemeral port for both send/receive
  VitaRPS5Result result = create_discovery_socket(discovery);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create discovery socket: %s",
              vitarps5_result_string(result));
    return result;
  }

  // Start discovery thread
  discovery->thread_running = true;
  discovery->discovery_thread = sceKernelCreateThread(
      "ps5_discovery", ps5_discovery_thread, 0x10000100, 0x10000, 0, 0, NULL);
  if (discovery->discovery_thread < 0) {
    log_error("Failed to create discovery thread: 0x%08X",
              discovery->discovery_thread);
    discovery->thread_running = false;
    sceNetSocketClose(discovery->discovery_socket);
    discovery->discovery_socket = -1;
    return VITARPS5_ERROR_INIT;
  }

  sceKernelStartThread(discovery->discovery_thread, sizeof(PS5Discovery*),
                       &discovery);

  discovery->scan_active = true;
  discovery->scan_start_time = get_timestamp_ms();

  return VITARPS5_SUCCESS;
}

VitaRPS5Result ps5_discovery_stop_scan(PS5Discovery* discovery) {
  if (!discovery) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (!discovery->scan_active) {
    return VITARPS5_SUCCESS;
  }

  log_info("Stopping PS5 console discovery scan");

  // Stop discovery thread
  if (discovery->thread_running) {
    discovery->thread_running = false;
    if (discovery->discovery_thread >= 0) {
      // Wait for thread to complete with 5 second timeout
      SceUInt timeout = 5000000;  // 5 seconds in microseconds
      int wait_result =
          sceKernelWaitThreadEnd(discovery->discovery_thread, NULL, &timeout);

      if (wait_result < 0) {
        log_warning(
            "Discovery thread did not stop gracefully within timeout: 0x%08X",
            wait_result);
        log_warning("Discovery thread may still be running in background");
      }

      sceKernelDeleteThread(discovery->discovery_thread);
      discovery->discovery_thread = -1;
    }
  }

  // Close sockets
  if (discovery->discovery_socket >= 0) {
    sceNetSocketClose(discovery->discovery_socket);
    discovery->discovery_socket = -1;
  }

  // Mark scan as complete
  sceKernelLockMutex(discovery->results_mutex, 1, NULL);
  discovery->results.scan_complete = true;
  discovery->results.scan_duration_ms =
      get_timestamp_ms() - discovery->scan_start_time;
  discovery->results.last_scan_time = get_timestamp_ms();
  sceKernelUnlockMutex(discovery->results_mutex, 1);

  discovery->scan_active = false;

  // Trigger completion callback
  if (discovery->config.discovery_complete_callback) {
    discovery->config.discovery_complete_callback(&discovery->results,
                                                  discovery->config.user_data);
  }

  log_info("Discovery scan completed - found %u consoles in %ums",
           discovery->results.console_count,
           discovery->results.scan_duration_ms);

  return VITARPS5_SUCCESS;
}

VitaRPS5Result ps5_discovery_get_results(const PS5Discovery* discovery,
                                         PS5DiscoveryResults* results) {
  if (!discovery || !results) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  sceKernelLockMutex(discovery->results_mutex, 1, NULL);
  *results = discovery->results;
  sceKernelUnlockMutex(discovery->results_mutex, 1);

  return VITARPS5_SUCCESS;
}

VitaRPS5Result ps5_discovery_add_manual_console(PS5Discovery* discovery,
                                                const char* ip_address,
                                                const char* device_name) {
  if (!discovery || !ip_address || !device_name) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (!ps5_discovery_is_valid_ip(ip_address)) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Create console info for manual entry
  PS5ConsoleInfo console;
  memset(&console, 0, sizeof(console));
  strncpy(console.ip_address, ip_address, sizeof(console.ip_address) - 1);
  strncpy(console.device_name, device_name, sizeof(console.device_name) - 1);
  console.console_type = PS_CONSOLE_UNKNOWN;
  console.port = 9295;      // Default Remote Play port
  console.is_awake = true;  // Assume awake for manual entries
  console.discovery_time_ms = get_timestamp_ms();
  console.signal_strength = 0.5f;  // Unknown signal strength

  // Generate a simple host ID from IP for manual entries
  snprintf(console.host_id, sizeof(console.host_id), "manual_%s", ip_address);

  add_discovered_console(discovery, &console);

  log_info("Manually added console: %s (%s)", device_name, ip_address);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result ps5_discovery_ping_console(const PS5ConsoleInfo* console,
                                          float* response_time_ms) {
  if (!console || !response_time_ms) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Simple TCP connection test to the console's control port
  int sock = sceNetSocket("ping_test", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
  if (sock < 0) {
    log_error("Failed to create ping socket: 0x%08X", sock);
    return VITARPS5_ERROR_NETWORK;
  }

  // Set socket to non-blocking mode for timeout control
  int flag = 1;
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &flag,
                   sizeof(flag));

  // Setup target address
  SceNetSockaddrIn addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(console->port);
  sceNetInetPton(SCE_NET_AF_INET, console->ip_address, &addr.sin_addr);

  uint64_t start_time = get_timestamp_ms();

  // Attempt connection
  int result = sceNetConnect(sock, (SceNetSockaddr*)&addr, sizeof(addr));
  if (result < 0 && result != SCE_NET_ERROR_EINPROGRESS) {
    sceNetSocketClose(sock);
    return VITARPS5_ERROR_NETWORK;
  }

  // Simple timeout approach - wait briefly and check connection status
  sceKernelDelayThread(100000);  // 100ms delay

  // Check connection status by trying to get socket error
  int sock_error = 0;
  unsigned int error_len = sizeof(sock_error);
  result = sceNetGetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR,
                            &sock_error, &error_len);

  uint64_t end_time = get_timestamp_ms();
  *response_time_ms = (float)(end_time - start_time);

  sceNetSocketClose(sock);

  if (result >= 0 && sock_error == 0) {
    return VITARPS5_SUCCESS;
  } else {
    return VITARPS5_ERROR_TIMEOUT;
  }
}

VitaRPS5Result ps5_discovery_set_psn_account(PS5Discovery* discovery,
                                             const char* psn_id_base64) {
  if (!discovery || !psn_id_base64) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("=== SETTING PSN ACCOUNT FOR DISCOVERY ===");
  log_info("PSN ID Base64: %s", psn_id_base64);

  if (!discovery->psn_account) {
    log_error("Discovery PSN account is NULL - cannot set PSN ID");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Set the PSN ID in the discovery PSN account
  VitaRPS5Result result =
      psn_account_set_psn_id_base64(discovery->psn_account, psn_id_base64);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to set PSN ID in discovery account: %s",
              vitarps5_result_string(result));
    return result;
  }

  // CRITICAL FIX: Freeze PSN account refresh after setting ID to prevent
  // overwrites
  VitaRPS5Result freeze_result =
      psn_account_freeze_refresh(discovery->psn_account);
  if (freeze_result == VITARPS5_SUCCESS) {
    log_info(
        "✅ PSN FREEZE: Discovery PSN account frozen to prevent registry "
        "overwrites");
  } else {
    log_warning("Failed to freeze discovery PSN account: %s",
                vitarps5_result_string(freeze_result));
  }

  // Update authentication status
  discovery->psn_authenticated =
      psn_account_is_authenticated(discovery->psn_account);
  log_info("Discovery PSN account updated - authenticated: %s",
           discovery->psn_authenticated ? "true" : "false");

  // Verify the PSN ID was set correctly
  uint8_t debug_psn_id[16];
  VitaRPS5Result id_result =
      psn_account_get_discovery_id(discovery->psn_account, debug_psn_id);
  if (id_result == VITARPS5_SUCCESS) {
    log_info("Verified PSN Account ID in discovery:");
    log_info("PSN ID (hex): %02X%02X%02X%02X%02X%02X%02X%02X", debug_psn_id[0],
             debug_psn_id[1], debug_psn_id[2], debug_psn_id[3], debug_psn_id[4],
             debug_psn_id[5], debug_psn_id[6], debug_psn_id[7]);
  } else {
    log_warning("Could not verify PSN Account ID: %s",
                vitarps5_result_string(id_result));
  }

  return VITARPS5_SUCCESS;
}

// Internal Functions

static int ps5_discovery_thread(SceSize args, void* argp) {
  PS5Discovery* discovery = *(PS5Discovery**)argp;

  log_info("PS5 discovery thread started");

  uint64_t scan_end_time =
      discovery->scan_start_time + discovery->config.scan_timeout_ms;

  while (discovery->thread_running && get_timestamp_ms() < scan_end_time) {
    // Send periodic discovery broadcasts
    uint64_t now = get_timestamp_ms();
    if (now - discovery->last_broadcast_time >=
        discovery->config.scan_interval_ms) {
      // DISABLED: Binary packet discovery - confuses PS5 protocol
      // send_discovery_broadcast(discovery);

      // Use only HTTP-style discovery packets (Chiaki-compatible)
      send_chiaki_style_discovery(discovery);
      discovery->last_broadcast_time = now;
    }

    // Process any incoming responses
    process_discovery_responses(discovery);

    // Small delay to prevent busy waiting
    sceKernelDelayThread(10000);  // 10ms
  }

  // Discovery scan complete - provide summary
  uint64_t scan_duration = get_timestamp_ms() - discovery->scan_start_time;
  log_info("=== PS5 DISCOVERY SCAN COMPLETE ===");
  log_info("Scan duration: %llu ms", scan_duration);
  log_info("Consoles found: %d", discovery->results.console_count);

  if (discovery->results.console_count == 0) {
    log_warning("⚠ No PS5 consoles found during discovery scan");
    log_info("Troubleshooting suggestions:");
    log_info("  • Ensure PS5 is powered on and network-connected");
    log_info("  • Check PS5 Remote Play settings are enabled");
    log_info("  • Verify Vita and PS5 are on same network subnet");
    log_info("  • Consider manual console addition if auto-discovery fails");
    log_info("  • Check if port 987/9302 binding was successful in logs above");
  } else {
    log_info("✓ Discovery completed successfully");
    for (uint32_t i = 0; i < discovery->results.console_count; i++) {
      log_info("  Console %d: %s (%s)", i + 1,
               discovery->results.consoles[i].device_name,
               discovery->results.consoles[i].ip_address);
    }
  }

  // Update discovery results and call completion callback
  sceKernelLockMutex(discovery->results_mutex, 1, NULL);
  discovery->results.scan_duration_ms = scan_duration;
  discovery->results.scan_complete = true;
  discovery->results.last_scan_time = get_timestamp_ms();
  sceKernelUnlockMutex(discovery->results_mutex, 1);

  discovery->scan_active = false;

  // Trigger completion callback for timeout/natural completion
  if (discovery->config.discovery_complete_callback) {
    log_info("Calling completion callback after timeout");
    discovery->config.discovery_complete_callback(&discovery->results,
                                                  discovery->config.user_data);
  }

  log_info("PS5 discovery thread ended");
  return 0;
}

static VitaRPS5Result create_discovery_socket(PS5Discovery* discovery) {
  log_info("=== CREATING PS5 DISCOVERY SOCKET (CHIAKI-STYLE) ===");
  log_info("Using single socket for both send and receive operations");

  // Check for port conflicts and provide diagnostic information
  check_discovery_port_conflicts();

  // Create UDP socket for discovery - uses ephemeral port
  discovery->discovery_socket =
      sceNetSocket("ps5_discovery", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM,
                   SCE_NET_IPPROTO_UDP);
  if (discovery->discovery_socket < 0) {
    log_error("Failed to create discovery socket: 0x%08X",
              discovery->discovery_socket);
    return VITARPS5_ERROR_NETWORK;
  }

  log_info("Discovery socket created successfully: %d",
           discovery->discovery_socket);

  // Skip SO_REUSEADDR/SO_REUSEPORT - not needed for ephemeral port
  log_info("Using ephemeral port strategy - no specific port binding required");

  // Enable broadcast
  int broadcast = 1;
  int broadcast_result =
      sceNetSetsockopt(discovery->discovery_socket, SCE_NET_SOL_SOCKET,
                       SCE_NET_SO_BROADCAST, &broadcast, sizeof(broadcast));
  if (broadcast_result < 0) {
    log_error("Failed to enable broadcast: 0x%08X", broadcast_result);
    sceNetSocketClose(discovery->discovery_socket);
    discovery->discovery_socket = -1;
    return VITARPS5_ERROR_NETWORK;
  } else {
    log_info("Broadcast enabled for discovery packets");
  }

  // Set socket to non-blocking
  int nonblock = 1;
  int nonblock_result =
      sceNetSetsockopt(discovery->discovery_socket, SCE_NET_SOL_SOCKET,
                       SCE_NET_SO_NBIO, &nonblock, sizeof(nonblock));
  if (nonblock_result < 0) {
    log_warning("Failed to set non-blocking mode: 0x%08X", nonblock_result);
  } else {
    log_info("Non-blocking mode enabled");
  }

  // CHIAKI-STYLE: Always use ephemeral port for discovery
  log_info("=== CHIAKI-STYLE PORT BINDING ===");
  log_info("Binding to ephemeral port (0) - PS5 will respond to source port");

  SceNetSockaddrIn bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = SCE_NET_AF_INET;
  bind_addr.sin_addr.s_addr = SCE_NET_INADDR_ANY;
  bind_addr.sin_port = 0;  // Let system choose ephemeral port

  int result = sceNetBind(discovery->discovery_socket,
                          (SceNetSockaddr*)&bind_addr, sizeof(bind_addr));
  if (result < 0) {
    log_error("Failed to bind to ephemeral port: 0x%08X", result);
    sceNetSocketClose(discovery->discovery_socket);
    discovery->discovery_socket = -1;
    return VITARPS5_ERROR_NETWORK;
  }

  // Get the actual port we were assigned
  SceNetSockaddrIn actual_addr;
  unsigned int addr_len = sizeof(actual_addr);
  sceNetGetsockname(discovery->discovery_socket, (SceNetSockaddr*)&actual_addr,
                    &addr_len);
  int final_port = sceNetNtohs(actual_addr.sin_port);

  log_info("=== DISCOVERY SOCKET SETUP COMPLETE ===");
  log_info("✓ Successfully bound to ephemeral port %d", final_port);
  log_info("✓ PS5 will send discovery responses to our source port %d",
           final_port);
  log_info("✓ This avoids port conflicts with other processes");
  log_info("Discovery socket ready for broadcast and response reception");

  return VITARPS5_SUCCESS;
}

// REMOVED: create_broadcast_socket - now using single socket for both
// operations

static VitaRPS5Result send_discovery_broadcast(PS5Discovery* discovery) {
  PSDiscoveryPacket packet;
  memset(&packet, 0, sizeof(packet));

  packet.magic = PS_DISCOVERY_MAGIC;
  packet.packet_type = 1;  // Discovery request
  packet.device_type = 0;  // PS Vita (client)
  packet.port = 0;         // We're not hosting
  strncpy(packet.device_name, "PS-Vita-VitaRPS5",
          sizeof(packet.device_name) - 1);

  // Include PSN authentication if available
  log_info("=== PS5 DISCOVERY PACKET DEBUG ===");
  log_info("discovery->psn_authenticated: %s",
           discovery->psn_authenticated ? "true" : "false");
  log_info("discovery->psn_account: %p", discovery->psn_account);

  if (discovery->psn_authenticated && discovery->psn_account) {
    VitaRPS5Result auth_result = psn_account_get_discovery_id(
        discovery->psn_account, packet.psn_account_id);
    if (auth_result == VITARPS5_SUCCESS) {
      packet.auth_flags = 0x01;  // Authenticated discovery flag

      // Log the PSN ID we're sending (first 8 bytes as hex)
      log_info("Sending authenticated discovery packet with PSN ID:");
      log_info("PSN Account ID (hex): %02X%02X%02X%02X%02X%02X%02X%02X",
               packet.psn_account_id[0], packet.psn_account_id[1],
               packet.psn_account_id[2], packet.psn_account_id[3],
               packet.psn_account_id[4], packet.psn_account_id[5],
               packet.psn_account_id[6], packet.psn_account_id[7]);
    } else {
      log_warning("Failed to get PSN ID for discovery: %s",
                  vitarps5_result_string(auth_result));
      memset(packet.psn_account_id, 0, sizeof(packet.psn_account_id));
      packet.auth_flags = 0x00;  // Unauthenticated
    }
  } else {
    // Send unauthenticated discovery packet
    memset(packet.psn_account_id, 0, sizeof(packet.psn_account_id));
    packet.auth_flags = 0x00;  // Unauthenticated
    log_info("Sending unauthenticated discovery packet (PSN not available)");
  }

  // Use only HTTP-style discovery like Chiaki/Vitaki (working implementations)
  // PS5 responds to HTTP-style packets but may ignore binary/custom formats
  VitaRPS5Result result = send_chiaki_style_discovery(discovery);

  log_info("Discovery packet result - HTTP-style: %s",
           vitarps5_result_string(result));

  return result;
}

static VitaRPS5Result process_discovery_responses(PS5Discovery* discovery) {
  uint8_t buffer[1024];
  SceNetSockaddrIn sender_addr;
  unsigned int addr_len = sizeof(sender_addr);

  // Enhanced response processing with network validation
  log_debug("=== PROCESSING DISCOVERY RESPONSES ===");
  log_debug("Listening on discovery socket %d for PS5 responses...",
            discovery->discovery_socket);

  int received =
      sceNetRecvfrom(discovery->discovery_socket, buffer, sizeof(buffer), 0,
                     (SceNetSockaddr*)&sender_addr, &addr_len);

  if (received > 0) {
    char sender_ip[16];
    sceNetInetNtop(SCE_NET_AF_INET, &sender_addr.sin_addr, sender_ip,
                   sizeof(sender_ip));
    uint16_t sender_port = sceNetNtohs(sender_addr.sin_port);

    log_info("=== RECEIVED UDP PACKET ===");
    log_info("From: %s:%d (%d bytes)", sender_ip, sender_port, received);

    // Log packet content for debugging (first 32 bytes)
    log_info("Packet hex dump (first %d bytes):",
             (received < 32) ? received : 32);
    char hex_dump[128] = {0};
    for (int i = 0; i < ((received < 32) ? received : 32); i++) {
      snprintf(hex_dump + (i * 3), sizeof(hex_dump) - (i * 3), "%02X ",
               buffer[i]);
    }
    log_info("  %s", hex_dump);

    // Try to interpret as text for simple packets
    bool is_printable = true;
    for (int i = 0; i < received && i < 64; i++) {
      if (buffer[i] < 32 || buffer[i] > 126) {
        is_printable = false;
        break;
      }
    }

    if (is_printable) {
      char text_preview[65] = {0};
      memcpy(text_preview, buffer, (received < 64) ? received : 64);
      log_info("Text content: \"%s\"", text_preview);
    }

    // Try to parse as PlayStation announcement
    VitaRPS5Result parse_result =
        parse_console_announcement(discovery, buffer, received, sender_ip);
    if (parse_result != VITARPS5_SUCCESS) {
      log_info(
          "Not a PlayStation announcement packet (this is normal for other "
          "network traffic)");
    }

    return VITARPS5_SUCCESS;
  } else if (received < 0) {
    // Enhanced error logging with specific error analysis
    if (received == SCE_NET_ERROR_EWOULDBLOCK || received == -2146762492) {
      log_debug(
          "No data available (EWOULDBLOCK) - this is normal during discovery");
    } else if (received == 0x80410104) {  // SCE_NET_ERROR_ENOTCONN
      log_warning("Socket not connected: 0x%08X", received);
    } else if (received == 0x80410105) {  // SCE_NET_ERROR_ESHUTDOWN
      log_warning("Socket shutdown: 0x%08X", received);
    } else if (received == 0x80410108) {  // SCE_NET_ERROR_ENOTSOCK
      log_error("Invalid socket descriptor: 0x%08X", received);
    } else {
      log_warning("Discovery receive error: 0x%08X", received);
    }
  } else if (received == 0) {
    log_debug("Zero-length packet received (connection closed gracefully)");
  }

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result parse_console_announcement(PS5Discovery* discovery,
                                                 const uint8_t* packet_data,
                                                 size_t packet_size,
                                                 const char* sender_ip) {
  // Parse HTTP-style response like Chiaki/Vitaki
  // Expected format: HTTP/1.1 200 OK\r\nheader: value\r\n...

  // Convert to string for parsing
  char* response = malloc(packet_size + 1);
  if (!response) {
    return VITARPS5_ERROR_MEMORY;
  }

  memcpy(response, packet_data, packet_size);
  response[packet_size] = '\0';

  log_info("=== PS5 DISCOVERY RESPONSE DEBUG ===");
  log_info("Source IP: %s", sender_ip);
  log_info("Response size: %zu bytes", packet_size);
  log_info("Raw response content:");
  log_info("[%s]", response);
  log_info("=====================================");

  // Check for HTTP response header
  if (strncmp(response, "HTTP/1.1", 8) != 0) {
    log_debug("Not an HTTP response, ignoring");
    free(response);
    return VITARPS5_ERROR_PROTOCOL;
  }

  // Parse HTTP status code
  int status_code = 0;
  if (sscanf(response + 9, "%d", &status_code) != 1) {
    log_warning("Failed to parse HTTP status code");
    free(response);
    return VITARPS5_ERROR_PROTOCOL;
  }

  log_info("HTTP status code: %d", status_code);
  log_info("=== HEADER PARSING DEBUG ===");

  // Create console info
  PS5ConsoleInfo console;
  memset(&console, 0, sizeof(console));

  // Set basic info
  strncpy(console.ip_address, sender_ip, sizeof(console.ip_address) - 1);
  console.discovery_time_ms = get_timestamp_ms();

  // Initialize state as unknown - will be determined by parsing headers or
  // status code
  console.is_awake = false;
  bool state_parsed_from_header = false;

  // Parse headers first to look for explicit state field (PS5 Protocol
  // compliance)
  char* line = strtok(response, "\r\n");
  while (line != NULL) {
    log_info("Parsing header line: [%s]", line);  // Debug every header line

    if (strstr(line, "host-name:")) {
      sscanf(line, "host-name:%63s", console.device_name);
      log_info("✓ Host name: %s", console.device_name);
    } else if (strstr(line, "host-type:")) {
      char host_type[32];
      sscanf(line, "host-type:%31s", host_type);
      log_info("✓ Host type: %s", host_type);
      if (strstr(host_type, "PS5")) {
        console.console_type = PS_CONSOLE_PS5;
        log_info("✓ Detected PS5 console");
      } else if (strstr(host_type, "PS4")) {
        console.console_type = PS_CONSOLE_PS4;
        log_info("✓ Detected PS4 console");
      }
    } else if (strstr(line, "host-id:")) {
      sscanf(line, "host-id:%32s", console.host_id);
      log_info("✓ Host ID: %s", console.host_id);
    } else if (strstr(line, "host-request-port:")) {
      int port;
      if (sscanf(line, "host-request-port:%d", &port) == 1) {
        if (port > 0 && port < 65536) {
          console.port = (uint16_t)port;
          log_debug("Discovery: host-request-port=%u", console.port);
          log_info("✓ Request port: %d", port);
        }
      }
    } else if (strstr(line, "system-version:")) {
      unsigned int version;
      if (sscanf(line, "system-version:%u", &version) == 1) {
        console.fw_version = version;
        log_info("✓ System version: %u", version);
      }
    } else if (strstr(line, "host-state:") || strstr(line, "status:") ||
               strstr(line, "state:") || strstr(line, "Host-State:") ||
               strstr(line, "Status:") || strstr(line, "State:") ||
               strstr(line, "ps-state:") || strstr(line, "PS-State:") ||
               strstr(line, "running-app:")) {
      // PROTOCOL COMPLIANCE: Parse explicit state field from PS5 discovery
      // response Try multiple header name variations (case insensitive)
      char state_value[64] = {0};

      log_info("🔍 Found potential state header: [%s]", line);

      // Try different state header formats with case variations
      if (sscanf(line, "host-state:%63s", state_value) == 1 ||
          sscanf(line, "Host-State:%63s", state_value) == 1 ||
          sscanf(line, "status:%63s", state_value) == 1 ||
          sscanf(line, "Status:%63s", state_value) == 1 ||
          sscanf(line, "state:%63s", state_value) == 1 ||
          sscanf(line, "State:%63s", state_value) == 1 ||
          sscanf(line, "ps-state:%63s", state_value) == 1 ||
          sscanf(line, "PS-State:%63s", state_value) == 1 ||
          sscanf(line, "running-app:%63s", state_value) == 1) {
        log_info("🎯 Extracted state value: [%s]", state_value);

        // Parse PS5 protocol state values (more comprehensive matching)
        if (strcasecmp(state_value, "ready") == 0 ||
            strcasecmp(state_value, "awake") == 0 ||
            strcasecmp(state_value, "active") == 0 ||
            strcasecmp(state_value, "on") == 0) {
          console.is_awake = true;
          state_parsed_from_header = true;
          log_info("✅ Console state from header: READY (awake) - value: %s",
                   state_value);
        } else if (strcasecmp(state_value, "standby") == 0 ||
                   strcasecmp(state_value, "sleep") == 0 ||
                   strcasecmp(state_value, "rest") == 0 ||
                   strcasecmp(state_value, "off") == 0) {
          console.is_awake = false;
          state_parsed_from_header = true;
          log_info(
              "😴 Console state from header: STANDBY (sleeping) - value: %s",
              state_value);
        } else {
          log_warning(
              "❓ Unknown PS5 state value in header: '%s' - will use HTTP "
              "status fallback",
              state_value);
        }
      } else {
        log_warning("❌ Failed to parse state value from header: [%s]", line);
      }
    } else {
      log_debug("→ Unrecognized header: [%s]", line);
    }

    line = strtok(NULL, "\r\n");
  }

  log_info("=== END HEADER PARSING ===");

  // Fallback to HTTP status code if no explicit state header found (backward
  // compatibility)
  if (!state_parsed_from_header) {
    log_info("=== HTTP STATUS CODE INTERPRETATION ===");
    log_info(
        "No explicit state header found, interpreting HTTP status code: %d",
        status_code);

    // CORRECTED PS5 STATE DETECTION: Based on actual PS5 behavior analysis
    // Research shows that awake PS5 consoles often return various HTTP status
    // codes
    if (status_code >= 200 && status_code < 500) {
      // Any HTTP response in 2xx-4xx range typically means console is awake and
      // responding This includes: 200 OK, 404 Not Found, 403 Forbidden, etc.
      console.is_awake = true;
      log_info(
          "✅ Console state from HTTP status: READY (status %d - console "
          "responding)",
          status_code);
      log_info(
          "   → Rationale: HTTP %d indicates console is awake and processing "
          "requests",
          status_code);
    } else if (status_code == 620) {
      // PS5-specific standby status code
      console.is_awake = false;
      log_info(
          "😴 Console state from HTTP status: STANDBY (status 620 - PS5 rest "
          "mode)");
    } else if (status_code >= 500) {
      // 5xx server errors might indicate console is awake but having issues
      // Treat as READY since console is responding
      console.is_awake = true;
      log_info(
          "⚠️  Console state from HTTP status: READY (status %d - server error "
          "but responding)",
          status_code);
      log_info(
          "   → Rationale: HTTP %d indicates console is awake but may have "
          "service issues",
          status_code);
    } else {
      // Unknown status codes - default to READY (awake) instead of STANDBY
      // This reverses the previous assumption that unknown = standby
      console.is_awake = true;
      log_info(
          "❓ Console state from HTTP status: READY (status %d - unknown, "
          "assuming awake)",
          status_code);
      log_info(
          "   → Rationale: Default to READY for unknown status codes (console "
          "is responding)");
    }
    log_info("=== END STATUS CODE INTERPRETATION ===");
  }

  // Default values if not provided
  if (console.port == 0) {
    console.port = 9295;  // Default PS Remote Play port
  }

  if (strlen(console.device_name) == 0) {
    snprintf(console.device_name, sizeof(console.device_name), "PlayStation-%s",
             console.console_type == PS_CONSOLE_PS5 ? "5" : "4");
  }

  // Set signal strength based on response time
  console.signal_strength = 0.8f;  // Good signal if we got a response

  // Add to discovery results
  add_discovered_console(discovery, &console);

  log_info("=== FINAL CONSOLE STATE SUMMARY ===");
  log_info("Console: %s (%s)", console.device_name, console.ip_address);
  log_info("Type: %s", ps5_console_type_string(console.console_type));
  log_info("Port: %d", console.port);
  log_info("Final State: %s (%s)", console.is_awake ? "READY" : "STANDBY",
           console.is_awake ? "awake/responsive" : "sleeping/rest mode");
  log_info("State Source: %s", state_parsed_from_header
                                   ? "Explicit header field"
                                   : "HTTP status code interpretation");
  log_info("=====================================");

  log_info("Discovered %s console: %s (%s) port %d - State: %s",
           ps5_console_type_string(console.console_type), console.device_name,
           console.ip_address, console.port,
           console.is_awake ? "READY" : "STANDBY");

  free(response);
  return VITARPS5_SUCCESS;
}

static PSConsoleType device_type_to_console_type(uint32_t device_type) {
  switch (device_type) {
    case PS_DEVICE_PS4:
      return PS_CONSOLE_PS4;
    case PS_DEVICE_PS4_PRO:
      return PS_CONSOLE_PS4_PRO;
    case PS_DEVICE_PS5:
      return PS_CONSOLE_PS5;
    case PS_DEVICE_PS5_DIGITAL:
      return PS_CONSOLE_PS5_DIGITAL;
    default:
      return PS_CONSOLE_UNKNOWN;
  }
}

static void add_discovered_console(PS5Discovery* discovery,
                                   const PS5ConsoleInfo* console) {
  sceKernelLockMutex(discovery->results_mutex, 1, NULL);

  // Check if console already exists
  bool found_existing = false;
  for (uint32_t i = 0; i < discovery->results.console_count; i++) {
    if (strcmp(discovery->results.consoles[i].host_id, console->host_id) == 0) {
      // Update existing console state
      discovery->results.consoles[i].is_awake = console->is_awake;
      discovery->results.consoles[i].discovery_time_ms =
          console->discovery_time_ms;
      log_info("Updated existing console %s state: %s", console->host_id,
               console->is_awake ? "Ready" : "Standby");
      found_existing = true;

      // Trigger found callback for state update
      if (discovery->config.console_found_callback) {
        discovery->config.console_found_callback(
            &discovery->results.consoles[i], discovery->config.user_data);
      }
      break;
    }
  }

  // Add new console if not found
  if (!found_existing) {
    if (discovery->results.console_count < PS5_MAX_CONSOLES) {
      discovery->results.consoles[discovery->results.console_count] = *console;
      discovery->results.console_count++;
      log_info("Added new console %s state: %s", console->host_id,
               console->is_awake ? "Ready" : "Standby");

      // Trigger found callback
      if (discovery->config.console_found_callback) {
        discovery->config.console_found_callback(console,
                                                 discovery->config.user_data);
      }
    }
  }

  sceKernelUnlockMutex(discovery->results_mutex, 1);
}

static bool is_console_already_discovered(const PS5Discovery* discovery,
                                          const char* host_id) {
  for (uint32_t i = 0; i < discovery->results.console_count; i++) {
    if (strcmp(discovery->results.consoles[i].host_id, host_id) == 0) {
      return true;
    }
  }
  return false;
}

static uint64_t get_timestamp_ms(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick / 1000;  // Convert to milliseconds
}

// Utility Functions

const char* ps5_console_type_string(PSConsoleType type) {
  switch (type) {
    case PS_CONSOLE_PS4:
      return "PlayStation 4";
    case PS_CONSOLE_PS4_PRO:
      return "PlayStation 4 Pro";
    case PS_CONSOLE_PS5:
      return "PlayStation 5";
    case PS_CONSOLE_PS5_DIGITAL:
      return "PlayStation 5 Digital";
    case PS_CONSOLE_UNKNOWN:
    default:
      return "Unknown Console";
  }
}

bool ps5_discovery_is_valid_ip(const char* ip_address) {
  if (!ip_address) return false;

  SceNetInAddr addr;
  return sceNetInetPton(SCE_NET_AF_INET, ip_address, &addr) == 1;
}

// RESEARCHER B) PATCH: Provide accessor for discovered request port
uint16_t ps5_discovery_get_request_port(const PS5ConsoleInfo* console) {
  if (!console || console->port == 0) {
    log_debug("Discovery: using fallback port 9295 (console=%p, port=%d)",
              console, console ? console->port : 0);
    return 9295;  // defensive fallback
  }
  log_debug("Discovery: host-request-port=%u", console->port);
  return console->port;
}

// Retrieve detailed console info for a specific IP from the discovery context
VitaRPS5Result ps5_discovery_get_console_info(PS5Discovery* discovery,
                                              const char* ip_address,
                                              PS5ConsoleInfo* info) {
  if (!discovery || !ip_address || !info) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Lock results for thread-safe read
  sceKernelLockMutex(discovery->results_mutex, 1, NULL);

  VitaRPS5Result result = VITARPS5_ERROR_NOT_FOUND;
  for (uint32_t i = 0; i < discovery->results.console_count; i++) {
    PS5ConsoleInfo* c = &discovery->results.consoles[i];
    if (strcmp(c->ip_address, ip_address) == 0) {
      *info = *c;  // copy entire struct
      result = VITARPS5_SUCCESS;
      break;
    }
  }

  sceKernelUnlockMutex(discovery->results_mutex, 1);
  if (result != VITARPS5_SUCCESS) {
    log_debug("Discovery: console %s not found in current results", ip_address);
  }
  return result;
}

float ps5_discovery_calculate_signal_strength(int32_t response_time_ms,
                                              bool is_local_network) {
  if (response_time_ms < 0) return 0.0f;

  // Simple signal strength calculation based on response time
  float strength;
  if (response_time_ms < 10) {
    strength = 1.0f;  // Excellent
  } else if (response_time_ms < 50) {
    strength = 0.8f;  // Good
  } else if (response_time_ms < 100) {
    strength = 0.6f;  // Fair
  } else if (response_time_ms < 200) {
    strength = 0.4f;  // Poor
  } else {
    strength = 0.2f;  // Very poor
  }

  // Boost signal strength for local network devices
  if (is_local_network && strength < 0.8f) {
    strength += 0.2f;
  }

  return (strength > 1.0f) ? 1.0f : strength;
}

// Network interface enumeration implementation

static VitaRPS5Result get_network_interfaces(uint32_t* local_ip,
                                             uint32_t* netmask,
                                             uint32_t* broadcast_ip) {
  if (!local_ip || !netmask || !broadcast_ip) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("=== ENUMERATING NETWORK INTERFACES ===");

  // Get IP address
  SceNetCtlInfo ip_info;
  int result = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &ip_info);
  if (result < 0) {
    log_error("Failed to get IP address: 0x%08X", result);
    return VITARPS5_ERROR_NETWORK;
  }

  // Get netmask
  SceNetCtlInfo netmask_info;
  result = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_NETMASK, &netmask_info);
  if (result < 0) {
    log_error("Failed to get netmask: 0x%08X", result);
    return VITARPS5_ERROR_NETWORK;
  }

  // Convert IP addresses from string to binary
  sceNetInetPton(SCE_NET_AF_INET, ip_info.ip_address, local_ip);
  sceNetInetPton(SCE_NET_AF_INET, netmask_info.netmask, netmask);

  // Calculate directed broadcast address: (IP & mask) | ~mask
  *broadcast_ip = (*local_ip & *netmask) | (~(*netmask));

  // Convert back to strings for logging
  char local_ip_str[16], netmask_str[16], broadcast_str[16];
  sceNetInetNtop(SCE_NET_AF_INET, local_ip, local_ip_str, sizeof(local_ip_str));
  sceNetInetNtop(SCE_NET_AF_INET, netmask, netmask_str, sizeof(netmask_str));
  sceNetInetNtop(SCE_NET_AF_INET, broadcast_ip, broadcast_str,
                 sizeof(broadcast_str));

  log_info("Local IP: %s", local_ip_str);
  log_info("Netmask: %s", netmask_str);
  log_info("Directed Broadcast: %s", broadcast_str);
  log_info("=== NETWORK INTERFACE ENUMERATION COMPLETE ===");

  return VITARPS5_SUCCESS;
}

/* REMOVED: send_custom_discovery_packet() and send_simple_discovery_ping()
 *
 * These functions have been removed because:
 * 1. PS5 responds to HTTP-style discovery but may ignore binary/custom formats
 * 2. VitaRPS5 now uses only Chiaki-compatible HTTP-style discovery packets
 * 3. Multiple discovery methods can confuse the protocol and create duplicates
 *
 * Only send_chiaki_style_discovery() is now used for optimal PS5 compatibility.
 */

static VitaRPS5Result send_chiaki_style_discovery(PS5Discovery* discovery) {
  log_info("=== SENDING CHIAKI HTTP-STYLE DISCOVERY ===");

  // Based on actual Chiaki/Vitaki implementation analysis:
  // - Uses HTTP-style text packets, NOT binary data
  // - Sends two separate packets: PS4 (port 987) and PS5 (port 9302)
  // - No PSN authentication in discovery packets
  // - Format: "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:XXXXXXXX\n"

  SceNetSockaddrIn broadcast_addr;
  memset(&broadcast_addr, 0, sizeof(broadcast_addr));
  broadcast_addr.sin_family = SCE_NET_AF_INET;
  broadcast_addr.sin_addr.s_addr = SCE_NET_INADDR_BROADCAST;

  // Discovery packet format (exactly matching Chiaki implementation)
  const char* ps4_packet =
      "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00020020\n";
  const char* ps5_packet =
      "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00030010\n";

  VitaRPS5Result result = VITARPS5_SUCCESS;

  // Send PS4 discovery packet (port 987)
  broadcast_addr.sin_port = sceNetHtons(987);

  log_info("Sending PS4 discovery to 255.255.255.255:987");
  log_info("Packet content: [%s]", ps4_packet);

  int sent_bytes = sceNetSendto(
      discovery->discovery_socket, ps4_packet, strlen(ps4_packet) + 1, 0,
      (SceNetSockaddr*)&broadcast_addr, sizeof(broadcast_addr));

  if (sent_bytes < 0) {
    log_error("Failed to send PS4 discovery packet: 0x%08X", sent_bytes);
    result = VITARPS5_ERROR_NETWORK;
  } else {
    log_info("PS4 discovery packet sent successfully (%d bytes)", sent_bytes);
  }

  // Send PS5 discovery packet (port 9302)
  broadcast_addr.sin_port = sceNetHtons(9302);

  log_info("Sending PS5 discovery to 255.255.255.255:9302");
  log_info("Packet content: [%s]", ps5_packet);

  sent_bytes = sceNetSendto(
      discovery->discovery_socket, ps5_packet, strlen(ps5_packet) + 1, 0,
      (SceNetSockaddr*)&broadcast_addr, sizeof(broadcast_addr));

  if (sent_bytes < 0) {
    log_error("Failed to send PS5 discovery packet: 0x%08X", sent_bytes);
    result = VITARPS5_ERROR_NETWORK;
  } else {
    log_info("PS5 discovery packet sent successfully (%d bytes)", sent_bytes);
  }

  log_info("Chiaki HTTP-style discovery packets sent");
  return result;
}

// Simple SHA1 implementation for PSN ID hashing
// Based on RFC 3174 - simplified for embedded use
static void simple_sha1(const uint8_t* data, size_t len, uint8_t* hash) {
  // SHA1 constants
  uint32_t h0 = 0x67452301;
  uint32_t h1 = 0xEFCDAB89;
  uint32_t h2 = 0x98BADCFE;
  uint32_t h3 = 0x10325476;
  uint32_t h4 = 0xC3D2E1F0;

  // For PSN ID (8 bytes), we can use a simplified implementation
  // Create 512-bit block with padding
  uint8_t block[64];
  memset(block, 0, sizeof(block));
  memcpy(block, data, len);
  block[len] = 0x80;  // Padding bit

  // Length in bits at end of block (big-endian)
  uint64_t bit_len = len * 8;
  block[56] = (bit_len >> 56) & 0xFF;
  block[57] = (bit_len >> 48) & 0xFF;
  block[58] = (bit_len >> 40) & 0xFF;
  block[59] = (bit_len >> 32) & 0xFF;
  block[60] = (bit_len >> 24) & 0xFF;
  block[61] = (bit_len >> 16) & 0xFF;
  block[62] = (bit_len >> 8) & 0xFF;
  block[63] = bit_len & 0xFF;

  // Process single 512-bit block
  uint32_t w[80];

  // Break chunk into sixteen 32-bit big-endian words
  for (int i = 0; i < 16; i++) {
    w[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16) |
           (block[i * 4 + 2] << 8) | block[i * 4 + 3];
  }

  // Extend sixteen words into eighty words
  for (int i = 16; i < 80; i++) {
    uint32_t temp = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
    w[i] = (temp << 1) | (temp >> 31);  // Left rotate 1
  }

  // Initialize working variables
  uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

  // Main loop
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5A827999;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDC;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }

    uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
    e = d;
    d = c;
    c = (b << 30) | (b >> 2);
    b = a;
    a = temp;
  }

  // Add to hash values
  h0 += a;
  h1 += b;
  h2 += c;
  h3 += d;
  h4 += e;

  // Convert to output format (big-endian)
  hash[0] = (h0 >> 24) & 0xFF;
  hash[1] = (h0 >> 16) & 0xFF;
  hash[2] = (h0 >> 8) & 0xFF;
  hash[3] = h0 & 0xFF;
  hash[4] = (h1 >> 24) & 0xFF;
  hash[5] = (h1 >> 16) & 0xFF;
  hash[6] = (h1 >> 8) & 0xFF;
  hash[7] = h1 & 0xFF;
  hash[8] = (h2 >> 24) & 0xFF;
  hash[9] = (h2 >> 16) & 0xFF;
  hash[10] = (h2 >> 8) & 0xFF;
  hash[11] = h2 & 0xFF;
  hash[12] = (h3 >> 24) & 0xFF;
  hash[13] = (h3 >> 16) & 0xFF;
  hash[14] = (h3 >> 8) & 0xFF;
  hash[15] = h3 & 0xFF;
  hash[16] = (h4 >> 24) & 0xFF;
  hash[17] = (h4 >> 16) & 0xFF;
  hash[18] = (h4 >> 8) & 0xFF;
  hash[19] = h4 & 0xFF;
}

// Port conflict detection - check which process is using discovery ports
static void check_discovery_port_conflicts(void) {
  log_info("=== CHECKING DISCOVERY PORT CONFLICTS ===");

  uint16_t discovery_ports[] = {987, 9302, 9295};
  int ports_blocked = 0;

  for (size_t i = 0; i < sizeof(discovery_ports) / sizeof(discovery_ports[0]);
       i++) {
    uint16_t port = discovery_ports[i];

    // Try to create and bind a test socket to each port
    int test_socket = sceNetSocket("port_test", SCE_NET_AF_INET,
                                   SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP);
    if (test_socket < 0) {
      log_warning("Could not create test socket for port %d", port);
      continue;
    }

    // Enable reuse to avoid interfering with existing connections
    int reuse = 1;
    sceNetSetsockopt(test_socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR,
                     &reuse, sizeof(reuse));

    SceNetSockaddrIn test_addr;
    memset(&test_addr, 0, sizeof(test_addr));
    test_addr.sin_family = SCE_NET_AF_INET;
    test_addr.sin_addr.s_addr = SCE_NET_INADDR_ANY;
    test_addr.sin_port = sceNetHtons(port);

    int result =
        sceNetBind(test_socket, (SceNetSockaddr*)&test_addr, sizeof(test_addr));
    if (result < 0) {
      if (result == 0x8041010D) {  // SCE_NET_ERROR_EADDRINUSE
        log_warning("Port %d is in use by another process", port);
        ports_blocked++;
      } else {
        log_warning("Port %d binding failed: 0x%08X", port, result);
        ports_blocked++;
      }
    } else {
      log_info("Port %d is available", port);
    }

    sceNetSocketClose(test_socket);
  }

  if (ports_blocked > 0) {
    log_warning("IMPORTANT: %d out of %zu discovery ports are blocked",
                ports_blocked,
                sizeof(discovery_ports) / sizeof(discovery_ports[0]));
    log_info("Using Chiaki-style ephemeral port discovery to avoid conflicts");
    log_info("This should work correctly with PS5 Remote Play");
  } else {
    log_info("All discovery ports are available");
  }

  log_info("=== PORT CONFLICT CHECK COMPLETE ===");
}

/* REMOVED: ps5_discovery_check_console_state() function
 *
 * This function has been removed because:
 * 1. PS5 responds to broadcast discovery but ignores individual state checks
 * 2. VitaRPS5 now uses discovery-based state detection like Vitaki
 * 3. State information comes from discovery response HTTP status codes:
 *    - HTTP/1.1 200 OK = CONSOLE_STATE_READY
 *    - HTTP/1.1 620 Server Standby = CONSOLE_STATE_STANDBY
 *
 * Console state is now updated via:
 * - Continuous discovery scans with periodic callbacks
 * - Discovery responses automatically update console cache
 * - UI receives state updates through discovery callback system
 */

// ============================================================================
// PS5 Console Wake Functionality (Chiaki-ng/Vitaki Compatible)
// ============================================================================

/**
 * Create wake packet payload using proven Chiaki-ng/Vitaki format
 * Returns the packet size or negative error code
 */
static int create_wake_packet(char* buffer, size_t buffer_size,
                              uint64_t user_credential, bool is_ps5) {
  if (!buffer || buffer_size < 256) {
    return -1;
  }

  // Use identical wake packet format from Chiaki-ng/Vitaki
  const char* protocol_version = is_ps5 ? "00030010" : "00020020";

  int packet_size =
      snprintf(buffer, buffer_size,
               "WAKEUP * HTTP/1.1\n"
               "client-type:vr\n"
               "auth-type:R\n"
               "model:w\n"
               "app-type:r\n"
               "user-credential:%llu\n"
               "device-discovery-protocol-version:%s\n",
               (unsigned long long)user_credential, protocol_version);

  if (packet_size >= (int)buffer_size) {
    log_error("Wake packet buffer too small: need %d, have %zu", packet_size,
              buffer_size);
    return -1;
  }

  return packet_size;
}

/**
 * Send wake packet to specific console IP address
 */
static VitaRPS5Result send_wake_packet(const char* ip_address,
                                       const char* wake_packet, int packet_size,
                                       bool is_ps5) {
  if (!ip_address || !wake_packet || packet_size <= 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Create UDP socket for wake transmission
  int sock = sceNetSocket("wake_udp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
  if (sock < 0) {
    log_error("Failed to create wake socket: 0x%08X", sock);
    return VITARPS5_ERROR_NETWORK;
  }

  // Set broadcast option (even though we're sending unicast, this is required)
  int broadcast = 1;
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &broadcast,
                   sizeof(broadcast));

  // Set socket to non-blocking to prevent UI freezes
  int nonblock = 1;
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nonblock,
                   sizeof(nonblock));

  // Set send timeout
  uint32_t timeout_ms =
      VITARPS5_TIMEOUT_SECONDS * 1000;  // Global timeout for better reliability
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &timeout_ms,
                   sizeof(timeout_ms));

  // Configure destination address
  SceNetSockaddrIn dest_addr;
  memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.sin_family = SCE_NET_AF_INET;
  dest_addr.sin_port =
      sceNetHtons(is_ps5 ? PS5_DISCOVERY_PORT_PS5 : PS4_DISCOVERY_PORT_PS4);

  int ip_result =
      sceNetInetPton(SCE_NET_AF_INET, ip_address, &dest_addr.sin_addr);
  if (ip_result != 1) {
    log_error("Invalid IP address for wake: %s", ip_address);
    sceNetSocketClose(sock);
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Sending wake packet to %s:%d (%s)", ip_address,
           is_ps5 ? PS5_DISCOVERY_PORT_PS5 : PS4_DISCOVERY_PORT_PS4,
           is_ps5 ? "PS5" : "PS4");

  // Send wake packet
  int sent = sceNetSendto(sock, wake_packet, packet_size, 0,
                          (SceNetSockaddr*)&dest_addr, sizeof(dest_addr));

  sceNetSocketClose(sock);

  if (sent < 0) {
    log_error("Failed to send wake packet: 0x%08X", sent);
    return VITARPS5_ERROR_NETWORK;
  }

  if (sent != packet_size) {
    log_warning("Wake packet partially sent: %d/%d bytes", sent, packet_size);
  } else {
    log_info("Wake packet sent successfully (%d bytes)", sent);
  }

  return VITARPS5_SUCCESS;
}

// Wait for console to respond to discovery after wake signal
// Sends discovery broadcasts and waits for console response
static VitaRPS5Result ps5_discovery_wait_for_wake_confirmation(
    const char* ip_address, bool is_ps5) {
  log_info("Waiting for %s to wake and respond to discovery broadcasts...",
           ip_address);

  const int MAX_WAKE_ATTEMPTS = 2;       // Reduced from 3 to prevent UI freeze
  const int DISCOVERY_TIMEOUT_SEC = 5;   // Reduced from 10 to prevent UI freeze
  const int DISCOVERY_INTERVAL_SEC = 1;  // Reduced from 2 for faster response

  for (int attempt = 1; attempt <= MAX_WAKE_ATTEMPTS; attempt++) {
    log_info(
        "Wake confirmation attempt %d/%d - sending discovery broadcasts to %s",
        attempt, MAX_WAKE_ATTEMPTS, ip_address);

    // Send discovery broadcasts for DISCOVERY_TIMEOUT_SEC seconds
    int discoveries_sent = 0;
    SceUInt64 start_time = sceKernelGetSystemTimeWide();

    while (true) {
      SceUInt64 current_time = sceKernelGetSystemTimeWide();
      SceUInt64 elapsed_ms =
          (current_time - start_time) / 1000;  // Convert to milliseconds

      if (elapsed_ms > (DISCOVERY_TIMEOUT_SEC * 1000)) {
        break;  // Timeout reached
      }

      // Send discovery broadcast every 2 seconds
      if ((elapsed_ms / 1000) >= (discoveries_sent * DISCOVERY_INTERVAL_SEC)) {
        log_debug("Sending discovery broadcast #%d to %s...",
                  discoveries_sent + 1, ip_address);

        // Try to get a response from console via discovery
        ConsoleDiscoveryState state = CONSOLE_DISCOVERY_STATE_UNKNOWN;
        PSConsoleType console_type = is_ps5 ? PS_CONSOLE_PS5 : PS_CONSOLE_PS4;
        VitaRPS5Result discovery_result =
            ps5_discovery_check_single_console_state(
                ip_address, console_type, CONSOLE_DISCOVERY_STATE_UNKNOWN,
                &state);

        if (discovery_result == VITARPS5_SUCCESS &&
            state == CONSOLE_DISCOVERY_STATE_READY) {
          log_info(
              "✅ Console %s responded to discovery and is READY - wake "
              "successful!",
              ip_address);
          return VITARPS5_SUCCESS;
        } else if (discovery_result == VITARPS5_SUCCESS) {
          const char* state_str =
              (state == CONSOLE_DISCOVERY_STATE_STANDBY)   ? "STANDBY"
              : (state == CONSOLE_DISCOVERY_STATE_UNKNOWN) ? "UNKNOWN"
                                                           : "INVALID";
          log_debug(
              "Console %s responded but state is %s (not READY) - continuing "
              "to wait...",
              ip_address, state_str);
        }

        discoveries_sent++;
      }

      // Sleep 500ms before checking again
      sceKernelDelayThread(500000);  // 0.5 seconds
    }

    log_warning(
        "Wake confirmation attempt %d failed - console %s not responding to "
        "discovery",
        attempt, ip_address);

    if (attempt < MAX_WAKE_ATTEMPTS) {
      log_info("Waiting 1 second before retry %d...", attempt + 1);
      sceKernelDelayThread(
          1000000);  // 1 second - reduced from 3 to prevent UI freeze
    }
  }

  log_error(
      "❌ Console %s failed to respond after %d wake attempts - may be off or "
      "network issue",
      ip_address, MAX_WAKE_ATTEMPTS);
  return VITARPS5_ERROR_TIMEOUT;
}

VitaRPS5Result ps5_discovery_wake_console(const PS5ConsoleInfo* console) {
  if (!console) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("=== WAKING PS5 CONSOLE ===");
  log_info("Console: %s (%s)", console->device_name, console->ip_address);

  // RESEARCHER FIX D: Use unified accessor for consistent credential lookup
  RegistrationCredentials creds;
  bool has_valid_creds = registration_get_by_ip(console->ip_address, &creds);

  if (!has_valid_creds) {
    log_error("Cannot wake console %s - no valid credentials available",
              console->ip_address);
    log_error("Unified accessor returned: invalid/missing credentials");
    return VITARPS5_ERROR_NOT_REGISTERED;
  }

  log_info("✅ Valid credentials found for wake operation");
  log_info("Console name: %s", creds.console_name);
  log_info("Wake credential: %s", creds.wake_credential_dec);

  // RESEARCHER FIX D: Skip corruption cleanup since unified accessor already
  // validated credentials

  // Validate registration key format
  size_t reg_key_len = strlen(creds.regkey_hex8);
  if (reg_key_len != 8) {
    log_error("Registration key has invalid length: %zu (expected: 8)",
              reg_key_len);
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Use wake credential from unified accessor
  if (strlen(creds.wake_credential_dec) > 0) {
    log_info("Using wake credential from unified accessor: %s",
             creds.wake_credential_dec);

    // Use the unified wake function
    VitaRPS5Result wake_result =
        console_registration_wake_console(console->ip_address);
    if (wake_result != VITARPS5_SUCCESS) {
      log_error("Failed to wake console %s: %s", console->ip_address,
                vitarps5_result_string(wake_result));
      return wake_result;
    }

    log_info("Wake packet sent to console %s (%s)", console->ip_address,
             creds.console_name);
  } else {
    // No wake credential available
    log_error("No wake credential available for console %s",
              console->ip_address);
    log_error("Wake credential should be set during PS5 registration process");
    log_info("Try re-registering the console to generate wake credentials");
    return VITARPS5_ERROR_INVALID_CREDENTIALS;
  }

  // Wake handled successfully - no additional processing needed
  return VITARPS5_SUCCESS;
}

// Console State Checking via HTTP (PS5 Protocol Implementation)

static VitaRPS5Result check_console_state_http(const char* console_ip,
                                               ConsoleState* state) {
  if (!console_ip || !state) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_debug("Checking console state via HTTP for %s", console_ip);

  int http_status_code;
  VitaRPS5Result result =
      send_http_state_request(console_ip, &http_status_code);

  if (result != VITARPS5_SUCCESS) {
    log_debug("HTTP state request failed for %s: %s", console_ip,
              vitarps5_result_string(result));
    *state = CONSOLE_STATE_UNKNOWN;
    return result;
  }

  *state = parse_http_status_to_state(http_status_code);

  log_info("Console %s HTTP state check: status=%d, state=%s", console_ip,
           http_status_code,
           (*state == CONSOLE_STATE_READY)     ? "READY"
           : (*state == CONSOLE_STATE_STANDBY) ? "STANDBY"
                                               : "UNKNOWN");

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result send_http_state_request(const char* console_ip,
                                              int* http_status_code) {
  if (!console_ip || !http_status_code) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  *http_status_code = 0;

  // Create TCP socket
  int sock =
      sceNetSocket("ps5_state_check", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
  if (sock < 0) {
    log_error("Failed to create HTTP state check socket: 0x%08X", sock);
    return VITARPS5_ERROR_NETWORK;
  }

  // Set socket to non-blocking to prevent UI freezes
  int nonblock = 1;
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nonblock,
                   sizeof(nonblock));

  // Set socket timeout (short timeout for quick state check)
  uint32_t timeout_ms =
      VITARPS5_TIMEOUT_SECONDS * 1000;  // Global timeout for reliable discovery
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &timeout_ms,
                   sizeof(timeout_ms));
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &timeout_ms,
                   sizeof(timeout_ms));

  // Connect to console on port 9295 (control port)
  SceNetSockaddrIn addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(9295);  // PS5 control port
  sceNetInetPton(SCE_NET_AF_INET, console_ip, &addr.sin_addr);

  int connect_result =
      sceNetConnect(sock, (SceNetSockaddr*)&addr, sizeof(addr));
  if (connect_result < 0) {
    sceNetSocketClose(sock);

    // CORRECTED STATE DETECTION: Connection refusal is ambiguous - could be
    // standby or network issue Don't assume connection refusal = standby, as
    // this causes false detection
    log_debug("Failed to connect to %s:9295 for state check: 0x%08X",
              console_ip, connect_result);

    if (connect_result == 0x80410123) {  // Connection refused
      log_info(
          "Connection to %s:9295 refused - could be standby, network issue, or "
          "RP service not ready",
          console_ip);
      // Return network error instead of assuming standby - let UDP discovery
      // determine actual state
      return VITARPS5_ERROR_NETWORK;
    } else if (connect_result == 0x80410146) {  // Network unreachable
      log_warning("Network unreachable to %s:9295 - check network connectivity",
                  console_ip);
      return VITARPS5_ERROR_NETWORK;
    } else if (connect_result == 0x80410141) {  // Connection timeout
      log_warning(
          "Connection timeout to %s:9295 - console may be slow to respond",
          console_ip);
      return VITARPS5_ERROR_TIMEOUT;
    } else {
      log_error("Unexpected network error connecting to %s:9295: 0x%08X",
                console_ip, connect_result);
      return VITARPS5_ERROR_NETWORK;
    }
  }

  // Send minimal HTTP GET request to check state
  const char* http_request =
      "GET / HTTP/1.1\r\n"
      "Host: %s:9295\r\n"
      "Connection: close\r\n"
      "\r\n";

  char request_buffer[256];
  snprintf(request_buffer, sizeof(request_buffer), http_request, console_ip);

  int sent = sceNetSend(sock, request_buffer, strlen(request_buffer), 0);
  if (sent < 0) {
    log_debug("Failed to send HTTP state request to %s: 0x%08X", console_ip,
              sent);
    sceNetSocketClose(sock);
    return VITARPS5_ERROR_NETWORK;
  }

  // Read HTTP response (we only need the status line)
  char response_buffer[512];
  int received =
      sceNetRecv(sock, response_buffer, sizeof(response_buffer) - 1, 0);
  sceNetSocketClose(sock);

  if (received <= 0) {
    log_debug("Failed to receive HTTP state response from %s: 0x%08X",
              console_ip, received);
    return VITARPS5_ERROR_NETWORK;
  }

  response_buffer[received] = '\0';

  // Parse HTTP status code from response
  int status_code;
  if (sscanf(response_buffer, "HTTP/1.1 %d", &status_code) == 1 ||
      sscanf(response_buffer, "HTTP/1.0 %d", &status_code) == 1) {
    *http_status_code = status_code;
    log_debug("HTTP state response from %s: %d", console_ip, status_code);
    return VITARPS5_SUCCESS;
  } else {
    log_debug("Failed to parse HTTP status from response: %.100s",
              response_buffer);
    return VITARPS5_ERROR_PROTOCOL;
  }
}

static ConsoleState parse_http_status_to_state(int http_status_code) {
  switch (http_status_code) {
    case 200:
      // HTTP 200 OK = Console is awake and ready for connections
      return CONSOLE_STATE_READY;

    case 620:
      // HTTP 620 = Console is in standby/sleep mode (PS5 specific)
      return CONSOLE_STATE_STANDBY;

    case 403:
    case 404:
    case 500:
      // Various error codes may indicate console is partially awake but not
      // ready
      return CONSOLE_STATE_UNKNOWN;

    default:
      // Any other status code is considered unknown state
      log_debug("Unknown HTTP status code for console state: %d",
                http_status_code);
      return CONSOLE_STATE_UNKNOWN;
  }
}

// UDP Discovery-Based Console State Checking (Protocol Compliant)

VitaRPS5Result ps5_discovery_check_single_console_state(
    const char* ip_address, PSConsoleType type,
    ConsoleDiscoveryState previous_state, ConsoleDiscoveryState* state) {
  if (!ip_address || !state) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_debug(
      "Checking single console state via UDP discovery: %s (type: %s, "
      "previous: %d)",
      ip_address, (type == PS_CONSOLE_PS5) ? "PS5" : "PS4", previous_state);

  // Use UDP discovery for state detection as specified in PS5 Remote Play
  // Protocol
  PS5DiscoveryConfig config = {0};
  config.scan_timeout_ms =
      1500;  // Quick 1.5-second scan for state check (optimized)
  config.scan_interval_ms = 300;  // Fast interval for state check (optimized)
  config.filter_local_network_only =
      true;  // Only local network for state checks

  PS5Discovery* discovery;
  VitaRPS5Result result = ps5_discovery_create(&config, &discovery);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create discovery instance for state check: %s",
              vitarps5_result_string(result));
    *state = CONSOLE_DISCOVERY_STATE_UNKNOWN;
    return result;
  }

  // Start quick discovery scan
  result = ps5_discovery_start_scan(discovery);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to start discovery scan for state check: %s",
              vitarps5_result_string(result));
    ps5_discovery_destroy(discovery);
    *state = CONSOLE_DISCOVERY_STATE_UNKNOWN;
    return result;
  }

  // Wait for scan completion
  uint64_t timeout_ms = get_timestamp_ms() + config.scan_timeout_ms;
  bool scan_completed = false;

  while (get_timestamp_ms() < timeout_ms && !scan_completed) {
    PS5DiscoveryResults results;
    ps5_discovery_get_results(discovery, &results);

    if (results.scan_complete) {
      scan_completed = true;
      break;
    }

    sceKernelDelayThread(100000);  // 100ms delay
  }

  // Check if our target console was found and get its state
  PS5DiscoveryResults final_results;
  result = ps5_discovery_get_results(discovery, &final_results);

  bool console_found = false;
  if (result == VITARPS5_SUCCESS) {
    for (uint32_t i = 0; i < final_results.console_count; i++) {
      const PS5ConsoleInfo* console = &final_results.consoles[i];

      // Match by IP address
      if (strcmp(console->ip_address, ip_address) == 0) {
        console_found = true;

        // Map discovery state to our internal state enum
        if (console->is_awake) {
          *state = CONSOLE_DISCOVERY_STATE_READY;
          log_info("UDP Discovery: Console %s is READY (awake)", ip_address);
        } else {
          *state = CONSOLE_DISCOVERY_STATE_STANDBY;
          log_info("UDP Discovery: Console %s is STANDBY (sleeping)",
                   ip_address);
        }
        break;
      }
    }
  }

  // Clean up discovery instance
  ps5_discovery_stop_scan(discovery);
  ps5_discovery_destroy(discovery);

  if (!console_found) {
    // Console not found in UDP discovery - check if it was previously known
    if (previous_state == CONSOLE_DISCOVERY_STATE_STANDBY) {
      // Preserve standby state for consoles that don't respond to UDP discovery
      // (deep sleep)
      *state = CONSOLE_DISCOVERY_STATE_STANDBY;
      log_info(
          "UDP Discovery: Console %s not found, preserving STANDBY state (deep "
          "sleep)",
          ip_address);
    } else {
      *state = CONSOLE_DISCOVERY_STATE_UNKNOWN;
      log_debug("UDP Discovery: Console %s not found, state unknown",
                ip_address);
    }
    return VITARPS5_ERROR_NOT_FOUND;
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result ps5_discovery_wait_for_ready(const char* ip_address,
                                            PSConsoleType type,
                                            uint32_t max_wait_ms,
                                            ConsoleState* final_state) {
  if (!ip_address || !final_state) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info(
      "Waiting for console %s to reach ready state via UDP discovery (max %u "
      "ms)",
      ip_address, max_wait_ms);

  uint64_t start_time = get_timestamp_ms();
  uint64_t end_time = start_time + max_wait_ms;

  *final_state = CONSOLE_STATE_UNKNOWN;

  while (get_timestamp_ms() < end_time) {
    // Use UDP discovery-based state checking instead of HTTP
    ConsoleDiscoveryState discovery_state;
    VitaRPS5Result result = ps5_discovery_check_single_console_state(
        ip_address, type, CONSOLE_DISCOVERY_STATE_UNKNOWN, &discovery_state);

    if (result == VITARPS5_SUCCESS &&
        discovery_state == CONSOLE_DISCOVERY_STATE_READY) {
      *final_state = CONSOLE_STATE_READY;
      uint64_t elapsed = get_timestamp_ms() - start_time;
      log_info("Console %s reached ready state after %llu ms via UDP discovery",
               ip_address, elapsed);
      return VITARPS5_SUCCESS;
    }

    // Map discovery state to console state for reporting
    switch (discovery_state) {
      case CONSOLE_DISCOVERY_STATE_READY:
        *final_state = CONSOLE_STATE_READY;
        break;
      case CONSOLE_DISCOVERY_STATE_STANDBY:
        *final_state = CONSOLE_STATE_STANDBY;
        break;
      default:
        *final_state = CONSOLE_STATE_UNKNOWN;
        break;
    }

    // Wait 2 seconds before checking again (UDP discovery is more resource
    // intensive)
    sceKernelDelayThread(2000 * 1000);  // 2 seconds in microseconds
  }

  // Timeout reached
  uint64_t elapsed = get_timestamp_ms() - start_time;
  log_warning(
      "Console %s did not reach ready state within %u ms (actual: %llu ms)",
      ip_address, max_wait_ms, elapsed);

  // Check final state one more time
  ConsoleDiscoveryState final_discovery_state;
  ps5_discovery_check_single_console_state(ip_address, type,
                                           CONSOLE_DISCOVERY_STATE_UNKNOWN,
                                           &final_discovery_state);

  switch (final_discovery_state) {
    case CONSOLE_DISCOVERY_STATE_READY:
      *final_state = CONSOLE_STATE_READY;
      break;
    case CONSOLE_DISCOVERY_STATE_STANDBY:
      *final_state = CONSOLE_STATE_STANDBY;
      break;
    default:
      *final_state = CONSOLE_STATE_UNKNOWN;
      break;
  }

  return VITARPS5_ERROR_TIMEOUT;
}

// Ultra-lightweight state check optimized for UI status updates
VitaRPS5Result ps5_discovery_lightweight_state_check(const char* ip_address,
                                                     PSConsoleType type,
                                                     ConsoleState* state) {
  if (!ip_address || !state) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_debug("Lightweight state check for %s (type: %s)", ip_address,
            (type == PS_CONSOLE_PS5) ? "PS5" : "PS4");

  // Default state
  *state = CONSOLE_STATE_UNKNOWN;

  // Create UDP socket with minimal timeout
  int sock = sceNetSocket("lightweight_discovery", SCE_NET_AF_INET,
                          SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP);
  if (sock < 0) {
    log_debug("Failed to create lightweight discovery socket: 0x%08X", sock);
    return VITARPS5_ERROR_NETWORK;
  }

  // Set balanced timeout for reliable PS5 communication (2 seconds)
  uint32_t timeout_ms = 2000;
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &timeout_ms,
                   sizeof(timeout_ms));
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &timeout_ms,
                   sizeof(timeout_ms));

  // Enable broadcast
  int broadcast = 1;
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &broadcast,
                   sizeof(broadcast));

  // Create targeted discovery packet (matching Chiaki format)
  const char* discovery_packet;
  int packet_size;

  if (type == PS_CONSOLE_PS5) {
    discovery_packet =
        "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00030010\n";
  } else {
    discovery_packet =
        "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00020020\n";
  }
  packet_size = strlen(discovery_packet);

  // Send broadcast discovery (PS5s only respond to broadcast, not unicast)
  SceNetSockaddrIn broadcast_addr;
  memset(&broadcast_addr, 0, sizeof(broadcast_addr));
  broadcast_addr.sin_family = SCE_NET_AF_INET;
  broadcast_addr.sin_addr.s_addr =
      SCE_NET_INADDR_BROADCAST;  // Broadcast to 255.255.255.255
  broadcast_addr.sin_port =
      sceNetHtons((type == PS_CONSOLE_PS5) ? PS5_DISCOVERY_PORT_PS5
                                           : PS4_DISCOVERY_PORT_PS4);

  // Send broadcast discovery packet
  int sent =
      sceNetSendto(sock, discovery_packet, packet_size, 0,
                   (SceNetSockaddr*)&broadcast_addr, sizeof(broadcast_addr));
  if (sent < 0) {
    log_debug("Failed to send lightweight broadcast discovery: 0x%08X", sent);
    sceNetSocketClose(sock);
    return VITARPS5_ERROR_NETWORK;
  }

  log_debug("Sent lightweight broadcast discovery (%d bytes)", sent);

  // Try to receive responses with short timeout, looking for our target console
  uint8_t response_buffer[1024];
  SceNetSockaddrIn from_addr;
  unsigned int from_len = sizeof(from_addr);

  uint64_t end_time = get_timestamp_ms() + timeout_ms;
  bool found_target_console = false;

  while (get_timestamp_ms() < end_time && !found_target_console) {
    int received =
        sceNetRecvfrom(sock, response_buffer, sizeof(response_buffer), 0,
                       (SceNetSockaddr*)&from_addr, &from_len);

    if (received > 0) {
      // Convert sender IP to string for comparison
      char sender_ip[16];
      sceNetInetNtop(SCE_NET_AF_INET, &from_addr.sin_addr, sender_ip,
                     sizeof(sender_ip));

      log_debug("Received lightweight discovery response from %s (%d bytes)",
                sender_ip, received);

      // Check if this response is from our target console
      if (strcmp(sender_ip, ip_address) == 0) {
        found_target_console = true;

        // Parse the response to determine actual state
        // Look for HTTP status line in the response
        response_buffer[received < 1023 ? received : 1023] = '\0';
        char* response_str = (char*)response_buffer;

        if (strstr(response_str, "HTTP/1.1 200") ||
            strstr(response_str, "HTTP/1.0 200")) {
          *state = CONSOLE_STATE_READY;
          log_debug("Lightweight state check result: %s -> READY (HTTP 200)",
                    ip_address);
        } else if (strstr(response_str, "HTTP/1.1 620") ||
                   strstr(response_str, "HTTP/1.0 620")) {
          *state = CONSOLE_STATE_STANDBY;
          log_debug("Lightweight state check result: %s -> STANDBY (HTTP 620)",
                    ip_address);
        } else {
          // Any response means console is responding, assume READY
          *state = CONSOLE_STATE_READY;
          log_debug(
              "Lightweight state check result: %s -> READY (response received)",
              ip_address);
        }
        break;
      } else {
        log_debug("Ignoring response from %s (looking for %s)", sender_ip,
                  ip_address);
      }
    } else if (received < 0) {
      // Check if it's timeout or would block (non-fatal)
      if (received != SCE_NET_ERROR_EAGAIN &&
          received != SCE_NET_ERROR_EWOULDBLOCK) {
        log_debug("Network error receiving discovery response: 0x%08X",
                  received);
        break;
      }
    }

    // Small delay to prevent busy waiting
    sceKernelDelayThread(10000);  // 10ms
  }

  sceNetSocketClose(sock);

  if (!found_target_console) {
    // No response from target console - assume standby
    log_debug("No response to lightweight discovery from %s - assuming standby",
              ip_address);
    *state = CONSOLE_STATE_STANDBY;
  }

  return VITARPS5_SUCCESS;
}
