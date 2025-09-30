#ifndef VITARPS5_PS5_SESSION_STATE_H
#define VITARPS5_PS5_SESSION_STATE_H

/**
 * PS5 Session State Machine
 *
 * Implements the proper state transition flow for PS5 Remote Play connections
 * as documented in the PS5_REMOTE_PLAY_PROTOCOL_ANALYSIS.md guide.
 *
 * State Flow:
 * IDLE → DISCOVERING → [WAKING] → SESSION_INIT → STREAMING
 *
 * This ensures we follow the correct protocol sequence and handle all
 * error conditions and timeouts properly.
 */

#include <psp2/types.h>
#include <stdbool.h>
#include <stdint.h>

#include "../core/error_codes.h"
#include "../core/vitarps5.h"
#include "console_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// PS5 Session State Enumeration (matches protocol documentation)
typedef enum {
  PS5_SESSION_STATE_IDLE = 0,            // No active session
  PS5_SESSION_STATE_DISCOVERING,         // Finding PS5 console via discovery
  PS5_SESSION_STATE_DISCOVERED_READY,    // Console found in ready state
  PS5_SESSION_STATE_DISCOVERED_STANDBY,  // Console found in standby state
  PS5_SESSION_STATE_WAKING,              // Sending wake signal to console
  PS5_SESSION_STATE_WAKE_FAILED,         // Wake attempt failed
  PS5_SESSION_STATE_SESSION_INIT,        // HTTP session initialization
  PS5_SESSION_STATE_STREAMING,           // Active streaming session
  PS5_SESSION_STATE_RECONNECTING,        // Attempting to reconnect
  PS5_SESSION_STATE_DISCONNECTED,        // Session ended
  PS5_SESSION_STATE_ERROR                // Unrecoverable error state
} PS5SessionState;

// Session State Transition Events
typedef enum {
  PS5_SESSION_EVENT_START_DISCOVERY = 0,    // Begin discovery process
  PS5_SESSION_EVENT_CONSOLE_FOUND_READY,    // Console discovered and ready
  PS5_SESSION_EVENT_CONSOLE_FOUND_STANDBY,  // Console discovered but sleeping
  PS5_SESSION_EVENT_DISCOVERY_TIMEOUT,      // Discovery failed/timeout
  PS5_SESSION_EVENT_WAKE_REQUEST,           // User requests wake
  PS5_SESSION_EVENT_WAKE_SUCCESS,           // Console woke up successfully
  PS5_SESSION_EVENT_WAKE_TIMEOUT,           // Wake attempt timed out
  PS5_SESSION_EVENT_WAKE_FAILED,            // Wake attempt failed
  PS5_SESSION_EVENT_SESSION_INIT_START,     // Begin HTTP session init
  PS5_SESSION_EVENT_SESSION_INIT_SUCCESS,   // HTTP session init succeeded
  PS5_SESSION_EVENT_SESSION_INIT_FAILED,    // HTTP session init failed
  PS5_SESSION_EVENT_STREAMING_START,        // Takion/streaming started
  PS5_SESSION_EVENT_STREAMING_FAILED,       // Streaming connection failed
  PS5_SESSION_EVENT_CONNECTION_LOST,        // Lost connection during streaming
  PS5_SESSION_EVENT_RECONNECT_SUCCESS,      // Reconnection succeeded
  PS5_SESSION_EVENT_RECONNECT_FAILED,       // Reconnection failed
  PS5_SESSION_EVENT_USER_DISCONNECT,        // User requested disconnect
  PS5_SESSION_EVENT_ERROR,                  // Unrecoverable error occurred
  PS5_SESSION_EVENT_RESET                   // Reset to idle state
} PS5SessionEvent;

// Session State Context (opaque structure)
typedef struct PS5SessionStateContext PS5SessionStateContext;

// State transition information
typedef struct {
  PS5SessionState from_state;
  PS5SessionState to_state;
  PS5SessionEvent event;
  uint64_t timestamp_ms;
  const char* reason;  // Human-readable reason for transition
} PS5SessionStateTransition;

// Session state callbacks
typedef void (*PS5SessionStateChangedCallback)(
    const PS5SessionStateTransition* transition, void* user_data);

typedef void (*PS5SessionActionRequiredCallback)(PS5SessionState current_state,
                                                 PS5SessionEvent required_event,
                                                 const char* action_description,
                                                 void* user_data);

// Session state configuration
typedef struct {
  // Timeout settings (milliseconds)
  uint32_t discovery_timeout_ms;     // How long to search for console
  uint32_t wake_timeout_ms;          // How long to wait after wake signal
  uint32_t session_init_timeout_ms;  // HTTP session init timeout
  uint32_t reconnect_timeout_ms;     // How long to try reconnecting

  // Retry settings
  uint32_t max_wake_attempts;       // Max wake retry attempts
  uint32_t max_reconnect_attempts;  // Max reconnection attempts
  uint32_t retry_backoff_ms;        // Delay between retries

  // Console information
  char console_ip[16];         // Target console IP
  PSConsoleType console_type;  // PS4 or PS5

  // Callbacks
  PS5SessionStateChangedCallback state_changed_callback;
  PS5SessionActionRequiredCallback action_required_callback;
  void* user_data;

  // Options
  bool enable_auto_wake;       // Automatically wake sleeping consoles
  bool enable_auto_reconnect;  // Automatically reconnect on connection loss
  bool enable_debug_logging;   // Verbose state transition logging
} PS5SessionStateConfig;

