#include "registration_cache.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logger.h"

// Cache entry structure
typedef struct {
  char console_ip[16];
  ConsoleRegistration registration;
  uint64_t cache_time_ms;
  bool is_registered;
  bool valid;
} RegistrationCacheEntry;

// Global state
static RegistrationCacheEntry cache_entries[REGISTRATION_CACHE_MAX_ENTRIES];
static RegistrationCacheStats cache_stats;
static SceUID cache_mutex = -1;
static bool cache_initialized = false;

// Internal functions
static uint64_t get_current_time_ms(void);
static int find_cache_entry(const char* console_ip);
static int find_empty_cache_slot(void);
static void expire_old_entries(void);
static void remove_cache_entry(int index);

// API Implementation

VitaRPS5Result registration_cache_init(void) {
  if (cache_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing unified registration cache system");

  // Create mutex for thread safety
  cache_mutex = sceKernelCreateMutex("reg_cache_mutex", 0, 0, NULL);
  if (cache_mutex < 0) {
    log_error("Failed to create registration cache mutex: 0x%08X", cache_mutex);
    return VITARPS5_ERROR_INIT;
  }

  // Initialize cache arrays
  memset(cache_entries, 0, sizeof(cache_entries));
  memset(&cache_stats, 0, sizeof(cache_stats));

  // Initialize core registration system
  VitaRPS5Result result = console_registration_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize core registration system: %s",
              vitarps5_result_string(result));
    sceKernelDeleteMutex(cache_mutex);
    cache_mutex = -1;
    return result;
  }

  cache_initialized = true;
  log_info("Unified registration cache initialized successfully");
  return VITARPS5_SUCCESS;
}

void registration_cache_cleanup(void) {
  if (!cache_initialized) {
    return;
  }

  log_info("Cleaning up unified registration cache system");

  if (cache_mutex >= 0) {
    sceKernelDeleteMutex(cache_mutex);
    cache_mutex = -1;
  }

  cache_initialized = false;
  log_info("Unified registration cache cleanup complete");
}

bool registration_cache_is_registered(const char* console_ip) {
  if (!console_ip || !cache_initialized) {
    return false;
  }

  sceKernelLockMutex(cache_mutex, 1, NULL);

  cache_stats.total_requests++;

  // Check cache first
  int cache_index = find_cache_entry(console_ip);
  if (cache_index >= 0) {
    RegistrationCacheEntry* entry = &cache_entries[cache_index];

    // Check if cache entry is still valid
    uint64_t current_time = get_current_time_ms();
    if (current_time - entry->cache_time_ms < REGISTRATION_CACHE_TTL_MS) {
      // Cache hit - return cached result
      bool result = entry->is_registered;
      cache_stats.cache_hits++;

      // RESEARCHER CLEANUP: Only log cache hits if we want to debug cache
      // performance Removed spam logging to keep logs clean

      sceKernelUnlockMutex(cache_mutex, 1);
      return result;
    } else {
      // Cache expired - remove entry
      log_debug("Registration cache EXPIRED for %s", console_ip);
      remove_cache_entry(cache_index);
      cache_stats.expired_entries_cleaned++;
    }
  }

  // Cache miss - query core system
  cache_stats.cache_misses++;
  bool is_registered = console_registration_is_registered(console_ip);

  log_debug("Registration cache MISS for %s, core result: %s", console_ip,
            is_registered ? "registered" : "not registered");

  // Store result in cache
  expire_old_entries();  // Clean up old entries first
  int empty_slot = find_empty_cache_slot();
  if (empty_slot >= 0) {
    RegistrationCacheEntry* entry = &cache_entries[empty_slot];
    strncpy(entry->console_ip, console_ip, sizeof(entry->console_ip) - 1);
    entry->console_ip[sizeof(entry->console_ip) - 1] = '\0';
    entry->is_registered = is_registered;
    entry->cache_time_ms = get_current_time_ms();
    entry->valid = true;

    // If registered, also cache the registration data
    if (is_registered) {
      console_registration_find_by_ip(console_ip, &entry->registration);
    }

    cache_stats.cache_entries++;
    log_debug("Cached registration result for %s in slot %d", console_ip,
              empty_slot);
  } else {
    log_warning("Registration cache full - could not cache result for %s",
                console_ip);
  }

  sceKernelUnlockMutex(cache_mutex, 1);
  return is_registered;
}

VitaRPS5Result registration_cache_get_registration(
    const char* console_ip, ConsoleRegistration* registration) {
  if (!console_ip || !registration || !cache_initialized) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  sceKernelLockMutex(cache_mutex, 1, NULL);

  // Check cache first
  int cache_index = find_cache_entry(console_ip);
  if (cache_index >= 0) {
    RegistrationCacheEntry* entry = &cache_entries[cache_index];

    // Check if cache entry is still valid and has registration data
    uint64_t current_time = get_current_time_ms();
    if (current_time - entry->cache_time_ms < REGISTRATION_CACHE_TTL_MS &&
        entry->is_registered) {
      // Cache hit - return cached registration
      *registration = entry->registration;
      // RESEARCHER CLEANUP: Removed registration data cache HIT spam logging

      sceKernelUnlockMutex(cache_mutex, 1);
      return VITARPS5_SUCCESS;
    }
  }

  // Cache miss or expired - query core system
  VitaRPS5Result result =
      console_registration_find_by_ip(console_ip, registration);

  sceKernelUnlockMutex(cache_mutex, 1);
  return result;
}

