#include "session_keepalive.h"

#include <errno.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logger.h"
#include "network_manager.h"

// Keepalive context structure
struct SessionKeepaliveContext {
  KeepaliveConfig config;
  KeepaliveStatus status;
  KeepaliveStats stats;

  // Threading
  SceUID keepalive_thread;
  bool thread_running;
  bool should_stop;

  // Network
  int udp_socket;
  SceNetSockaddrIn target_addr;

  // Timing
  uint64_t last_send_time;
  uint64_t session_start_time;
};

// Global state
static bool keepalive_subsystem_initialized = false;

// Internal function declarations
static int keepalive_thread_func(SceSize args, void* argp);
static VitaRPS5Result send_heartbeat_packet(SessionKeepaliveContext* context);
static VitaRPS5Result receive_heartbeat_response(
    SessionKeepaliveContext* context);
static uint64_t get_timestamp_ms(void);
static void update_keepalive_stats(SessionKeepaliveContext* context,
                                   bool response_received, uint32_t rtt_ms);

// API Implementation

VitaRPS5Result session_keepalive_init(void) {
  if (keepalive_subsystem_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing session keepalive subsystem");

  // Initialize network manager if needed
  VitaRPS5Result result = network_manager_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize network manager for keepalive: %s",
              vitarps5_result_string(result));
    return result;
  }

  keepalive_subsystem_initialized = true;
  log_info("Session keepalive subsystem initialized successfully");

  return VITARPS5_SUCCESS;
}

void session_keepalive_cleanup(void) {
  if (!keepalive_subsystem_initialized) {
    return;
  }

  log_info("Cleaning up session keepalive subsystem");
  keepalive_subsystem_initialized = false;
  log_info("Session keepalive cleanup complete");
}

