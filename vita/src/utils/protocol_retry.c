#include "protocol_retry.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/error_codes.h"
#include "logger.h"

// Retry context structure
struct ProtocolRetryContext {
  ProtocolRetryConfig config;

  // State tracking
  uint32_t current_attempt;
  uint64_t start_time_ms;
  uint64_t last_attempt_time_ms;
  VitaRPS5Result last_error;

  // Operation state
  bool operation_active;
  bool timed_out;
  bool max_attempts_reached;
  bool aborted;
};

// Protocol-specific configurations
static const ProtocolRetryConfig DISCOVERY_CONFIG = {
    .max_attempts = 3,
    .base_delay_ms = 1000,
    .max_delay_ms = 5000,
    .timeout_ms = VITARPS5_TIMEOUT_SECONDS * 1000,
    .strategy = RETRY_STRATEGY_EXPONENTIAL,
    .backoff_multiplier = 2.0f,
    .jitter_factor = 0.1f,
    .abort_on_auth_failure = false,
    .abort_on_protocol_error = false,
    .continue_on_network_error = true};

static const ProtocolRetryConfig WAKE_CONFIG = {
    .max_attempts = 5,
    .base_delay_ms = 2000,
    .max_delay_ms = 8000,
    .timeout_ms = 30000,
    .strategy = RETRY_STRATEGY_LINEAR,
    .backoff_multiplier = 1.5f,
    .jitter_factor = 0.2f,
    .abort_on_auth_failure = false,
    .abort_on_protocol_error = false,
    .continue_on_network_error = true};

static const ProtocolRetryConfig SESSION_INIT_CONFIG = {
    .max_attempts = 3,
    .base_delay_ms = 1500,
    .max_delay_ms = 6000,
    .timeout_ms = 20000,
    .strategy = RETRY_STRATEGY_EXPONENTIAL,
    .backoff_multiplier = 2.0f,
    .jitter_factor = 0.15f,
    .abort_on_auth_failure = true,
    .abort_on_protocol_error = true,
    .continue_on_network_error = true};

static const ProtocolRetryConfig STREAMING_CONFIG = {
    .max_attempts = 2,
    .base_delay_ms = 500,
    .max_delay_ms = 2000,
    .timeout_ms = 10000,
    .strategy = RETRY_STRATEGY_FIXED_INTERVAL,
    .backoff_multiplier = 1.0f,
    .jitter_factor = 0.05f,
    .abort_on_auth_failure = true,
    .abort_on_protocol_error = true,
    .continue_on_network_error = false};

static const ProtocolRetryConfig RECONNECT_CONFIG = {
    .max_attempts = 10,
    .base_delay_ms = 1000,
    .max_delay_ms = 10000,
    .timeout_ms = 60000,
    .strategy = RETRY_STRATEGY_EXPONENTIAL,
    .backoff_multiplier = 1.5f,
    .jitter_factor = 0.3f,
    .abort_on_auth_failure = false,
    .abort_on_protocol_error = false,
    .continue_on_network_error = true};

// Internal utility functions
static uint64_t get_timestamp_ms(void);
static uint32_t generate_random_jitter(uint32_t base_delay,
                                       float jitter_factor);

// Protocol-specific configuration getters

ProtocolRetryConfig protocol_retry_get_discovery_config(void) {
  return DISCOVERY_CONFIG;
}

ProtocolRetryConfig protocol_retry_get_wake_config(void) { return WAKE_CONFIG; }

ProtocolRetryConfig protocol_retry_get_session_init_config(void) {
  return SESSION_INIT_CONFIG;
}

ProtocolRetryConfig protocol_retry_get_streaming_config(void) {
  return STREAMING_CONFIG;
}

ProtocolRetryConfig protocol_retry_get_reconnect_config(void) {
  return RECONNECT_CONFIG;
}

// Retry Context Management

