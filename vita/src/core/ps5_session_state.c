#include "ps5_session_state.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logger.h"

// Session state context structure
struct PS5SessionStateContext {
  PS5SessionStateConfig config;

  // Current state information
  PS5SessionState current_state;
  PS5SessionState previous_state;
  uint64_t state_enter_time_ms;
  uint64_t last_transition_time_ms;

  // Retry and timeout tracking
  uint32_t retry_count;
  uint32_t max_retries_for_state;
  uint64_t next_retry_time_ms;

  // State history for debugging
  PS5SessionStateTransition last_transition;

  // State-specific data
  bool console_found;
  ConsoleState console_state;
  bool wake_requested;
  bool session_init_attempted;
};

// Global state
static bool session_state_initialized = false;

// State transition table - defines valid transitions
typedef struct {
  PS5SessionState from_state;
  PS5SessionEvent event;
  PS5SessionState to_state;
  const char* description;
} StateTransitionRule;

// Valid state transitions (based on protocol documentation)
static const StateTransitionRule VALID_TRANSITIONS[] = {
    // From IDLE
    {PS5_SESSION_STATE_IDLE, PS5_SESSION_EVENT_START_DISCOVERY,
     PS5_SESSION_STATE_DISCOVERING, "Start console discovery"},

    // From DISCOVERING
    {PS5_SESSION_STATE_DISCOVERING, PS5_SESSION_EVENT_CONSOLE_FOUND_READY,
     PS5_SESSION_STATE_DISCOVERED_READY, "Console found and ready"},
    {PS5_SESSION_STATE_DISCOVERING, PS5_SESSION_EVENT_CONSOLE_FOUND_STANDBY,
     PS5_SESSION_STATE_DISCOVERED_STANDBY, "Console found but sleeping"},
    {PS5_SESSION_STATE_DISCOVERING, PS5_SESSION_EVENT_DISCOVERY_TIMEOUT,
     PS5_SESSION_STATE_ERROR, "Discovery timed out"},

    // From DISCOVERED_READY
    {PS5_SESSION_STATE_DISCOVERED_READY, PS5_SESSION_EVENT_SESSION_INIT_START,
     PS5_SESSION_STATE_SESSION_INIT, "Begin session initialization"},

    // From DISCOVERED_STANDBY
    {PS5_SESSION_STATE_DISCOVERED_STANDBY, PS5_SESSION_EVENT_WAKE_REQUEST,
     PS5_SESSION_STATE_WAKING, "Send wake signal"},
    {PS5_SESSION_STATE_DISCOVERED_STANDBY, PS5_SESSION_EVENT_SESSION_INIT_START,
     PS5_SESSION_STATE_SESSION_INIT, "Try session init without wake"},

    // From WAKING
    {PS5_SESSION_STATE_WAKING, PS5_SESSION_EVENT_WAKE_SUCCESS,
     PS5_SESSION_STATE_DISCOVERED_READY, "Console woke up successfully"},
    {PS5_SESSION_STATE_WAKING, PS5_SESSION_EVENT_WAKE_TIMEOUT,
     PS5_SESSION_STATE_WAKE_FAILED, "Wake attempt timed out"},
    {PS5_SESSION_STATE_WAKING, PS5_SESSION_EVENT_WAKE_FAILED,
     PS5_SESSION_STATE_WAKE_FAILED, "Wake attempt failed"},

    // From WAKE_FAILED
    {PS5_SESSION_STATE_WAKE_FAILED, PS5_SESSION_EVENT_WAKE_REQUEST,
     PS5_SESSION_STATE_WAKING, "Retry wake attempt"},
    {PS5_SESSION_STATE_WAKE_FAILED, PS5_SESSION_EVENT_ERROR,
     PS5_SESSION_STATE_ERROR, "Give up on wake"},
    {PS5_SESSION_STATE_WAKE_FAILED, PS5_SESSION_EVENT_RESET,
     PS5_SESSION_STATE_IDLE, "Reset to idle"},

    // From SESSION_INIT
    {PS5_SESSION_STATE_SESSION_INIT, PS5_SESSION_EVENT_SESSION_INIT_SUCCESS,
     PS5_SESSION_STATE_STREAMING, "Session init succeeded, begin streaming"},
    {PS5_SESSION_STATE_SESSION_INIT, PS5_SESSION_EVENT_SESSION_INIT_FAILED,
     PS5_SESSION_STATE_ERROR, "Session init failed"},

    // From STREAMING
    {PS5_SESSION_STATE_STREAMING, PS5_SESSION_EVENT_CONNECTION_LOST,
     PS5_SESSION_STATE_RECONNECTING, "Connection lost, try to reconnect"},
    {PS5_SESSION_STATE_STREAMING, PS5_SESSION_EVENT_USER_DISCONNECT,
     PS5_SESSION_STATE_DISCONNECTED, "User requested disconnect"},
    {PS5_SESSION_STATE_STREAMING, PS5_SESSION_EVENT_STREAMING_FAILED,
     PS5_SESSION_STATE_ERROR, "Streaming failed"},

    // From RECONNECTING
    {PS5_SESSION_STATE_RECONNECTING, PS5_SESSION_EVENT_RECONNECT_SUCCESS,
     PS5_SESSION_STATE_STREAMING, "Reconnection successful"},
    {PS5_SESSION_STATE_RECONNECTING, PS5_SESSION_EVENT_RECONNECT_FAILED,
     PS5_SESSION_STATE_DISCONNECTED, "Reconnection failed"},

    // Global transitions (can happen from any state)
    {PS5_SESSION_STATE_IDLE, PS5_SESSION_EVENT_RESET, PS5_SESSION_STATE_IDLE,
     "Reset to idle"},
    {PS5_SESSION_STATE_DISCOVERING, PS5_SESSION_EVENT_RESET,
     PS5_SESSION_STATE_IDLE, "Reset to idle"},
    {PS5_SESSION_STATE_DISCOVERED_READY, PS5_SESSION_EVENT_RESET,
     PS5_SESSION_STATE_IDLE, "Reset to idle"},
    {PS5_SESSION_STATE_DISCOVERED_STANDBY, PS5_SESSION_EVENT_RESET,
     PS5_SESSION_STATE_IDLE, "Reset to idle"},
    {PS5_SESSION_STATE_WAKING, PS5_SESSION_EVENT_RESET, PS5_SESSION_STATE_IDLE,
     "Reset to idle"},
    {PS5_SESSION_STATE_SESSION_INIT, PS5_SESSION_EVENT_RESET,
     PS5_SESSION_STATE_IDLE, "Reset to idle"},
    {PS5_SESSION_STATE_STREAMING, PS5_SESSION_EVENT_RESET,
     PS5_SESSION_STATE_IDLE, "Reset to idle"},
    {PS5_SESSION_STATE_RECONNECTING, PS5_SESSION_EVENT_RESET,
     PS5_SESSION_STATE_IDLE, "Reset to idle"},
    {PS5_SESSION_STATE_ERROR, PS5_SESSION_EVENT_RESET, PS5_SESSION_STATE_IDLE,
     "Reset to idle"},
    {PS5_SESSION_STATE_DISCONNECTED, PS5_SESSION_EVENT_RESET,
     PS5_SESSION_STATE_IDLE, "Reset to idle"}};