VitaRPS5Result session_keepalive_create(const KeepaliveConfig* config,
                                        SessionKeepaliveContext** context) {
  if (!keepalive_subsystem_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!config || !context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  SessionKeepaliveContext* new_context =
      malloc(sizeof(SessionKeepaliveContext));
  if (!new_context) {
    return VITARPS5_ERROR_MEMORY;
  }

  // Initialize context
  memset(new_context, 0, sizeof(SessionKeepaliveContext));
  new_context->config = *config;
  new_context->status = KEEPALIVE_STATUS_INACTIVE;
  new_context->thread_running = false;
  new_context->should_stop = false;
  new_context->udp_socket = -1;

  // Set default timing if not specified
  if (new_context->config.interval_ms == 0) {
    new_context->config.interval_ms = KEEPALIVE_INTERVAL_MS;
  }
  if (new_context->config.timeout_ms == 0) {
    new_context->config.timeout_ms = KEEPALIVE_TIMEOUT_MS;
  }
  if (new_context->config.max_failures == 0) {
    new_context->config.max_failures = KEEPALIVE_MAX_FAILURES;
  }

  // Setup target address
  memset(&new_context->target_addr, 0, sizeof(new_context->target_addr));
  new_context->target_addr.sin_family = SCE_NET_AF_INET;
  new_context->target_addr.sin_port = sceNetHtons(config->console_port);
  sceNetInetPton(SCE_NET_AF_INET, config->console_ip,
                 &new_context->target_addr.sin_addr);

  log_info("Created keepalive context for %s:%d", config->console_ip,
           config->console_port);

  *context = new_context;
  return VITARPS5_SUCCESS;
}

void session_keepalive_destroy(SessionKeepaliveContext* context) {
  if (!context) {
    return;
  }

  log_info("Destroying keepalive context");

  // Stop keepalive if running
  if (context->status == KEEPALIVE_STATUS_ACTIVE) {
    session_keepalive_stop(context);
  }

  // Wait for thread to finish
  if (context->thread_running && context->keepalive_thread >= 0) {
    context->should_stop = true;
    SceUInt timeout = 3000000;  // 3 seconds
    sceKernelWaitThreadEnd(context->keepalive_thread, NULL, &timeout);
    sceKernelDeleteThread(context->keepalive_thread);
  }

  // Close socket
  if (context->udp_socket >= 0) {
    sceNetSocketClose(context->udp_socket);
  }

  free(context);
  log_debug("Keepalive context destroyed");
}

VitaRPS5Result session_keepalive_start(SessionKeepaliveContext* context) {
  if (!context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (context->status == KEEPALIVE_STATUS_ACTIVE) {
    log_warn("Keepalive already active");
    return VITARPS5_SUCCESS;
  }

  log_info("🔄 Starting PS5 session keepalive to %s:%d",
           context->config.console_ip, context->config.console_port);

  // Create UDP socket for heartbeat
  context->udp_socket =
      sceNetSocket("keepalive", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
  if (context->udp_socket < 0) {
    log_error("Failed to create keepalive UDP socket: 0x%08X",
              context->udp_socket);
    return VITARPS5_ERROR_NETWORK;
  }

  // Set socket timeout
  uint32_t timeout = context->config.timeout_ms;
  sceNetSetsockopt(context->udp_socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO,
                   &timeout, sizeof(timeout));

  // Reset stats
  memset(&context->stats, 0, sizeof(context->stats));
  context->session_start_time = get_timestamp_ms();

  // Create and start keepalive thread
  context->keepalive_thread = sceKernelCreateThread(
      "keepalive", keepalive_thread_func, 0x10000100, 0x10000, 0, 0, NULL);

  if (context->keepalive_thread < 0) {
    log_error("Failed to create keepalive thread: 0x%08X",
              context->keepalive_thread);
    sceNetSocketClose(context->udp_socket);
    context->udp_socket = -1;
    return VITARPS5_ERROR_INIT;
  }

  context->thread_running = true;
  context->should_stop = false;
  context->status = KEEPALIVE_STATUS_ACTIVE;

  sceKernelStartThread(context->keepalive_thread,
                       sizeof(SessionKeepaliveContext*), &context);

  log_info("✅ Keepalive started successfully (interval: %ums)",
           context->config.interval_ms);

  // Notify callback
  if (context->config.status_callback) {
    context->config.status_callback(KEEPALIVE_STATUS_ACTIVE, &context->stats,
                                    context->config.user_data);
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result session_keepalive_stop(SessionKeepaliveContext* context) {
  if (!context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (context->status == KEEPALIVE_STATUS_INACTIVE) {
    return VITARPS5_SUCCESS;
  }

  log_info("Stopping keepalive");

  // Signal thread to stop
  context->should_stop = true;

  // Wait for thread to finish
  if (context->thread_running && context->keepalive_thread >= 0) {
    SceUInt timeout = 2000000;  // 2 seconds
    sceKernelWaitThreadEnd(context->keepalive_thread, NULL, &timeout);
    sceKernelDeleteThread(context->keepalive_thread);
    context->thread_running = false;
  }

  // Close socket
  if (context->udp_socket >= 0) {
    sceNetSocketClose(context->udp_socket);
    context->udp_socket = -1;
  }

  context->status = KEEPALIVE_STATUS_INACTIVE;

  // Notify callback
  if (context->config.status_callback) {
    context->config.status_callback(KEEPALIVE_STATUS_INACTIVE, &context->stats,
                                    context->config.user_data);
  }

  log_info("Keepalive stopped");
  return VITARPS5_SUCCESS;
}

KeepaliveStatus session_keepalive_get_status(
    const SessionKeepaliveContext* context) {
  if (!context) {
    return KEEPALIVE_STATUS_INACTIVE;
  }
  return context->status;
}

VitaRPS5Result session_keepalive_get_stats(
    const SessionKeepaliveContext* context, KeepaliveStats* stats) {
  if (!context || !stats) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  *stats = context->stats;
  return VITARPS5_SUCCESS;
}

bool session_keepalive_is_active(const SessionKeepaliveContext* context) {
  return context && (context->status == KEEPALIVE_STATUS_ACTIVE);
}

// Integration Functions

VitaRPS5Result session_keepalive_start_after_init(
    const char* console_ip, uint16_t console_port,
    SessionKeepaliveContext** context) {
  if (!console_ip || !context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info(
      "🚀 CRITICAL: Starting keepalive immediately after successful session "
      "init");

  // Create keepalive config
  KeepaliveConfig config = {0};
  strncpy(config.console_ip, console_ip, sizeof(config.console_ip) - 1);
  config.console_port = console_port;
  config.interval_ms = KEEPALIVE_INTERVAL_MS;
  config.timeout_ms = KEEPALIVE_TIMEOUT_MS;
  config.max_failures = KEEPALIVE_MAX_FAILURES;

  // Create and start keepalive
  VitaRPS5Result result = session_keepalive_create(&config, context);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create keepalive context: %s",
              vitarps5_result_string(result));
    return result;
  }

  result = session_keepalive_start(*context);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to start keepalive: %s", vitarps5_result_string(result));
    session_keepalive_destroy(*context);
    *context = NULL;
    return result;
  }

  log_info("✅ Keepalive started successfully after session init");
  return VITARPS5_SUCCESS;
}

// Internal Implementation

static int keepalive_thread_func(SceSize args, void* argp) {
  if (args != sizeof(SessionKeepaliveContext*)) {
    log_error("Keepalive thread: Invalid argument size");
    return -1;
  }

  SessionKeepaliveContext* context = *(SessionKeepaliveContext**)argp;
  if (!context) {
    log_error("Keepalive thread: Invalid context");
    return -1;
  }

  log_debug("Keepalive thread started");

  while (!context->should_stop && context->status == KEEPALIVE_STATUS_ACTIVE) {
    uint64_t current_time = get_timestamp_ms();

    // Check if it's time to send heartbeat
    if (current_time - context->last_send_time >= context->config.interval_ms) {
      VitaRPS5Result result = send_heartbeat_packet(context);
      if (result != VITARPS5_SUCCESS) {
        context->stats.consecutive_failures++;
        log_warn("Heartbeat send failed (consecutive failures: %u/%u)",
                 context->stats.consecutive_failures,
                 context->config.max_failures);

        if (context->stats.consecutive_failures >=
            context->config.max_failures) {
          log_error("❌ Keepalive failed - too many consecutive failures");
          context->status = KEEPALIVE_STATUS_FAILED;

          if (context->config.status_callback) {
            context->config.status_callback(KEEPALIVE_STATUS_FAILED,
                                            &context->stats,
                                            context->config.user_data);
          }
          break;
        }
      } else {
        context->last_send_time = current_time;
        context->stats.packets_sent++;

        // Try to receive response (non-blocking)
        receive_heartbeat_response(context);
      }
    }

    // Sleep for 100ms before next iteration
    sceKernelDelayThread(100000);
  }

  context->thread_running = false;
  log_debug("Keepalive thread finished");
  return 0;
}

static VitaRPS5Result send_heartbeat_packet(SessionKeepaliveContext* context) {
  if (!context || context->udp_socket < 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Create simple heartbeat packet (matches chiaki-ng format)
  char heartbeat_packet[KEEPALIVE_PACKET_SIZE];
  memset(heartbeat_packet, 0, sizeof(heartbeat_packet));

  // Simple heartbeat format: "KEEP" + timestamp
  uint64_t timestamp = get_timestamp_ms();
  snprintf(heartbeat_packet, sizeof(heartbeat_packet), "KEEP:%llu", timestamp);

  int sent = sceNetSendto(
      context->udp_socket, heartbeat_packet, strlen(heartbeat_packet), 0,
      (SceNetSockaddr*)&context->target_addr, sizeof(context->target_addr));

  if (sent < 0) {
    log_error("Failed to send heartbeat packet: 0x%08X", sent);
    return VITARPS5_ERROR_NETWORK;
  }

  log_debug("💓 Heartbeat sent to %s:%d (%d bytes)", context->config.console_ip,
            context->config.console_port, sent);

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result receive_heartbeat_response(
    SessionKeepaliveContext* context) {
  if (!context || context->udp_socket < 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  char response_buffer[KEEPALIVE_PACKET_SIZE];
  SceNetSockaddrIn from_addr;
  unsigned int from_addr_len = sizeof(from_addr);

  int received = sceNetRecvfrom(
      context->udp_socket, response_buffer, sizeof(response_buffer) - 1,
      SCE_NET_MSG_DONTWAIT, (SceNetSockaddr*)&from_addr, &from_addr_len);

  if (received > 0) {
    response_buffer[received] = '\0';
    uint64_t current_time = get_timestamp_ms();

    // Calculate RTT if response contains timestamp
    uint32_t rtt_ms = 0;
    uint64_t sent_timestamp;
    if (sscanf(response_buffer, "KEEP:%llu", &sent_timestamp) == 1) {
      rtt_ms = (uint32_t)(current_time - sent_timestamp);
    }

    update_keepalive_stats(context, true, rtt_ms);
    log_debug("💚 Heartbeat response received (RTT: %ums)", rtt_ms);

    return VITARPS5_SUCCESS;
  } else if (received == 0x80410259) {  // Would block
    // No response available, this is normal
    return VITARPS5_SUCCESS;
  } else {
    log_debug("Failed to receive heartbeat response: 0x%08X", received);
    update_keepalive_stats(context, false, 0);
    return VITARPS5_ERROR_NETWORK;
  }
}

static uint64_t get_timestamp_ms(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick / 1000;
}

static void update_keepalive_stats(SessionKeepaliveContext* context,
                                   bool response_received, uint32_t rtt_ms) {
  if (!context) return;

  if (response_received) {
    context->stats.packets_received++;
    context->stats.consecutive_failures = 0;  // Reset failure count
    context->stats.last_response_time_ms = get_timestamp_ms();

    // Update average RTT (simple moving average)
    if (rtt_ms > 0) {
      if (context->stats.average_rtt_ms == 0) {
        context->stats.average_rtt_ms = rtt_ms;
      } else {
        context->stats.average_rtt_ms =
            (context->stats.average_rtt_ms * 3 + rtt_ms) / 4;
      }
    }
  }
}