VitaRPS5Result registration_cache_add_registration(
    const char* console_ip, const ConsoleRegistration* registration) {
  if (!console_ip || !registration || !cache_initialized) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  sceKernelLockMutex(cache_mutex, 1, NULL);

  // Add to core storage first
  VitaRPS5Result result =
      console_registration_add_complete(console_ip, registration);
  if (result != VITARPS5_SUCCESS) {
    sceKernelUnlockMutex(cache_mutex, 1);
    return result;
  }

  // Invalidate cache entry for this console since data changed
  int cache_index = find_cache_entry(console_ip);
  if (cache_index >= 0) {
    remove_cache_entry(cache_index);
    log_debug("Invalidated cache entry for %s after registration update",
              console_ip);
  }

  log_info("Added registration for %s and invalidated cache", console_ip);

  sceKernelUnlockMutex(cache_mutex, 1);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result registration_cache_remove_registration(const char* console_ip) {
  if (!console_ip || !cache_initialized) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  sceKernelLockMutex(cache_mutex, 1, NULL);

  // Remove from core storage first
  VitaRPS5Result result = console_registration_remove(console_ip);

  // Invalidate cache entry regardless of core result
  int cache_index = find_cache_entry(console_ip);
  if (cache_index >= 0) {
    remove_cache_entry(cache_index);
    log_debug("Invalidated cache entry for %s after registration removal",
              console_ip);
  }

  log_info("Removed registration for %s and invalidated cache", console_ip);

  sceKernelUnlockMutex(cache_mutex, 1);
  return result;
}

void registration_cache_invalidate_console(const char* console_ip) {
  if (!console_ip || !cache_initialized) {
    return;
  }

  sceKernelLockMutex(cache_mutex, 1, NULL);

  int cache_index = find_cache_entry(console_ip);
  if (cache_index >= 0) {
    remove_cache_entry(cache_index);
    log_debug("Manually invalidated cache entry for %s", console_ip);
  }

  sceKernelUnlockMutex(cache_mutex, 1);
}

void registration_cache_clear_all(void) {
  if (!cache_initialized) {
    return;
  }

  sceKernelLockMutex(cache_mutex, 1, NULL);

  memset(cache_entries, 0, sizeof(cache_entries));
  cache_stats.cache_entries = 0;
  cache_stats.expired_entries_cleaned = 0;

  log_info("Cleared all registration cache entries");

  sceKernelUnlockMutex(cache_mutex, 1);
}

void registration_cache_get_stats(RegistrationCacheStats* stats) {
  if (!stats || !cache_initialized) {
    return;
  }

  sceKernelLockMutex(cache_mutex, 1, NULL);
  *stats = cache_stats;
  sceKernelUnlockMutex(cache_mutex, 1);
}

void registration_cache_reset_stats(void) {
  if (!cache_initialized) {
    return;
  }

  sceKernelLockMutex(cache_mutex, 1, NULL);
  memset(&cache_stats, 0, sizeof(cache_stats));

  // Recount current cache entries
  for (int i = 0; i < REGISTRATION_CACHE_MAX_ENTRIES; i++) {
    if (cache_entries[i].valid) {
      cache_stats.cache_entries++;
    }
  }

  sceKernelUnlockMutex(cache_mutex, 1);
}

// Internal Functions

static uint64_t get_current_time_ms(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick / 1000;  // Convert to milliseconds
}

static int find_cache_entry(const char* console_ip) {
  for (int i = 0; i < REGISTRATION_CACHE_MAX_ENTRIES; i++) {
    if (cache_entries[i].valid &&
        strcmp(cache_entries[i].console_ip, console_ip) == 0) {
      return i;
    }
  }
  return -1;  // Not found
}

static int find_empty_cache_slot(void) {
  for (int i = 0; i < REGISTRATION_CACHE_MAX_ENTRIES; i++) {
    if (!cache_entries[i].valid) {
      return i;
    }
  }
  return -1;  // Cache full
}

static void expire_old_entries(void) {
  uint64_t current_time = get_current_time_ms();

  for (int i = 0; i < REGISTRATION_CACHE_MAX_ENTRIES; i++) {
    if (cache_entries[i].valid &&
        current_time - cache_entries[i].cache_time_ms >=
            REGISTRATION_CACHE_TTL_MS) {
      log_debug("Expiring old cache entry for %s", cache_entries[i].console_ip);
      remove_cache_entry(i);
      cache_stats.expired_entries_cleaned++;
    }
  }
}

static void remove_cache_entry(int index) {
  if (index >= 0 && index < REGISTRATION_CACHE_MAX_ENTRIES &&
      cache_entries[index].valid) {
    memset(&cache_entries[index], 0, sizeof(RegistrationCacheEntry));
    if (cache_stats.cache_entries > 0) {
      cache_stats.cache_entries--;
    }
  }
}