#define NUM_VALID_TRANSITIONS \
  (sizeof(VALID_TRANSITIONS) / sizeof(VALID_TRANSITIONS[0]))

// Internal function declarations
static VitaRPS5Result ps5_session_state_transition(
    PS5SessionStateContext* context, PS5SessionEvent event, const char* reason);
static bool ps5_session_state_find_transition(PS5SessionState from_state,
                                              PS5SessionEvent event,
                                              PS5SessionState* to_state,
                                              const char** description);
static void ps5_session_state_enter_state(PS5SessionStateContext* context,
                                          PS5SessionState new_state);
static void ps5_session_state_exit_state(PS5SessionStateContext* context,
                                         PS5SessionState old_state);
static uint32_t ps5_session_state_get_timeout_for_state(
    PS5SessionState state, const PS5SessionStateConfig* config);
static uint32_t ps5_session_state_get_max_retries_for_state(
    PS5SessionState state, const PS5SessionStateConfig* config);
static uint64_t get_timestamp_ms(void);

// API Implementation

VitaRPS5Result ps5_session_state_init(void) {
  if (session_state_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing PS5 session state subsystem");
  session_state_initialized = true;
  log_info("PS5 session state subsystem initialized successfully");

  return VITARPS5_SUCCESS;
}

void ps5_session_state_cleanup(void) {
  if (!session_state_initialized) {
    return;
  }

  log_info("Cleaning up PS5 session state subsystem");
  session_state_initialized = false;
  log_info("PS5 session state cleanup complete");
}

VitaRPS5Result ps5_session_state_create_context(
    const PS5SessionStateConfig* config, PS5SessionStateContext** context) {
  if (!session_state_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!config || !context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (!ps5_session_state_validate_config(config)) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  PS5SessionStateContext* new_context = malloc(sizeof(PS5SessionStateContext));
  if (!new_context) {
    return VITARPS5_ERROR_MEMORY;
  }

  // Initialize context
  memset(new_context, 0, sizeof(PS5SessionStateContext));
  new_context->config = *config;
  new_context->current_state = PS5_SESSION_STATE_IDLE;
  new_context->previous_state = PS5_SESSION_STATE_IDLE;
  new_context->state_enter_time_ms = get_timestamp_ms();
  new_context->last_transition_time_ms = new_context->state_enter_time_ms;

  log_info("Created PS5 session state context for console %s",
           config->console_ip);

  *context = new_context;
  return VITARPS5_SUCCESS;
}

void ps5_session_state_destroy_context(PS5SessionStateContext* context) {
  if (!context) {
    return;
  }

  log_info("Destroying PS5 session state context");

  // Notify of final state if streaming was active
  if (ps5_session_state_is_streaming(context->current_state)) {
    ps5_session_state_send_event(context, PS5_SESSION_EVENT_USER_DISCONNECT,
                                 "Context destroyed");
  }

  free(context);
  log_debug("PS5 session state context destroyed");
}

PS5SessionState ps5_session_state_get_current(PS5SessionStateContext* context) {
  if (!context) {
    return PS5_SESSION_STATE_ERROR;
  }
  return context->current_state;
}

VitaRPS5Result ps5_session_state_send_event(PS5SessionStateContext* context,
                                            PS5SessionEvent event,
                                            const char* event_data) {
  if (!context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_debug("Session state: Received event %s in state %s",
            ps5_session_event_to_string(event),
            ps5_session_state_to_string(context->current_state));

  // Try to transition based on the event
  VitaRPS5Result result =
      ps5_session_state_transition(context, event, event_data);
  if (result != VITARPS5_SUCCESS) {
    log_warning("Invalid state transition: %s -> %s (event: %s)",
                ps5_session_state_to_string(context->current_state), "UNKNOWN",
                ps5_session_event_to_string(event));
    return result;
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result ps5_session_state_update(PS5SessionStateContext* context) {
  if (!context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  uint64_t current_time = get_timestamp_ms();

  // Check for timeouts
  if (ps5_session_state_has_timed_out(context)) {
    log_warning("State %s timed out after %llu ms",
                ps5_session_state_to_string(context->current_state),
                current_time - context->state_enter_time_ms);

    // Handle timeout based on current state
    switch (context->current_state) {
      case PS5_SESSION_STATE_DISCOVERING:
        ps5_session_state_send_event(context,
                                     PS5_SESSION_EVENT_DISCOVERY_TIMEOUT,
                                     "Discovery timed out");
        break;
      case PS5_SESSION_STATE_WAKING:
        ps5_session_state_send_event(context, PS5_SESSION_EVENT_WAKE_TIMEOUT,
                                     "Wake timed out");
        break;
      case PS5_SESSION_STATE_SESSION_INIT:
        ps5_session_state_send_event(context,
                                     PS5_SESSION_EVENT_SESSION_INIT_FAILED,
                                     "Session init timed out");
        break;
      case PS5_SESSION_STATE_RECONNECTING:
        ps5_session_state_send_event(
            context, PS5_SESSION_EVENT_RECONNECT_FAILED, "Reconnect timed out");
        break;
      default:
        // Other states don't have automatic timeout handling
        break;
    }
  }

  // Check for automatic state advances
  if (ps5_session_state_should_auto_advance(context)) {
    switch (context->current_state) {
      case PS5_SESSION_STATE_DISCOVERED_READY:
        if (context->config.state_changed_callback) {
          // Notify that session init should begin
          if (context->config.action_required_callback) {
            context->config.action_required_callback(
                context->current_state, PS5_SESSION_EVENT_SESSION_INIT_START,
                "Ready to begin session initialization",
                context->config.user_data);
          }
        }
        break;
      case PS5_SESSION_STATE_DISCOVERED_STANDBY:
        if (context->config.enable_auto_wake) {
          ps5_session_state_send_event(context, PS5_SESSION_EVENT_WAKE_REQUEST,
                                       "Auto-wake enabled");
        } else if (context->config.action_required_callback) {
          context->config.action_required_callback(
              context->current_state, PS5_SESSION_EVENT_WAKE_REQUEST,
              "Console is sleeping, wake required", context->config.user_data);
        }
        break;
      default:
        break;
    }
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result ps5_session_state_reset(PS5SessionStateContext* context) {
  if (!context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Resetting session state to idle");
  return ps5_session_state_send_event(context, PS5_SESSION_EVENT_RESET,
                                      "Manual reset");
}

// State Validation and Information

bool ps5_session_state_is_transition_valid(PS5SessionState from_state,
                                           PS5SessionEvent event,
                                           PS5SessionState to_state) {
  PS5SessionState expected_to_state;
  const char* description;

  if (ps5_session_state_find_transition(from_state, event, &expected_to_state,
                                        &description)) {
    return (expected_to_state == to_state);
  }

  return false;
}

bool ps5_session_state_is_terminal(PS5SessionState state) {
  return (state == PS5_SESSION_STATE_ERROR ||
          state == PS5_SESSION_STATE_DISCONNECTED);
}

bool ps5_session_state_is_streaming(PS5SessionState state) {
  return (state == PS5_SESSION_STATE_STREAMING ||
          state == PS5_SESSION_STATE_RECONNECTING);
}

uint64_t ps5_session_state_get_time_in_current_state(
    PS5SessionStateContext* context) {
  if (!context) {
    return 0;
  }
  return get_timestamp_ms() - context->state_enter_time_ms;
}

// Timeout and Retry Management

bool ps5_session_state_has_timed_out(PS5SessionStateContext* context) {
  if (!context) {
    return false;
  }

  uint32_t timeout = ps5_session_state_get_timeout_for_state(
      context->current_state, &context->config);
  if (timeout == 0) {
    return false;  // No timeout for this state
  }

  uint64_t time_in_state = ps5_session_state_get_time_in_current_state(context);
  return time_in_state >= timeout;
}

uint32_t ps5_session_state_get_timeout_remaining(
    PS5SessionStateContext* context) {
  if (!context) {
    return 0;
  }

  uint32_t timeout = ps5_session_state_get_timeout_for_state(
      context->current_state, &context->config);
  if (timeout == 0) {
    return UINT32_MAX;  // No timeout
  }

  uint64_t time_in_state = ps5_session_state_get_time_in_current_state(context);
  if (time_in_state >= timeout) {
    return 0;  // Already timed out
  }

  return timeout - (uint32_t)time_in_state;
}

bool ps5_session_state_should_retry(PS5SessionStateContext* context) {
  if (!context) {
    return false;
  }

  // Check if we've exceeded max retries
  uint32_t max_retries = ps5_session_state_get_max_retries_for_state(
      context->current_state, &context->config);
  if (context->retry_count >= max_retries) {
    return false;
  }

  // Check if enough time has passed for retry
  uint64_t current_time = get_timestamp_ms();
  return current_time >= context->next_retry_time_ms;
}

uint32_t ps5_session_state_get_retry_count(PS5SessionStateContext* context) {
  if (!context) {
    return 0;
  }
  return context->retry_count;
}

// Utility Functions

const char* ps5_session_state_to_string(PS5SessionState state) {
  switch (state) {
    case PS5_SESSION_STATE_IDLE:
      return "IDLE";
    case PS5_SESSION_STATE_DISCOVERING:
      return "DISCOVERING";
    case PS5_SESSION_STATE_DISCOVERED_READY:
      return "DISCOVERED_READY";
    case PS5_SESSION_STATE_DISCOVERED_STANDBY:
      return "DISCOVERED_STANDBY";
    case PS5_SESSION_STATE_WAKING:
      return "WAKING";
    case PS5_SESSION_STATE_WAKE_FAILED:
      return "WAKE_FAILED";
    case PS5_SESSION_STATE_SESSION_INIT:
      return "SESSION_INIT";
    case PS5_SESSION_STATE_STREAMING:
      return "STREAMING";
    case PS5_SESSION_STATE_RECONNECTING:
      return "RECONNECTING";
    case PS5_SESSION_STATE_DISCONNECTED:
      return "DISCONNECTED";
    case PS5_SESSION_STATE_ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

const char* ps5_session_event_to_string(PS5SessionEvent event) {
  switch (event) {
    case PS5_SESSION_EVENT_START_DISCOVERY:
      return "START_DISCOVERY";
    case PS5_SESSION_EVENT_CONSOLE_FOUND_READY:
      return "CONSOLE_FOUND_READY";
    case PS5_SESSION_EVENT_CONSOLE_FOUND_STANDBY:
      return "CONSOLE_FOUND_STANDBY";
    case PS5_SESSION_EVENT_DISCOVERY_TIMEOUT:
      return "DISCOVERY_TIMEOUT";
    case PS5_SESSION_EVENT_WAKE_REQUEST:
      return "WAKE_REQUEST";
    case PS5_SESSION_EVENT_WAKE_SUCCESS:
      return "WAKE_SUCCESS";
    case PS5_SESSION_EVENT_WAKE_TIMEOUT:
      return "WAKE_TIMEOUT";
    case PS5_SESSION_EVENT_WAKE_FAILED:
      return "WAKE_FAILED";
    case PS5_SESSION_EVENT_SESSION_INIT_START:
      return "SESSION_INIT_START";
    case PS5_SESSION_EVENT_SESSION_INIT_SUCCESS:
      return "SESSION_INIT_SUCCESS";
    case PS5_SESSION_EVENT_SESSION_INIT_FAILED:
      return "SESSION_INIT_FAILED";
    case PS5_SESSION_EVENT_STREAMING_START:
      return "STREAMING_START";
    case PS5_SESSION_EVENT_STREAMING_FAILED:
      return "STREAMING_FAILED";
    case PS5_SESSION_EVENT_CONNECTION_LOST:
      return "CONNECTION_LOST";
    case PS5_SESSION_EVENT_RECONNECT_SUCCESS:
      return "RECONNECT_SUCCESS";
    case PS5_SESSION_EVENT_RECONNECT_FAILED:
      return "RECONNECT_FAILED";
    case PS5_SESSION_EVENT_USER_DISCONNECT:
      return "USER_DISCONNECT";
    case PS5_SESSION_EVENT_ERROR:
      return "ERROR";
    case PS5_SESSION_EVENT_RESET:
      return "RESET";
    default:
      return "UNKNOWN";
  }
}

const char* ps5_session_state_get_description(PS5SessionState state) {
  switch (state) {
    case PS5_SESSION_STATE_IDLE:
      return "No active session";
    case PS5_SESSION_STATE_DISCOVERING:
      return "Searching for PS5 console";
    case PS5_SESSION_STATE_DISCOVERED_READY:
      return "Console found and ready";
    case PS5_SESSION_STATE_DISCOVERED_STANDBY:
      return "Console found but sleeping";
    case PS5_SESSION_STATE_WAKING:
      return "Waking up console";
    case PS5_SESSION_STATE_WAKE_FAILED:
      return "Failed to wake console";
    case PS5_SESSION_STATE_SESSION_INIT:
      return "Initializing session";
    case PS5_SESSION_STATE_STREAMING:
      return "Streaming active";
    case PS5_SESSION_STATE_RECONNECTING:
      return "Reconnecting to console";
    case PS5_SESSION_STATE_DISCONNECTED:
      return "Session ended";
    case PS5_SESSION_STATE_ERROR:
      return "Error occurred";
    default:
      return "Unknown state";
  }
}

bool ps5_session_state_validate_config(const PS5SessionStateConfig* config) {
  if (!config) {
    return false;
  }

  if (strlen(config->console_ip) == 0) {
    log_error("Session state config validation failed: empty console IP");
    return false;
  }

  if (config->discovery_timeout_ms == 0 ||
      config->discovery_timeout_ms > 60000) {
    log_error(
        "Session state config validation failed: invalid discovery timeout");
    return false;
  }

  return true;
}

// Integration Functions

VitaRPS5Result ps5_session_state_set_discovery_result(
    PS5SessionStateContext* context, bool console_found,
    ConsoleState console_state) {
  if (!context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  context->console_found = console_found;
  context->console_state = console_state;

  if (console_found) {
    if (console_state == CONSOLE_STATE_READY) {
      return ps5_session_state_send_event(
          context, PS5_SESSION_EVENT_CONSOLE_FOUND_READY, "Console ready");
    } else if (console_state == CONSOLE_STATE_STANDBY) {
      return ps5_session_state_send_event(
          context, PS5_SESSION_EVENT_CONSOLE_FOUND_STANDBY, "Console sleeping");
    }
  } else {
    return ps5_session_state_send_event(
        context, PS5_SESSION_EVENT_DISCOVERY_TIMEOUT, "Console not found");
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result ps5_session_state_set_wake_result(
    PS5SessionStateContext* context, bool wake_success) {
  if (!context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (wake_success) {
    return ps5_session_state_send_event(context, PS5_SESSION_EVENT_WAKE_SUCCESS,
                                        "Wake successful");
  } else {
    return ps5_session_state_send_event(context, PS5_SESSION_EVENT_WAKE_FAILED,
                                        "Wake failed");
  }
}

// Internal Implementation

static VitaRPS5Result ps5_session_state_transition(
    PS5SessionStateContext* context, PS5SessionEvent event,
    const char* reason) {
  PS5SessionState to_state;
  const char* description;

  if (!ps5_session_state_find_transition(context->current_state, event,
                                         &to_state, &description)) {
    log_debug("No valid transition found for state %s with event %s",
              ps5_session_state_to_string(context->current_state),
              ps5_session_event_to_string(event));
    return VITARPS5_ERROR_INVALID_STATE;
  }

  PS5SessionState old_state = context->current_state;

  log_info("State transition: %s -> %s (event: %s, reason: %s)",
           ps5_session_state_to_string(old_state),
           ps5_session_state_to_string(to_state),
           ps5_session_event_to_string(event), reason ? reason : "none");

  // Exit current state
  ps5_session_state_exit_state(context, old_state);

  // Enter new state
  context->previous_state = old_state;
  context->current_state = to_state;
  ps5_session_state_enter_state(context, to_state);

  // Record transition
  context->last_transition.from_state = old_state;
  context->last_transition.to_state = to_state;
  context->last_transition.event = event;
  context->last_transition.timestamp_ms = get_timestamp_ms();
  context->last_transition.reason = reason;

  // Call state changed callback
  if (context->config.state_changed_callback) {
    context->config.state_changed_callback(&context->last_transition,
                                           context->config.user_data);
  }

  return VITARPS5_SUCCESS;
}

static bool ps5_session_state_find_transition(PS5SessionState from_state,
                                              PS5SessionEvent event,
                                              PS5SessionState* to_state,
                                              const char** description) {
  for (size_t i = 0; i < NUM_VALID_TRANSITIONS; i++) {
    const StateTransitionRule* rule = &VALID_TRANSITIONS[i];
    if (rule->from_state == from_state && rule->event == event) {
      if (to_state) *to_state = rule->to_state;
      if (description) *description = rule->description;
      return true;
    }
  }

  return false;
}

static void ps5_session_state_enter_state(PS5SessionStateContext* context,
                                          PS5SessionState new_state) {
  context->state_enter_time_ms = get_timestamp_ms();
  context->last_transition_time_ms = context->state_enter_time_ms;
  context->retry_count = 0;
  context->max_retries_for_state =
      ps5_session_state_get_max_retries_for_state(new_state, &context->config);

  log_debug("Entered state %s", ps5_session_state_to_string(new_state));
}

static void ps5_session_state_exit_state(PS5SessionStateContext* context,
                                         PS5SessionState old_state) {
  uint64_t time_in_state = get_timestamp_ms() - context->state_enter_time_ms;
  log_debug("Exited state %s after %llu ms",
            ps5_session_state_to_string(old_state), time_in_state);
}

static uint32_t ps5_session_state_get_timeout_for_state(
    PS5SessionState state, const PS5SessionStateConfig* config) {
  switch (state) {
    case PS5_SESSION_STATE_DISCOVERING:
      return config->discovery_timeout_ms;
    case PS5_SESSION_STATE_WAKING:
      return config->wake_timeout_ms;
    case PS5_SESSION_STATE_SESSION_INIT:
      return config->session_init_timeout_ms;
    case PS5_SESSION_STATE_RECONNECTING:
      return config->reconnect_timeout_ms;
    default:
      return 0;  // No timeout
  }
}

static uint32_t ps5_session_state_get_max_retries_for_state(
    PS5SessionState state, const PS5SessionStateConfig* config) {
  switch (state) {
    case PS5_SESSION_STATE_WAKING:
      return config->max_wake_attempts;
    case PS5_SESSION_STATE_RECONNECTING:
      return config->max_reconnect_attempts;
    default:
      return 0;  // No retries
  }
}

static uint64_t get_timestamp_ms(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick / 1000;
}

bool ps5_session_state_should_auto_advance(PS5SessionStateContext* context) {
  if (!context) {
    return false;
  }

  // Only auto-advance if configuration allows it
  switch (context->current_state) {
    case PS5_SESSION_STATE_DISCOVERED_READY:
      // Auto-advance to session init if auto-connect is enabled
      return context->config.enable_auto_reconnect;

    case PS5_SESSION_STATE_WAKING:
      // Check if wake timeout has been reached
      if (context->state_enter_time_ms > 0) {
        uint64_t elapsed = get_timestamp_ms() - context->state_enter_time_ms;
        return elapsed > context->config.wake_timeout_ms;
      }
      return false;

    case PS5_SESSION_STATE_SESSION_INIT:
      // Check if session init timeout has been reached
      if (context->state_enter_time_ms > 0) {
        uint64_t elapsed = get_timestamp_ms() - context->state_enter_time_ms;
        return elapsed > context->config.session_init_timeout_ms;
      }
      return false;

    default:
      // Other states don't auto-advance
      return false;
  }
}