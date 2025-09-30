#ifndef VITARPS5_CONTROL_PLANE_H
#define VITARPS5_CONTROL_PLANE_H

/**
 * PS5 Control Plane Protocol Implementation
 *
 * Implements the control plane messaging protocol that runs on TCP 9295
 * after successful HTTP session initialization. This handles:
 *
 * - BANG keep-alive messages (periodic heartbeat)
 * - Version negotiation with PS5 console
 * - Input event forwarding
 * - Session control and monitoring
 *
 * Based on PS5_REMOTE_PLAY_PROTOCOL_ANALYSIS.md requirements.
 */

#include <psp2/types.h>
#include <stdbool.h>
#include <stdint.h>

#include "../core/error_codes.h"
#include "../core/vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

// Control plane message types (based on Chiaki/Vitaki protocol analysis)
typedef enum {
  CONTROL_MSG_BANG = 0x01,          // Keep-alive heartbeat
  CONTROL_MSG_VERSION_REQ = 0x02,   // Version negotiation request
  CONTROL_MSG_VERSION_RSP = 0x03,   // Version negotiation response
  CONTROL_MSG_INPUT = 0x04,         // Input event
  CONTROL_MSG_SESSION_CTRL = 0x05,  // Session control
  CONTROL_MSG_ERROR = 0xFF          // Error response
} ControlPlaneMessageType;

// Control plane message structure
typedef struct {
  uint8_t message_type;   // ControlPlaneMessageType
  uint8_t flags;          // Message flags
  uint16_t payload_size;  // Size of payload data
  uint8_t payload[1024];  // Variable payload data
} ControlPlaneMessage;

// Control plane configuration
typedef struct {
  char console_ip[16];             // Console IP address
  uint16_t control_port;           // Control port (usually 9295)
  char session_id[64];             // Session ID from HTTP init
  uint32_t keepalive_interval_ms;  // BANG message interval
  uint32_t connection_timeout_ms;  // Connection timeout
  bool enable_input_forwarding;    // Forward input events

  // Callbacks
  void (*on_version_negotiated)(const char* server_version, void* user_data);
  void (*on_connection_lost)(void* user_data);
  void (*on_error)(VitaRPS5Result error, const char* message, void* user_data);
  void* user_data;
} ControlPlaneConfig;

// Control plane context (opaque)
typedef struct ControlPlaneContext ControlPlaneContext;

// Control Plane API

/**
 * Initialize control plane subsystem
 */
VitaRPS5Result control_plane_init(void);

/**
 * Cleanup control plane subsystem
 */
void control_plane_cleanup(void);

/**
 * Create control plane context
 */
VitaRPS5Result control_plane_create_context(const ControlPlaneConfig* config,
                                            ControlPlaneContext** context);

/**
 * Destroy control plane context
 */
void control_plane_destroy_context(ControlPlaneContext* context);

/**
 * Connect to control plane (after HTTP session init)
 */
VitaRPS5Result control_plane_connect(ControlPlaneContext* context);

/**
 * Disconnect from control plane
 */
VitaRPS5Result control_plane_disconnect(ControlPlaneContext* context);

/**
 * Update control plane (call periodically - handles BANG messages)
 */
VitaRPS5Result control_plane_update(ControlPlaneContext* context);

/**
 * Send input event through control plane
 */
VitaRPS5Result control_plane_send_input(ControlPlaneContext* context,
                                        const uint8_t* input_data,
                                        size_t input_size);

/**
 * Send custom control message
 */
VitaRPS5Result control_plane_send_message(ControlPlaneContext* context,
                                          ControlPlaneMessageType msg_type,
                                          const uint8_t* payload,
                                          size_t payload_size);

// Status and Information

/**
 * Check if control plane is connected
 */
bool control_plane_is_connected(const ControlPlaneContext* context);

/**
 * Get negotiated protocol version
 */
const char* control_plane_get_server_version(
    const ControlPlaneContext* context);

/**
 * Get control plane statistics
 */
typedef struct {
  uint64_t messages_sent;
  uint64_t messages_received;
  uint64_t bang_messages_sent;
  uint32_t current_latency_ms;
  uint64_t last_bang_time_ms;
  bool version_negotiated;
} ControlPlaneStats;

VitaRPS5Result control_plane_get_stats(const ControlPlaneContext* context,
                                       ControlPlaneStats* stats);

// Protocol Helper Functions

/**
 * Create BANG keep-alive message
 */
VitaRPS5Result control_plane_create_bang_message(ControlPlaneMessage* message);

/**
 * Create version negotiation request
 */
VitaRPS5Result control_plane_create_version_request(
    ControlPlaneMessage* message, const char* client_version);

/**
 * Parse received control plane message
 */
VitaRPS5Result control_plane_parse_message(const uint8_t* data,
                                           size_t data_size,
                                           ControlPlaneMessage* message);

// Integration Functions

/**
 * Build control plane config from session init response
 */
VitaRPS5Result control_plane_build_config_from_session_init(
    const char* console_ip, uint16_t control_port, const char* session_id,
    ControlPlaneConfig* config);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_CONTROL_PLANE_H