// Core Session State API

/**
 * Initialize PS5 session state subsystem
 */
VitaRPS5Result ps5_session_state_init(void);

/**
 * Cleanup PS5 session state subsystem
 */
void ps5_session_state_cleanup(void);

/**
 * Create session state context
 */
VitaRPS5Result ps5_session_state_create_context(
    const PS5SessionStateConfig* config, PS5SessionStateContext** context);

/**
 * Destroy session state context
 */
void ps5_session_state_destroy_context(PS5SessionStateContext* context);

/**
 * Get current session state
 */
PS5SessionState ps5_session_state_get_current(PS5SessionStateContext* context);

/**
 * Send event to state machine (triggers state transitions)
 */
VitaRPS5Result ps5_session_state_send_event(PS5SessionStateContext* context,
                                            PS5SessionEvent event,
                                            const char* event_data);

/**
 * Update state machine (check timeouts, trigger automatic transitions)
 * Should be called regularly from main loop
 */
VitaRPS5Result ps5_session_state_update(PS5SessionStateContext* context);

/**
 * Reset state machine to idle state
 */
VitaRPS5Result ps5_session_state_reset(PS5SessionStateContext* context);

// State Information and Validation

/**
 * Check if state transition is valid
 */
bool ps5_session_state_is_transition_valid(PS5SessionState from_state,
                                           PS5SessionEvent event,
                                           PS5SessionState to_state);

/**
 * Get expected next events for current state
 */
VitaRPS5Result ps5_session_state_get_expected_events(
    PS5SessionState current_state, PS5SessionEvent* events, size_t max_events,
    size_t* event_count);

/**
 * Check if state machine is in a terminal state (error or disconnected)
 */
bool ps5_session_state_is_terminal(PS5SessionState state);

/**
 * Check if state machine is in an active streaming state
 */
bool ps5_session_state_is_streaming(PS5SessionState state);

/**
 * Get time spent in current state (milliseconds)
 */
uint64_t ps5_session_state_get_time_in_current_state(
    PS5SessionStateContext* context);

// Action and Flow Control

/**
 * Check if automatic action should be triggered in current state
 * Returns true if an automatic transition should occur
 */
bool ps5_session_state_should_auto_advance(PS5SessionStateContext* context);

/**
 * Get recommended action for current state
 * Returns human-readable string describing what should happen next
 */
const char* ps5_session_state_get_recommended_action(PS5SessionState state);

/**
 * Check if user intervention is required
 */
bool ps5_session_state_requires_user_action(PS5SessionState state);

// Timeout and Retry Management

/**
 * Check if current state has timed out
 */
bool ps5_session_state_has_timed_out(PS5SessionStateContext* context);

/**
 * Get remaining time before timeout (milliseconds, 0 if timed out)
 */
uint32_t ps5_session_state_get_timeout_remaining(
    PS5SessionStateContext* context);

/**
 * Check if retry should be attempted for current state
 */
bool ps5_session_state_should_retry(PS5SessionStateContext* context);

/**
 * Get retry attempt count for current state
 */
uint32_t ps5_session_state_get_retry_count(PS5SessionStateContext* context);

// Utility Functions

/**
 * Convert session state to string
 */
const char* ps5_session_state_to_string(PS5SessionState state);

/**
 * Convert session event to string
 */
const char* ps5_session_event_to_string(PS5SessionEvent event);

/**
 * Get state description (user-friendly explanation)
 */
const char* ps5_session_state_get_description(PS5SessionState state);

/**
 * Log state transition for debugging
 */
void ps5_session_state_log_transition(
    const PS5SessionStateTransition* transition);

/**
 * Validate session state configuration
 */
bool ps5_session_state_validate_config(const PS5SessionStateConfig* config);

// Integration with Other Subsystems

/**
 * Set console discovery result (called by discovery subsystem)
 */
VitaRPS5Result ps5_session_state_set_discovery_result(
    PS5SessionStateContext* context, bool console_found,
    ConsoleState console_state);

/**
 * Set wake result (called by wake subsystem)
 */
VitaRPS5Result ps5_session_state_set_wake_result(
    PS5SessionStateContext* context, bool wake_success);

/**
 * Set session init result (called by session init subsystem)
 */
VitaRPS5Result ps5_session_state_set_session_init_result(
    PS5SessionStateContext* context, bool init_success);

/**
 * Set streaming status (called by streaming subsystem)
 */
VitaRPS5Result ps5_session_state_set_streaming_status(
    PS5SessionStateContext* context, bool streaming_active);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_PS5_SESSION_STATE_H