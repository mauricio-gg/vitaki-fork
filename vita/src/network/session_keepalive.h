#ifndef VITARPS5_SESSION_KEEPALIVE_H
#define VITARPS5_SESSION_KEEPALIVE_H

/**
 * PS5 Session Keepalive/Heartbeat System
 *
 * Maintains active PS5 connection after successful session initialization
 * by sending periodic UDP heartbeat packets. This prevents the PS5 from
 * closing the connection before streaming setup is complete.
 *
 * Based on chiaki-ng heartbeat implementation patterns.
 */

#include <psp2/types.h>
#include <stdbool.h>
#include <stdint.h>

#include "../core/error_codes.h"
#include "../core/vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

// Keepalive constants
#define KEEPALIVE_INTERVAL_MS 3000  // Send heartbeat every 3 seconds
#define KEEPALIVE_TIMEOUT_MS 15000  // Consider connection dead after 15s
#define KEEPALIVE_MAX_FAILURES 5    // Max consecutive failures before giving up
#define KEEPALIVE_PACKET_SIZE 64    // UDP heartbeat packet size

// Keepalive context (opaque structure)
typedef struct SessionKeepaliveContext SessionKeepaliveContext;

// Keepalive status
typedef enum {
  KEEPALIVE_STATUS_INACTIVE = 0,
  KEEPALIVE_STATUS_ACTIVE,
  KEEPALIVE_STATUS_FAILED,
  KEEPALIVE_STATUS_TIMEOUT
} KeepaliveStatus;

// Keepalive statistics
typedef struct {
  uint32_t packets_sent;
  uint32_t packets_received;
  uint32_t consecutive_failures;
  uint64_t last_response_time_ms;
  uint32_t average_rtt_ms;
} KeepaliveStats;

// Keepalive event callbacks
typedef void (*KeepaliveStatusCallback)(KeepaliveStatus status,
                                        const KeepaliveStats* stats,
                                        void* user_data);

// Keepalive configuration
typedef struct {
  char console_ip[16];
  uint16_t console_port;

  // Timing configuration
  uint32_t interval_ms;   // Heartbeat interval (default: 3000ms)
  uint32_t timeout_ms;    // Response timeout (default: 15000ms)
  uint32_t max_failures;  // Max failures before giving up (default: 5)

  // Callbacks
  KeepaliveStatusCallback status_callback;
  void* user_data;

  // Advanced options
  bool enable_adaptive_timing;   // Adjust timing based on RTT
  bool enable_jitter_reduction;  // Add random jitter to prevent sync issues
} KeepaliveConfig;

// Core Keepalive API

/**
 * Initialize keepalive subsystem
 */
VitaRPS5Result session_keepalive_init(void);

/**
 * Cleanup keepalive subsystem
 */
void session_keepalive_cleanup(void);

/**
 * Create keepalive context
 */
VitaRPS5Result session_keepalive_create(const KeepaliveConfig* config,
                                        SessionKeepaliveContext** context);

/**
 * Destroy keepalive context
 */
void session_keepalive_destroy(SessionKeepaliveContext* context);

/**
 * Start keepalive heartbeat (call immediately after successful session init)
 */
VitaRPS5Result session_keepalive_start(SessionKeepaliveContext* context);

/**
 * Stop keepalive heartbeat
 */
VitaRPS5Result session_keepalive_stop(SessionKeepaliveContext* context);

/**
 * Get current keepalive status
 */
KeepaliveStatus session_keepalive_get_status(
    const SessionKeepaliveContext* context);

/**
 * Get keepalive statistics
 */
VitaRPS5Result session_keepalive_get_stats(
    const SessionKeepaliveContext* context, KeepaliveStats* stats);

/**
 * Check if keepalive is currently active
 */
bool session_keepalive_is_active(const SessionKeepaliveContext* context);

// Integration Functions

/**
 * Start keepalive immediately after successful PS5 session init
 * This is the critical missing piece - must be called right after HTTP 200 OK
 */
VitaRPS5Result session_keepalive_start_after_init(
    const char* console_ip, uint16_t console_port,
    SessionKeepaliveContext** context);

/**
 * Integration point for session manager - handles keepalive lifecycle
 */
VitaRPS5Result session_keepalive_integrate_with_session(const char* console_ip,
                                                        uint16_t console_port,
                                                        void* session_context);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_SESSION_KEEPALIVE_H