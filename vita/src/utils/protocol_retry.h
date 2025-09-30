#ifndef VITARPS5_PROTOCOL_RETRY_H
#define VITARPS5_PROTOCOL_RETRY_H

/**
 * Protocol Retry and Timeout Utilities
 *
 * Centralized retry logic for PS5 Remote Play protocol operations.
 * Provides consistent timeout handling, exponential backoff, and
 * error recovery patterns across all protocol components.
 */

#include <psp2/types.h>
#include <stdbool.h>
#include <stdint.h>

#include "../core/vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

// Retry strategies
typedef enum {
  RETRY_STRATEGY_FIXED_INTERVAL,  // Fixed delay between retries
  RETRY_STRATEGY_EXPONENTIAL,     // Exponential backoff
  RETRY_STRATEGY_LINEAR,          // Linear increase in delay
  RETRY_STRATEGY_IMMEDIATE        // No delay between retries
} RetryStrategy;

// Retry configuration
typedef struct {
  uint32_t max_attempts;   // Maximum retry attempts (0 = no retries)
  uint32_t base_delay_ms;  // Base delay between retries
  uint32_t max_delay_ms;   // Maximum delay cap
  uint32_t timeout_ms;     // Total operation timeout
  RetryStrategy strategy;  // Retry strategy to use

  // Backoff parameters
  float
      backoff_multiplier;  // Multiplier for exponential backoff (default: 2.0)
  float jitter_factor;     // Random jitter factor (0.0-1.0, default: 0.1)

  // Protocol-specific options
  bool abort_on_auth_failure;      // Stop retrying on authentication errors
  bool abort_on_protocol_error;    // Stop retrying on protocol format errors
  bool continue_on_network_error;  // Continue retrying on network errors
} ProtocolRetryConfig;

// Retry context (opaque structure for tracking retry state)
typedef struct ProtocolRetryContext ProtocolRetryContext;

// Retry operation callback - return VITARPS5_SUCCESS to stop, error to retry
typedef VitaRPS5Result (*ProtocolRetryOperation)(void* user_data,
                                                 uint32_t attempt_number);

// Retry progress callback - called before each retry attempt
typedef void (*ProtocolRetryProgressCallback)(uint32_t attempt_number,
                                              uint32_t max_attempts,
                                              uint32_t delay_ms,
                                              VitaRPS5Result last_error,
                                              void* user_data);

// Protocol-specific retry configurations

/**
 * Get retry configuration for discovery operations
 */
ProtocolRetryConfig protocol_retry_get_discovery_config(void);

/**
 * Get retry configuration for wake operations
 */
ProtocolRetryConfig protocol_retry_get_wake_config(void);

/**
 * Get retry configuration for session initialization
 */
ProtocolRetryConfig protocol_retry_get_session_init_config(void);

/**
 * Get retry configuration for streaming connections
 */
ProtocolRetryConfig protocol_retry_get_streaming_config(void);

/**
 * Get retry configuration for reconnection attempts
 */
ProtocolRetryConfig protocol_retry_get_reconnect_config(void);

// Retry Context Management

/**
 * Create retry context with configuration
 */
VitaRPS5Result protocol_retry_create_context(const ProtocolRetryConfig* config,
                                             ProtocolRetryContext** context);

/**
 * Destroy retry context
 */
void protocol_retry_destroy_context(ProtocolRetryContext* context);

/**
 * Reset retry context for new operation
 */
VitaRPS5Result protocol_retry_reset_context(ProtocolRetryContext* context);

// Retry Operation Execution

/**
 * Execute operation with retry logic
 */
VitaRPS5Result protocol_retry_execute(
    ProtocolRetryContext* context, ProtocolRetryOperation operation,
    ProtocolRetryProgressCallback progress_callback, void* user_data);

/**
 * Execute operation with simple retry (no context needed)
 */
VitaRPS5Result protocol_retry_execute_simple(const ProtocolRetryConfig* config,
                                             ProtocolRetryOperation operation,
                                             void* user_data);

// Retry State Information

/**
 * Get current attempt number
 */
uint32_t protocol_retry_get_current_attempt(
    const ProtocolRetryContext* context);

/**
 * Get time elapsed since first attempt
 */
uint64_t protocol_retry_get_elapsed_time(const ProtocolRetryContext* context);

/**
 * Check if operation has timed out
 */
bool protocol_retry_is_timed_out(const ProtocolRetryContext* context);

/**
 * Check if max attempts reached
 */
bool protocol_retry_is_max_attempts_reached(
    const ProtocolRetryContext* context);

/**
 * Check if error should abort retry sequence
 */
bool protocol_retry_should_abort(const ProtocolRetryContext* context,
                                 VitaRPS5Result error);

// Timeout Utilities

/**
 * Check if operation should timeout based on elapsed time
 */
bool protocol_timeout_check(uint64_t start_time_ms, uint32_t timeout_ms);

/**
 * Get remaining timeout time
 */
uint32_t protocol_timeout_remaining(uint64_t start_time_ms,
                                    uint32_t timeout_ms);

/**
 * Sleep with timeout check (returns early if timeout reached)
 */
VitaRPS5Result protocol_timeout_sleep(uint32_t sleep_ms, uint64_t start_time_ms,
                                      uint32_t timeout_ms);

// Delay Calculation

/**
 * Calculate next retry delay based on strategy
 */
uint32_t protocol_retry_calculate_delay(const ProtocolRetryConfig* config,
                                        uint32_t attempt_number);

/**
 * Add jitter to delay value
 */
uint32_t protocol_retry_add_jitter(uint32_t base_delay_ms, float jitter_factor);

// Error Classification

/**
 * Check if error is retryable based on configuration
 */
bool protocol_retry_is_error_retryable(VitaRPS5Result error,
                                       const ProtocolRetryConfig* config);

/**
 * Check if error is a network-related error
 */
bool protocol_retry_is_network_error(VitaRPS5Result error);

/**
 * Check if error is an authentication error
 */
bool protocol_retry_is_auth_error(VitaRPS5Result error);

/**
 * Check if error is a protocol format error
 */
bool protocol_retry_is_protocol_error(VitaRPS5Result error);

// Utility Functions

/**
 * Validate retry configuration
 */
bool protocol_retry_validate_config(const ProtocolRetryConfig* config);

/**
 * Get human-readable retry strategy name
 */
const char* protocol_retry_strategy_string(RetryStrategy strategy);

/**
 * Log retry attempt information
 */
void protocol_retry_log_attempt(uint32_t attempt, uint32_t max_attempts,
                                VitaRPS5Result last_error, uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_PROTOCOL_RETRY_H