VitaRPS5Result protocol_retry_create_context(const ProtocolRetryConfig* config,
                                             ProtocolRetryContext** context) {
  if (!config || !context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (!protocol_retry_validate_config(config)) {
    log_error("Invalid retry configuration provided");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProtocolRetryContext* new_context = malloc(sizeof(ProtocolRetryContext));
  if (!new_context) {
    return VITARPS5_ERROR_MEMORY;
  }

  memset(new_context, 0, sizeof(ProtocolRetryContext));
  new_context->config = *config;
  new_context->start_time_ms = get_timestamp_ms();
  new_context->last_attempt_time_ms = new_context->start_time_ms;

  log_debug(
      "Created retry context: max_attempts=%u, timeout=%u ms, strategy=%s",
      config->max_attempts, config->timeout_ms,
      protocol_retry_strategy_string(config->strategy));

  *context = new_context;
  return VITARPS5_SUCCESS;
}

void protocol_retry_destroy_context(ProtocolRetryContext* context) {
  if (!context) {
    return;
  }

  log_debug("Destroying retry context after %u attempts",
            context->current_attempt);
  free(context);
}

VitaRPS5Result protocol_retry_reset_context(ProtocolRetryContext* context) {
  if (!context) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  context->current_attempt = 0;
  context->start_time_ms = get_timestamp_ms();
  context->last_attempt_time_ms = context->start_time_ms;
  context->last_error = VITARPS5_SUCCESS;
  context->operation_active = false;
  context->timed_out = false;
  context->max_attempts_reached = false;
  context->aborted = false;

  log_debug("Reset retry context");
  return VITARPS5_SUCCESS;
}

// Retry Operation Execution

VitaRPS5Result protocol_retry_execute(
    ProtocolRetryContext* context, ProtocolRetryOperation operation,
    ProtocolRetryProgressCallback progress_callback, void* user_data) {
  if (!context || !operation) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_debug("Starting retry operation: max_attempts=%u, timeout=%u ms",
            context->config.max_attempts, context->config.timeout_ms);

  context->operation_active = true;
  context->current_attempt = 0;
  context->start_time_ms = get_timestamp_ms();

  while (context->current_attempt <= context->config.max_attempts) {
    context->current_attempt++;

    // Check for timeout before attempt
    if (protocol_retry_is_timed_out(context)) {
      context->timed_out = true;
      log_warning("Retry operation timed out after %llu ms (attempt %u/%u)",
                  get_timestamp_ms() - context->start_time_ms,
                  context->current_attempt, context->config.max_attempts);
      context->operation_active = false;
      return VITARPS5_ERROR_TIMEOUT;
    }

    // Call progress callback
    if (progress_callback) {
      uint32_t delay_ms =
          (context->current_attempt > 1)
              ? protocol_retry_calculate_delay(&context->config,
                                               context->current_attempt - 1)
              : 0;
      progress_callback(context->current_attempt, context->config.max_attempts,
                        delay_ms, context->last_error, user_data);
    }

    log_debug("Retry attempt %u/%u", context->current_attempt,
              context->config.max_attempts);

    // Execute operation
    VitaRPS5Result result = operation(user_data, context->current_attempt);
    context->last_error = result;
    context->last_attempt_time_ms = get_timestamp_ms();

    // Success - stop retrying
    if (result == VITARPS5_SUCCESS) {
      uint64_t elapsed = get_timestamp_ms() - context->start_time_ms;
      log_info("Retry operation succeeded on attempt %u/%u (elapsed: %llu ms)",
               context->current_attempt, context->config.max_attempts, elapsed);
      context->operation_active = false;
      return VITARPS5_SUCCESS;
    }

    // Check if we should abort retrying
    if (protocol_retry_should_abort(context, result)) {
      log_warning("Retry operation aborted due to error: %s",
                  vitarps5_result_string(result));
      context->aborted = true;
      context->operation_active = false;
      return result;
    }

    // Check if this was the last attempt
    if (context->current_attempt >= context->config.max_attempts) {
      context->max_attempts_reached = true;
      log_error("Retry operation failed after %u attempts (final error: %s)",
                context->config.max_attempts, vitarps5_result_string(result));
      context->operation_active = false;
      return result;
    }

    // Calculate and apply delay before next retry
    if (context->current_attempt < context->config.max_attempts) {
      uint32_t delay_ms = protocol_retry_calculate_delay(
          &context->config, context->current_attempt);
      log_debug("Waiting %u ms before retry attempt %u", delay_ms,
                context->current_attempt + 1);

      VitaRPS5Result sleep_result = protocol_timeout_sleep(
          delay_ms, context->start_time_ms, context->config.timeout_ms);
      if (sleep_result == VITARPS5_ERROR_TIMEOUT) {
        context->timed_out = true;
        context->operation_active = false;
        return VITARPS5_ERROR_TIMEOUT;
      }
    }
  }

  // Should not reach here, but handle gracefully
  context->operation_active = false;
  return context->last_error;
}

VitaRPS5Result protocol_retry_execute_simple(const ProtocolRetryConfig* config,
                                             ProtocolRetryOperation operation,
                                             void* user_data) {
  if (!config || !operation) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ProtocolRetryContext* context;
  VitaRPS5Result result = protocol_retry_create_context(config, &context);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  result = protocol_retry_execute(context, operation, NULL, user_data);
  protocol_retry_destroy_context(context);

  return result;
}

// Retry State Information

uint32_t protocol_retry_get_current_attempt(
    const ProtocolRetryContext* context) {
  return context ? context->current_attempt : 0;
}

uint64_t protocol_retry_get_elapsed_time(const ProtocolRetryContext* context) {
  if (!context) {
    return 0;
  }
  return get_timestamp_ms() - context->start_time_ms;
}

bool protocol_retry_is_timed_out(const ProtocolRetryContext* context) {
  if (!context) {
    return false;
  }
  return protocol_timeout_check(context->start_time_ms,
                                context->config.timeout_ms);
}

bool protocol_retry_is_max_attempts_reached(
    const ProtocolRetryContext* context) {
  if (!context) {
    return false;
  }
  return context->current_attempt >= context->config.max_attempts;
}

bool protocol_retry_should_abort(const ProtocolRetryContext* context,
                                 VitaRPS5Result error) {
  if (!context) {
    return true;
  }

  // Check configuration-based abort conditions
  if (context->config.abort_on_auth_failure &&
      protocol_retry_is_auth_error(error)) {
    return true;
  }

  if (context->config.abort_on_protocol_error &&
      protocol_retry_is_protocol_error(error)) {
    return true;
  }

  // Network errors - check if we should continue
  if (protocol_retry_is_network_error(error) &&
      !context->config.continue_on_network_error) {
    return true;
  }

  return false;
}

// Timeout Utilities

bool protocol_timeout_check(uint64_t start_time_ms, uint32_t timeout_ms) {
  if (timeout_ms == 0) {
    return false;  // No timeout
  }
  return (get_timestamp_ms() - start_time_ms) >= timeout_ms;
}

uint32_t protocol_timeout_remaining(uint64_t start_time_ms,
                                    uint32_t timeout_ms) {
  if (timeout_ms == 0) {
    return UINT32_MAX;  // No timeout
  }

  uint64_t elapsed = get_timestamp_ms() - start_time_ms;
  if (elapsed >= timeout_ms) {
    return 0;
  }
  return timeout_ms - (uint32_t)elapsed;
}

VitaRPS5Result protocol_timeout_sleep(uint32_t sleep_ms, uint64_t start_time_ms,
                                      uint32_t timeout_ms) {
  // Check if we would timeout during sleep
  uint32_t remaining = protocol_timeout_remaining(start_time_ms, timeout_ms);
  if (remaining <= sleep_ms) {
    log_debug("Sleep would cause timeout - sleeping for %u ms instead of %u ms",
              remaining, sleep_ms);
    if (remaining > 0) {
      sceKernelDelayThread(remaining * 1000);  // Convert to microseconds
    }
    return VITARPS5_ERROR_TIMEOUT;
  }

  sceKernelDelayThread(sleep_ms * 1000);  // Convert to microseconds
  return VITARPS5_SUCCESS;
}

// Delay Calculation

uint32_t protocol_retry_calculate_delay(const ProtocolRetryConfig* config,
                                        uint32_t attempt_number) {
  if (!config || attempt_number == 0) {
    return 0;
  }

  uint32_t delay_ms;

  switch (config->strategy) {
    case RETRY_STRATEGY_FIXED_INTERVAL:
      delay_ms = config->base_delay_ms;
      break;

    case RETRY_STRATEGY_LINEAR:
      delay_ms = config->base_delay_ms * attempt_number;
      break;

    case RETRY_STRATEGY_EXPONENTIAL: {
      delay_ms = config->base_delay_ms;
      for (uint32_t i = 1; i < attempt_number; i++) {
        delay_ms = (uint32_t)(delay_ms * config->backoff_multiplier);
      }
      break;
    }

    case RETRY_STRATEGY_IMMEDIATE:
      delay_ms = 0;
      break;

    default:
      delay_ms = config->base_delay_ms;
      break;
  }

  // Apply maximum delay cap
  if (delay_ms > config->max_delay_ms) {
    delay_ms = config->max_delay_ms;
  }

  // Add jitter if configured
  if (config->jitter_factor > 0.0f) {
    delay_ms = protocol_retry_add_jitter(delay_ms, config->jitter_factor);
  }

  return delay_ms;
}

uint32_t protocol_retry_add_jitter(uint32_t base_delay_ms,
                                   float jitter_factor) {
  if (jitter_factor <= 0.0f) {
    return base_delay_ms;
  }

  uint32_t jitter_amount = generate_random_jitter(base_delay_ms, jitter_factor);
  return base_delay_ms + jitter_amount;
}

// Error Classification

bool protocol_retry_is_error_retryable(VitaRPS5Result error,
                                       const ProtocolRetryConfig* config) {
  if (error == VITARPS5_SUCCESS) {
    return false;  // No need to retry success
  }

  // Check abort conditions
  if (config->abort_on_auth_failure && protocol_retry_is_auth_error(error)) {
    return false;
  }

  if (config->abort_on_protocol_error &&
      protocol_retry_is_protocol_error(error)) {
    return false;
  }

  if (!config->continue_on_network_error &&
      protocol_retry_is_network_error(error)) {
    return false;
  }

  return true;  // Default: retry other errors
}

bool protocol_retry_is_network_error(VitaRPS5Result error) {
  switch (error) {
    case VITARPS5_ERROR_NETWORK:
    case VITARPS5_ERROR_TIMEOUT:
    case VITARPS5_ERROR_OFFLINE:
    case VITARPS5_ERROR_NOT_CONNECTED:
      return true;
    default:
      return false;
  }
}

bool protocol_retry_is_auth_error(VitaRPS5Result error) {
  switch (error) {
    case VITARPS5_ERROR_AUTH_FAILED:
    case VITARPS5_ERROR_NOT_REGISTERED:
    case VITARPS5_ERROR_NOT_AUTHENTICATED:
      return true;
    default:
      return false;
  }
}

bool protocol_retry_is_protocol_error(VitaRPS5Result error) {
  switch (error) {
    case VITARPS5_ERROR_PROTOCOL:
    case VITARPS5_ERROR_INVALID_DATA:
    case VITARPS5_ERROR_BUFFER_TOO_SMALL:
      return true;
    default:
      return false;
  }
}

// Utility Functions

bool protocol_retry_validate_config(const ProtocolRetryConfig* config) {
  if (!config) {
    return false;
  }

  if (config->max_attempts == 0) {
    log_error("Retry validation failed: max_attempts cannot be 0");
    return false;
  }

  if (config->base_delay_ms > config->max_delay_ms) {
    log_error("Retry validation failed: base_delay_ms > max_delay_ms");
    return false;
  }

  if (config->backoff_multiplier <= 0.0f) {
    log_error("Retry validation failed: backoff_multiplier must be > 0");
    return false;
  }

  if (config->jitter_factor < 0.0f || config->jitter_factor > 1.0f) {
    log_error("Retry validation failed: jitter_factor must be 0.0-1.0");
    return false;
  }

  return true;
}

const char* protocol_retry_strategy_string(RetryStrategy strategy) {
  switch (strategy) {
    case RETRY_STRATEGY_FIXED_INTERVAL:
      return "Fixed";
    case RETRY_STRATEGY_EXPONENTIAL:
      return "Exponential";
    case RETRY_STRATEGY_LINEAR:
      return "Linear";
    case RETRY_STRATEGY_IMMEDIATE:
      return "Immediate";
    default:
      return "Unknown";
  }
}

void protocol_retry_log_attempt(uint32_t attempt, uint32_t max_attempts,
                                VitaRPS5Result last_error, uint32_t delay_ms) {
  if (attempt == 1) {
    log_info("Starting operation (max %u attempts)", max_attempts);
  } else {
    log_info("Retry attempt %u/%u (previous error: %s, delay: %u ms)", attempt,
             max_attempts, vitarps5_result_string(last_error), delay_ms);
  }
}

// Internal utility functions

static uint64_t get_timestamp_ms(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick / 1000;
}

static uint32_t generate_random_jitter(uint32_t base_delay,
                                       float jitter_factor) {
  // Simple pseudo-random jitter generation
  static uint32_t seed = 1;
  seed = seed * 1103515245 + 12345;  // Linear congruential generator

  float random_factor =
      ((seed & 0x7FFFFFFF) / (float)0x7FFFFFFF);  // 0.0 to 1.0
  uint32_t max_jitter = (uint32_t)(base_delay * jitter_factor);

  return (uint32_t)(max_jitter * random_factor);
}