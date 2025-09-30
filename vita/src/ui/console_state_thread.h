#ifndef VITARPS5_CONSOLE_STATE_THREAD_H
#define VITARPS5_CONSOLE_STATE_THREAD_H

#include <psp2/types.h>
#include <stdbool.h>
#include <stdint.h>

#include "../core/console_storage.h"
#include "../core/vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Background Console State Thread
 *
 * This module provides a background thread that continuously updates console
 * states without blocking the main UI thread. It provides professional-grade
 * UI responsiveness by moving all network operations to the background.
 */

// Background state checker configuration
typedef struct {
  uint32_t
      check_interval_ms;  // Interval between console checks (default: 20000ms)
  uint32_t
      thread_sleep_ms;  // Thread sleep between iterations (default: 1000ms)
  uint32_t
      max_consoles_per_cycle;  // Max consoles to check per cycle (default: 1)
  bool stagger_checks;  // Stagger checks across multiple cycles (default: true)
  bool enabled;         // Enable/disable background checking (default: true)
} ConsoleStateThreadConfig;

// Background state thread context (opaque)
typedef struct ConsoleStateThread ConsoleStateThread;

// Thread lifecycle management

/**
 * Initialize the console state thread subsystem
 */
VitaRPS5Result console_state_thread_init(void);

/**
 * Cleanup the console state thread subsystem
 */
void console_state_thread_cleanup(void);

/**
 * Create and start background state checking thread
 * @param config Thread configuration (NULL for defaults)
 * @param console_cache Pointer to console cache to update
 * @param thread Output thread context
 */
VitaRPS5Result console_state_thread_start(
    const ConsoleStateThreadConfig* config, ConsoleCacheData* console_cache,
    ConsoleStateThread** thread);

/**
 * Stop and destroy background state checking thread
 * @param thread Thread context to stop and cleanup
 */
void console_state_thread_stop(ConsoleStateThread* thread);

// Thread control

/**
 * Pause background state checking (temporarily)
 * @param thread Thread context
 */
VitaRPS5Result console_state_thread_pause(ConsoleStateThread* thread);

/**
 * Resume background state checking
 * @param thread Thread context
 */
VitaRPS5Result console_state_thread_resume(ConsoleStateThread* thread);

/**
 * Check if background thread is running
 * @param thread Thread context
 * @return True if thread is active and running
 */
bool console_state_thread_is_running(const ConsoleStateThread* thread);

/**
 * Request immediate state check for specific console
 * @param thread Thread context
 * @param console_ip IP address of console to check
 */
VitaRPS5Result console_state_thread_request_check(ConsoleStateThread* thread,
                                                  const char* console_ip);

// Configuration helpers

/**
 * Get default configuration for background state thread
 * @param config Output configuration structure
 */
void console_state_thread_get_default_config(ConsoleStateThreadConfig* config);

/**
 * Update thread configuration (takes effect on next cycle)
 * @param thread Thread context
 * @param config New configuration
 */
VitaRPS5Result console_state_thread_update_config(
    ConsoleStateThread* thread, const ConsoleStateThreadConfig* config);

// Statistics and monitoring

typedef struct {
  uint32_t total_checks_performed;  // Total state checks performed
  uint32_t successful_checks;       // Successful state checks
  uint32_t failed_checks;           // Failed state checks
  uint32_t cache_updates;           // Console cache updates
  uint64_t last_check_time_ms;      // Timestamp of last check
  uint64_t thread_uptime_ms;        // Thread uptime
  bool is_running;                  // Thread running status
  bool is_paused;                   // Thread paused status
} ConsoleStateThreadStats;

/**
 * Get background thread statistics
 * @param thread Thread context
 * @param stats Output statistics structure
 */
VitaRPS5Result console_state_thread_get_stats(const ConsoleStateThread* thread,
                                              ConsoleStateThreadStats* stats);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_CONSOLE_STATE_THREAD_H