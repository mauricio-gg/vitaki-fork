#include "control_plane.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logger.h"

// Control plane context structure
struct ControlPlaneContext {
  ControlPlaneConfig config;

  // Connection state
  int control_socket;
  bool is_connected;
  bool should_disconnect;

  // Protocol state
  bool version_negotiated;
  char server_version[32];

  // Keep-alive state
  uint64_t last_bang_time_ms;
  uint64_t last_received_time_ms;

  // Threading
  SceUID update_thread;
  bool thread_running;
  bool thread_should_stop;

  // Statistics
  ControlPlaneStats stats;
};

// Global state
static bool control_plane_subsystem_initialized = false;

// Internal function declarations
static int control_plane_thread_func(SceSize args, void* argp);
static VitaRPS5Result send_control_message(ControlPlaneContext* context,
                                           const ControlPlaneMessage* message);
static VitaRPS5Result receive_control_message(ControlPlaneContext* context,
                                              ControlPlaneMessage* message);
static VitaRPS5Result handle_received_message(
    ControlPlaneContext* context, const ControlPlaneMessage* message);
static uint64_t get_timestamp_ms(void);

// API Implementation

VitaRPS5Result control_plane_init(void) {
  if (control_plane_subsystem_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing PS5 control plane subsystem");
  control_plane_subsystem_initialized = true;
  log_info("PS5 control plane subsystem initialized successfully");

  return VITARPS5_SUCCESS;
}

void control_plane_cleanup(void) {
  if (!control_plane_subsystem_initialized) {
    return;
  }

  log_info("Cleaning up PS5 control plane subsystem");
  control_plane_subsystem_initialized = false;
  log_info("PS5 control plane cleanup complete");
}

VitaRPS5Result control_plane_create_context(const ControlPlaneConfig* config,
                                            ControlPlaneContext** context) {
  if (!control_plane_subsystem_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!config || !context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ControlPlaneContext* new_context = malloc(sizeof(ControlPlaneContext));
  if (!new_context) {
    return VITARPS5_ERROR_MEMORY;
  }

  // Initialize context
  memset(new_context, 0, sizeof(ControlPlaneContext));
  new_context->config = *config;
  new_context->control_socket = -1;
  new_context->is_connected = false;
  new_context->version_negotiated = false;
  new_context->thread_running = false;
  new_context->thread_should_stop = false;

  // Set default keepalive interval if not specified
  if (new_context->config.keepalive_interval_ms == 0) {
    new_context->config.keepalive_interval_ms = 1000;  // 1 second BANG interval
  }

  if (new_context->config.connection_timeout_ms == 0) {
    new_context->config.connection_timeout_ms = 10000;  // 10 second timeout
  }

  log_info("Created control plane context for %s:%d", config->console_ip,
           config->control_port);

  *context = new_context;
  return VITARPS5_SUCCESS;
}

void control_plane_destroy_context(ControlPlaneContext* context) {
  if (!context) {
    return;
  }

  log_info("Destroying control plane context");

  // Disconnect if connected
  if (context->is_connected) {
    control_plane_disconnect(context);
  }

  // Stop thread
  if (context->thread_running) {
    context->thread_should_stop = true;
    if (context->update_thread >= 0) {
      SceUInt timeout = 3000000;  // 3 seconds
      sceKernelWaitThreadEnd(context->update_thread, NULL, &timeout);
      sceKernelDeleteThread(context->update_thread);
    }
  }

  free(context);
  log_debug("Control plane context destroyed");
}

VitaRPS5Result control_plane_connect(ControlPlaneContext* context) {
  if (!context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (context->is_connected) {
    return VITARPS5_SUCCESS;
  }

  log_info("Connecting to control plane at %s:%d", context->config.console_ip,
           context->config.control_port);

  // Create TCP socket
  context->control_socket =
      sceNetSocket("control_plane", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
  if (context->control_socket < 0) {
    log_error("Failed to create control plane socket: 0x%08X",
              context->control_socket);
    return VITARPS5_ERROR_NETWORK;
  }

  // Set socket timeout
  uint32_t timeout_ms = context->config.connection_timeout_ms;
  sceNetSetsockopt(context->control_socket, SCE_NET_SOL_SOCKET,
                   SCE_NET_SO_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
  sceNetSetsockopt(context->control_socket, SCE_NET_SOL_SOCKET,
                   SCE_NET_SO_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));

  // Connect to console
  SceNetSockaddrIn addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(context->config.control_port);
  sceNetInetPton(SCE_NET_AF_INET, context->config.console_ip, &addr.sin_addr);

  int connect_result = sceNetConnect(context->control_socket,
                                     (SceNetSockaddr*)&addr, sizeof(addr));
  if (connect_result < 0) {
    log_error("Failed to connect to control plane: 0x%08X", connect_result);
    sceNetSocketClose(context->control_socket);
    context->control_socket = -1;
    return VITARPS5_ERROR_NETWORK;
  }

  context->is_connected = true;
  context->last_bang_time_ms = get_timestamp_ms();
  context->last_received_time_ms = context->last_bang_time_ms;

  log_info("✅ Control plane connected successfully");

  // Start update thread
  context->thread_should_stop = false;
  context->update_thread =
      sceKernelCreateThread("control_plane_update", control_plane_thread_func,
                            0x10000100, 0x10000, 0, 0, NULL);

  if (context->update_thread < 0) {
    log_error("Failed to create control plane thread: 0x%08X",
              context->update_thread);
    control_plane_disconnect(context);
    return VITARPS5_ERROR_INIT;
  }

  context->thread_running = true;
  sceKernelStartThread(context->update_thread, sizeof(ControlPlaneContext*),
                       &context);

  // Send initial version negotiation
  ControlPlaneMessage version_msg;
  VitaRPS5Result version_result =
      control_plane_create_version_request(&version_msg, "VitaRPS5-1.0");
  if (version_result == VITARPS5_SUCCESS) {
    send_control_message(context, &version_msg);
    log_info("Sent version negotiation request");
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result control_plane_disconnect(ControlPlaneContext* context) {
  if (!context || !context->is_connected) {
    return VITARPS5_SUCCESS;
  }

  log_info("Disconnecting from control plane");

  context->should_disconnect = true;
  context->is_connected = false;

  // Close socket
  if (context->control_socket >= 0) {
    sceNetSocketClose(context->control_socket);
    context->control_socket = -1;
  }

  log_info("Control plane disconnected");
  return VITARPS5_SUCCESS;
}

VitaRPS5Result control_plane_update(ControlPlaneContext* context) {
  if (!context || !context->is_connected) {
    return VITARPS5_ERROR_INVALID_STATE;
  }

  uint64_t current_time = get_timestamp_ms();

  // Check if we need to send BANG keep-alive
  if (current_time - context->last_bang_time_ms >=
      context->config.keepalive_interval_ms) {
    ControlPlaneMessage bang_msg;
    VitaRPS5Result bang_result = control_plane_create_bang_message(&bang_msg);
    if (bang_result == VITARPS5_SUCCESS) {
      VitaRPS5Result send_result = send_control_message(context, &bang_msg);
      if (send_result == VITARPS5_SUCCESS) {
        context->last_bang_time_ms = current_time;
        context->stats.bang_messages_sent++;
        log_debug("Sent BANG keep-alive message");
      } else {
        log_warning("Failed to send BANG message: %s",
                    vitarps5_result_string(send_result));
      }
    }
  }

  // Check connection timeout
  if (current_time - context->last_received_time_ms >
      context->config.connection_timeout_ms) {
    log_error("Control plane connection timeout");
    if (context->config.on_connection_lost) {
      context->config.on_connection_lost(context->config.user_data);
    }
    return VITARPS5_ERROR_TIMEOUT;
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result control_plane_send_input(ControlPlaneContext* context,
                                        const uint8_t* input_data,
                                        size_t input_size) {
  if (!context || !context->is_connected || !input_data) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (!context->config.enable_input_forwarding) {
    return VITARPS5_SUCCESS;  // Silently ignore if disabled
  }

  if (input_size > sizeof(((ControlPlaneMessage*)0)->payload)) {
    log_error("Input data too large: %zu > %zu", input_size,
              sizeof(((ControlPlaneMessage*)0)->payload));
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ControlPlaneMessage input_msg;
  input_msg.message_type = CONTROL_MSG_INPUT;
  input_msg.flags = 0;
  input_msg.payload_size = (uint16_t)input_size;
  memcpy(input_msg.payload, input_data, input_size);

  return send_control_message(context, &input_msg);
}

// Protocol Helper Functions Implementation

VitaRPS5Result control_plane_create_bang_message(ControlPlaneMessage* message) {
  if (!message) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  message->message_type = CONTROL_MSG_BANG;
  message->flags = 0;
  message->payload_size = 4;  // Timestamp payload

  uint32_t timestamp = (uint32_t)(get_timestamp_ms() & 0xFFFFFFFF);
  memcpy(message->payload, &timestamp, sizeof(timestamp));

  return VITARPS5_SUCCESS;
}

VitaRPS5Result control_plane_create_version_request(
    ControlPlaneMessage* message, const char* client_version) {
  if (!message || !client_version) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  message->message_type = CONTROL_MSG_VERSION_REQ;
  message->flags = 0;

  size_t version_len = strlen(client_version);
  if (version_len >= sizeof(message->payload)) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  message->payload_size = (uint16_t)version_len;
  memcpy(message->payload, client_version, version_len);

  return VITARPS5_SUCCESS;
}

// Internal Implementation

static int control_plane_thread_func(SceSize args, void* argp) {
  if (args != sizeof(ControlPlaneContext*)) {
    log_error("Control plane thread: Invalid argument size");
    return -1;
  }

  ControlPlaneContext* context = *(ControlPlaneContext**)argp;
  if (!context) {
    log_error("Control plane thread: Invalid context");
    return -1;
  }

  log_debug("Control plane update thread started");

  while (!context->thread_should_stop && context->is_connected) {
    // Update control plane (send BANG, check timeouts)
    control_plane_update(context);

    // Try to receive messages (non-blocking)
    ControlPlaneMessage received_msg;
    VitaRPS5Result recv_result =
        receive_control_message(context, &received_msg);
    if (recv_result == VITARPS5_SUCCESS) {
      handle_received_message(context, &received_msg);
      context->last_received_time_ms = get_timestamp_ms();
    }

    // Sleep briefly to avoid busy waiting
    sceKernelDelayThread(100 * 1000);  // 100ms
  }

  context->thread_running = false;
  log_debug("Control plane update thread completed");
  return 0;
}

static VitaRPS5Result send_control_message(ControlPlaneContext* context,
                                           const ControlPlaneMessage* message) {
  if (!context || !message || context->control_socket < 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Serialize message (simplified - in production would use proper protocol)
  size_t total_size = 4 + message->payload_size;  // header + payload
  uint8_t* buffer = malloc(total_size);
  if (!buffer) {
    return VITARPS5_ERROR_MEMORY;
  }

  buffer[0] = message->message_type;
  buffer[1] = message->flags;
  buffer[2] = (message->payload_size >> 8) & 0xFF;
  buffer[3] = message->payload_size & 0xFF;

  if (message->payload_size > 0) {
    memcpy(buffer + 4, message->payload, message->payload_size);
  }

  int sent = sceNetSend(context->control_socket, buffer, total_size, 0);
  free(buffer);

  if (sent < 0) {
    log_error("Failed to send control message: 0x%08X", sent);
    return VITARPS5_ERROR_NETWORK;
  }

  context->stats.messages_sent++;
  return VITARPS5_SUCCESS;
}

static VitaRPS5Result receive_control_message(ControlPlaneContext* context,
                                              ControlPlaneMessage* message) {
  if (!context || !message || context->control_socket < 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Try to receive header (non-blocking)
  uint8_t header[4];
  int received = sceNetRecv(context->control_socket, header, 4, 0);

  if (received == 0) {
    return VITARPS5_ERROR_NO_DATA;  // Connection closed
  } else if (received < 0) {
    // Check if it's just no data available (non-blocking)
    if (received == SCE_NET_ERROR_EAGAIN ||
        received == SCE_NET_ERROR_EWOULDBLOCK) {
      return VITARPS5_ERROR_NO_DATA;
    }
    return VITARPS5_ERROR_NETWORK;
  } else if (received != 4) {
    return VITARPS5_ERROR_PROTOCOL;  // Incomplete header
  }

  // Parse header
  message->message_type = header[0];
  message->flags = header[1];
  message->payload_size = ((uint16_t)header[2] << 8) | header[3];

  // Receive payload if any
  if (message->payload_size > 0) {
    if (message->payload_size > sizeof(message->payload)) {
      return VITARPS5_ERROR_BUFFER_TOO_SMALL;
    }

    int payload_received = sceNetRecv(context->control_socket, message->payload,
                                      message->payload_size, 0);
    if (payload_received != message->payload_size) {
      return VITARPS5_ERROR_PROTOCOL;
    }
  }

  context->stats.messages_received++;
  return VITARPS5_SUCCESS;
}

static VitaRPS5Result handle_received_message(
    ControlPlaneContext* context, const ControlPlaneMessage* message) {
  if (!context || !message) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  switch (message->message_type) {
    case CONTROL_MSG_VERSION_RSP:
      if (message->payload_size > 0 &&
          message->payload_size < sizeof(context->server_version)) {
        memcpy(context->server_version, message->payload,
               message->payload_size);
        context->server_version[message->payload_size] = '\0';
        context->version_negotiated = true;

        log_info("✅ Version negotiation completed: server version %s",
                 context->server_version);

        if (context->config.on_version_negotiated) {
          context->config.on_version_negotiated(context->server_version,
                                                context->config.user_data);
        }
      }
      break;

    case CONTROL_MSG_BANG:
      log_debug("Received BANG response from server");
      break;

    case CONTROL_MSG_ERROR:
      log_error("Received error message from control plane");
      if (context->config.on_error) {
        context->config.on_error(VITARPS5_ERROR_PROTOCOL, "Control plane error",
                                 context->config.user_data);
      }
      break;

    default:
      log_debug("Received unknown control message type: 0x%02X",
                message->message_type);
      break;
  }

  return VITARPS5_SUCCESS;
}

// Status Functions

bool control_plane_is_connected(const ControlPlaneContext* context) {
  return context && context->is_connected;
}

const char* control_plane_get_server_version(
    const ControlPlaneContext* context) {
  if (!context || !context->version_negotiated) {
    return NULL;
  }
  return context->server_version;
}

VitaRPS5Result control_plane_get_stats(const ControlPlaneContext* context,
                                       ControlPlaneStats* stats) {
  if (!context || !stats) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  *stats = context->stats;
  stats->last_bang_time_ms = context->last_bang_time_ms;
  stats->version_negotiated = context->version_negotiated;

  // Calculate current latency (simplified)
  uint64_t current_time = get_timestamp_ms();
  stats->current_latency_ms =
      (uint32_t)(current_time - context->last_received_time_ms);

  return VITARPS5_SUCCESS;
}

// Integration Helper

VitaRPS5Result control_plane_build_config_from_session_init(
    const char* console_ip, uint16_t control_port, const char* session_id,
    ControlPlaneConfig* config) {
  if (!console_ip || !session_id || !config) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  memset(config, 0, sizeof(ControlPlaneConfig));

  strncpy(config->console_ip, console_ip, sizeof(config->console_ip) - 1);
  config->control_port = control_port;
  strncpy(config->session_id, session_id, sizeof(config->session_id) - 1);

  // Set default values
  config->keepalive_interval_ms = 1000;   // 1 second
  config->connection_timeout_ms = 10000;  // 10 seconds
  config->enable_input_forwarding = true;

  return VITARPS5_SUCCESS;
}

// Utility Functions

static uint64_t get_timestamp_ms(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick / 1000;
}