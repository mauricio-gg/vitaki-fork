#include "console_state_thread.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../discovery/ps5_discovery.h"
#include "../utils/logger.h"

// Background state thread context
struct ConsoleStateThread {
  ConsoleStateThreadConfig config;
  ConsoleCacheData* console_cache;

  // Thread management
  SceUID thread_id;
  bool thread_running;
  bool thread_should_stop;
  bool thread_paused;

  // State tracking
  uint32_t current_console_index;  // For staggered checking
  uint64_t thread_start_time_ms;
  uint64_t last_check_time_ms;

  // Statistics
  ConsoleStateThreadStats stats;

  // Immediate check requests (simple flag-based)
  char pending_check_ip[16];  // IP address for immediate check
  bool has_pending_check;     // Flag for pending immediate check
};

// Global subsystem state
static bool console_state_thread_initialized = false;

// Internal function declarations
static int console_state_thread_func(SceSize args, void* argp);
static uint64_t get_timestamp_ms(void);
static VitaRPS5Result perform_single_console_check(ConsoleStateThread* context,
                                                   UIConsoleInfo* console);
static bool should_check_console(const UIConsoleInfo* console,
                                 uint64_t current_time,
                                 uint32_t check_interval_ms);

// Subsystem lifecycle

VitaRPS5Result console_state_thread_init(void) {
  if (console_state_thread_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing console state background thread subsystem");
  console_state_thread_initialized = true;

  return VITARPS5_SUCCESS;
}

void console_state_thread_cleanup(void) {
  if (!console_state_thread_initialized) {
    return;
  }

  log_info("Cleaning up console state background thread subsystem");
  console_state_thread_initialized = false;
}

// Configuration helpers

void console_state_thread_get_default_config(ConsoleStateThreadConfig* config) {
  if (!config) return;

  memset(config, 0, sizeof(ConsoleStateThreadConfig));
  // DISCOVERY OPTIMIZATION: Reduce intervals for responsive UI
  config->check_interval_ms =
      5000;  // Check each console every 5 seconds (was 20s)
  config->thread_sleep_ms =
      1000;  // Thread sleeps 1 second between iterations (was 2s)
  config->max_consoles_per_cycle =
      2;  // Check 2 consoles per cycle for faster updates (was 1)
  config->stagger_checks = true;  // Spread checks across time
  config->enabled = true;         // Background checking enabled
}

// Thread management

VitaRPS5Result console_state_thread_start(
    const ConsoleStateThreadConfig* config, ConsoleCacheData* console_cache,
    ConsoleStateThread** thread) {
  if (!console_state_thread_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!console_cache || !thread) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ConsoleStateThread* new_thread = malloc(sizeof(ConsoleStateThread));
  if (!new_thread) {
    return VITARPS5_ERROR_MEMORY;
  }

  // Initialize thread context
  memset(new_thread, 0, sizeof(ConsoleStateThread));
  new_thread->console_cache = console_cache;

  // Set configuration (use defaults if none provided)
  if (config) {
    new_thread->config = *config;
  } else {
    console_state_thread_get_default_config(&new_thread->config);
  }

  // Initialize state
  new_thread->thread_running = false;
  new_thread->thread_should_stop = false;
  new_thread->thread_paused = false;
  new_thread->current_console_index = 0;
  new_thread->thread_start_time_ms = get_timestamp_ms();
  new_thread->has_pending_check = false;

  log_info("Starting console state background thread (check interval: %u ms)",
           new_thread->config.check_interval_ms);

  // Create and start background thread
  new_thread->thread_id = sceKernelCreateThread(
      "console_state_bg", console_state_thread_func,
      0x10000100,  // Priority (same as other background threads)
      0x10000,     // Stack size
      0,           // Attributes
      0,           // CPU affinity mask
      NULL         // Options
  );

  if (new_thread->thread_id < 0) {
    log_error("Failed to create console state background thread: 0x%08X",
              new_thread->thread_id);
    free(new_thread);
    return VITARPS5_ERROR_INIT;
  }

  // Start the thread
  new_thread->thread_running = true;
  int start_result = sceKernelStartThread(
      new_thread->thread_id, sizeof(ConsoleStateThread*), &new_thread);

  if (start_result < 0) {
    log_error("Failed to start console state background thread: 0x%08X",
              start_result);
    sceKernelDeleteThread(new_thread->thread_id);
    free(new_thread);
    return VITARPS5_ERROR_INIT;
  }

  log_info("Console state background thread started successfully");
  *thread = new_thread;
  return VITARPS5_SUCCESS;
}

void console_state_thread_stop(ConsoleStateThread* thread) {
  if (!thread) {
    return;
  }

  log_info("Stopping console state background thread");

  // Signal thread to stop
  thread->thread_should_stop = true;
  thread->thread_running = false;

  // Wait for thread to complete (with timeout)
  if (thread->thread_id >= 0) {
    SceUInt timeout = 5000000;  // 5 seconds in microseconds
    int wait_result = sceKernelWaitThreadEnd(thread->thread_id, NULL, &timeout);

    if (wait_result < 0) {
      log_warning("Thread did not stop gracefully within timeout: 0x%08X",
                  wait_result);
      // PS Vita doesn't have sceKernelTerminateThread, so we just log and
      // continue
      log_warning("Console state thread may still be running in background");
    }

    sceKernelDeleteThread(thread->thread_id);
  }

  // Log final statistics
  uint64_t uptime = get_timestamp_ms() - thread->thread_start_time_ms;
  log_info(
      "Background thread stopped - Uptime: %llu ms, Checks: %u (%u successful)",
      uptime, thread->stats.total_checks_performed,
      thread->stats.successful_checks);

  free(thread);
}

// Thread control

VitaRPS5Result console_state_thread_pause(ConsoleStateThread* thread) {
  if (!thread) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  thread->thread_paused = true;
  log_debug("Console state background thread paused");
  return VITARPS5_SUCCESS;
}

VitaRPS5Result console_state_thread_resume(ConsoleStateThread* thread) {
  if (!thread) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  thread->thread_paused = false;
  log_debug("Console state background thread resumed");
  return VITARPS5_SUCCESS;
}

bool console_state_thread_is_running(const ConsoleStateThread* thread) {
  return thread && thread->thread_running && !thread->thread_should_stop;
}

VitaRPS5Result console_state_thread_request_check(ConsoleStateThread* thread,
                                                  const char* console_ip) {
  if (!thread || !console_ip) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Simple flag-based immediate check request
  strncpy(thread->pending_check_ip, console_ip,
          sizeof(thread->pending_check_ip) - 1);
  thread->pending_check_ip[sizeof(thread->pending_check_ip) - 1] = '\0';
  thread->has_pending_check = true;

  log_debug("Requested immediate state check for %s", console_ip);
  return VITARPS5_SUCCESS;
}

// Configuration management

VitaRPS5Result console_state_thread_update_config(
    ConsoleStateThread* thread, const ConsoleStateThreadConfig* config) {
  if (!thread || !config) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  thread->config = *config;
  log_info("Updated background thread config: check_interval=%u ms",
           config->check_interval_ms);
  return VITARPS5_SUCCESS;
}

// Statistics

VitaRPS5Result console_state_thread_get_stats(const ConsoleStateThread* thread,
                                              ConsoleStateThreadStats* stats) {
  if (!thread || !stats) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  *stats = thread->stats;
  stats->thread_uptime_ms = get_timestamp_ms() - thread->thread_start_time_ms;
  stats->is_running = thread->thread_running;
  stats->is_paused = thread->thread_paused;

  return VITARPS5_SUCCESS;
}

// Background thread implementation

static int console_state_thread_func(SceSize args, void* argp) {
  if (args != sizeof(ConsoleStateThread*)) {
    log_error("Console state thread: Invalid argument size");
    return -1;
  }

  ConsoleStateThread* context = *(ConsoleStateThread**)argp;
  if (!context) {
    log_error("Console state thread: Invalid context");
    return -1;
  }

  log_info("Console state background thread started");

  // DISCOVERY OPTIMIZATION: Immediate startup discovery burst
  log_info(
      "🚀 Starting immediate discovery burst for responsive console states");
  uint64_t startup_time = get_timestamp_ms();

  // Check all consoles immediately on startup (ignore intervals)
  for (uint32_t i = 0; i < context->console_cache->console_count; i++) {
    UIConsoleInfo* console = &context->console_cache->consoles[i];
    if (console && strlen(console->ip_address) > 0) {
      log_debug("Startup check for console %s", console->ip_address);
      perform_single_console_check(context, console);
      context->stats.successful_checks++;

      // Small delay between startup checks to avoid network congestion
      sceKernelDelayThread(200000);  // 200ms delay
    }
  }

  uint64_t startup_duration = get_timestamp_ms() - startup_time;
  log_info("✅ Startup discovery burst completed in %llu ms", startup_duration);

  while (!context->thread_should_stop) {
    // Skip processing if paused or disabled
    if (context->thread_paused || !context->config.enabled) {
      sceKernelDelayThread(context->config.thread_sleep_ms * 1000);
      continue;
    }

    uint64_t current_time = get_timestamp_ms();
    context->stats.last_check_time_ms = current_time;

    // Handle immediate check requests first
    if (context->has_pending_check) {
      log_debug("Processing immediate check request for %s",
                context->pending_check_ip);

      // Find console in cache and check it
      for (uint32_t i = 0; i < context->console_cache->console_count; i++) {
        UIConsoleInfo* console = &context->console_cache->consoles[i];
        if (strcmp(console->ip_address, context->pending_check_ip) == 0) {
          perform_single_console_check(context, console);
          break;
        }
      }

      context->has_pending_check = false;
    }

    // Perform regular staggered checks
    uint32_t checks_this_cycle = 0;
    uint32_t consoles_checked = 0;

    // Staggered checking: start from where we left off
    uint32_t start_index = context->current_console_index;

    for (uint32_t offset = 0;
         offset < context->console_cache->console_count &&
         checks_this_cycle < context->config.max_consoles_per_cycle;
         offset++) {
      uint32_t i =
          (start_index + offset) % context->console_cache->console_count;
      UIConsoleInfo* console = &context->console_cache->consoles[i];

      if (should_check_console(console, current_time,
                               context->config.check_interval_ms)) {
        perform_single_console_check(context, console);
        checks_this_cycle++;
        consoles_checked++;

        // Update position for next cycle
        context->current_console_index =
            (i + 1) % context->console_cache->console_count;
      }
    }

    if (consoles_checked > 0) {
      log_debug("Background thread checked %u consoles this cycle",
                consoles_checked);
    }

    // Sleep before next iteration
    sceKernelDelayThread(context->config.thread_sleep_ms * 1000);
  }

  log_info("Console state background thread completed");
  return 0;
}

// Helper functions

static VitaRPS5Result perform_single_console_check(ConsoleStateThread* context,
                                                   UIConsoleInfo* console) {
  if (!context || !console) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  context->stats.total_checks_performed++;

  log_debug("Background checking state for %s (%s)", console->display_name,
            console->ip_address);

  // Perform lightweight state check
  ConsoleState new_state;
  VitaRPS5Result result = ps5_discovery_lightweight_state_check(
      console->ip_address, console->console_type, &new_state);

  if (result == VITARPS5_SUCCESS) {
    context->stats.successful_checks++;

    // Convert ConsoleState to ConsoleDiscoveryState for compatibility
    ConsoleDiscoveryState discovery_state;
    switch (new_state) {
      case CONSOLE_STATE_READY:
        discovery_state = CONSOLE_DISCOVERY_STATE_READY;
        break;
      case CONSOLE_STATE_STANDBY:
        discovery_state = CONSOLE_DISCOVERY_STATE_STANDBY;
        break;
      default:
        discovery_state = CONSOLE_DISCOVERY_STATE_UNKNOWN;
        break;
    }

    // Update cache if state changed
    if (console->discovery_state != discovery_state) {
      // CRITICAL FIX D: Add debounce for STANDBY transitions during active
      // connect
      bool should_apply_change = true;

      if (console->discovery_state == CONSOLE_DISCOVERY_STATE_READY &&
          discovery_state == CONSOLE_DISCOVERY_STATE_STANDBY) {
        // Check if session manager is currently doing session init
        extern bool session_is_in_phase(int phase);  // TODO: Add this function

        // For now, use a simple time-based debounce for READY→STANDBY
        uint64_t time_since_last_state_change =
            get_timestamp_ms() - console->last_seen;

        if (time_since_last_state_change <
            3000) {  // Less than 3 seconds since last change
          log_warning(
              "CRITICAL FIX D: Debouncing READY→STANDBY transition for %s (too "
              "recent: %llums)",
              console->display_name, time_since_last_state_change);
          should_apply_change = false;

          // Still update last_seen to prevent this from being stuck
          console->last_seen = get_timestamp_ms();
        }
      }

      if (should_apply_change) {
        log_info("Background thread detected state change: %s -> %s (%s)",
                 (console->discovery_state == CONSOLE_DISCOVERY_STATE_READY)
                     ? "READY"
                 : (console->discovery_state == CONSOLE_DISCOVERY_STATE_STANDBY)
                     ? "STANDBY"
                     : "UNKNOWN",
                 (discovery_state == CONSOLE_DISCOVERY_STATE_READY) ? "READY"
                 : (discovery_state == CONSOLE_DISCOVERY_STATE_STANDBY)
                     ? "STANDBY"
                     : "UNKNOWN",
                 console->display_name);

        console->discovery_state = discovery_state;
        console->last_seen = get_timestamp_ms();

        // Save updated cache (this is thread-safe in our implementation)
        console_storage_save(context->console_cache);
        context->stats.cache_updates++;
      }
    } else {
      // Update last_seen even if state didn't change
      console->last_seen = get_timestamp_ms();
    }

  } else {
    context->stats.failed_checks++;

    // DISCOVERY OPTIMIZATION: Intelligent state memory during failures
    uint64_t time_since_last_success = get_timestamp_ms() - console->last_seen;

    if (time_since_last_success <
        30000) {  // Less than 30 seconds since last success
      // Keep previous state for short-term failures (likely network hiccups)
      log_debug(
          "Background state check failed for %s, keeping previous state (%ums "
          "ago): %s",
          console->display_name, (uint32_t)time_since_last_success,
          vitarps5_result_string(result));
    } else {
      // Long-term failure - transition to unknown state
      if (console->discovery_state != CONSOLE_DISCOVERY_STATE_UNKNOWN) {
        log_info("Console %s failed checks for %llums, marking as UNKNOWN: %s",
                 console->display_name, time_since_last_success,
                 vitarps5_result_string(result));
        console->discovery_state = CONSOLE_DISCOVERY_STATE_UNKNOWN;
        console_storage_save(context->console_cache);
        context->stats.cache_updates++;
      } else {
        log_debug("Background state check failed for %s (already unknown): %s",
                  console->display_name, vitarps5_result_string(result));
      }
    }
  }

  return result;
}

static bool should_check_console(const UIConsoleInfo* console,
                                 uint64_t current_time,
                                 uint32_t check_interval_ms) {
  if (!console) {
    return false;
  }

  // DISCOVERY OPTIMIZATION: Adaptive intervals based on console state
  uint32_t adaptive_interval = check_interval_ms;

  if (console->discovery_state == CONSOLE_DISCOVERY_STATE_UNKNOWN) {
    // Unknown state: check very frequently until we know the state
    adaptive_interval = 2000;  // 2 seconds for unknown consoles
  } else if (console->discovery_state == CONSOLE_DISCOVERY_STATE_READY) {
    // Ready state: check moderately - user might start session
    adaptive_interval = 4000;  // 4 seconds for ready consoles
  } else if (console->discovery_state == CONSOLE_DISCOVERY_STATE_STANDBY) {
    // Standby state: check less frequently - state changes are rare
    adaptive_interval = 8000;  // 8 seconds for standby consoles
  }
  // Other states use default interval

  // Check if enough time has passed since last update
  uint64_t time_since_update = current_time - console->last_seen;

  bool should_check = time_since_update >= adaptive_interval;

  if (should_check && adaptive_interval != check_interval_ms) {
    log_debug("Adaptive check for %s: state=%s, interval=%ums (default=%ums)",
              console->ip_address,
              (console->discovery_state == CONSOLE_DISCOVERY_STATE_UNKNOWN)
                  ? "UNKNOWN"
              : (console->discovery_state == CONSOLE_DISCOVERY_STATE_READY)
                  ? "READY"
              : (console->discovery_state == CONSOLE_DISCOVERY_STATE_STANDBY)
                  ? "STANDBY"
                  : "OTHER",
              adaptive_interval, check_interval_ms);
  }

  return should_check;
}

static uint64_t get_timestamp_ms(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick / 1000;
}