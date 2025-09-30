#pragma once

#include "../core/vitarps5.h"
#include "console_registration.h"

/**
 * Unified Registration Cache API
 *
 * This module provides a thread-safe unified interface for console registration
 * data access with automatic caching and invalidation. It consolidates the
 * core registration storage and UI cache layer into a single API.
 */

// Cache configuration
#define REGISTRATION_CACHE_TTL_MS (5 * 60 * 1000)  // 5 minutes
#define REGISTRATION_CACHE_MAX_ENTRIES 32

/**
 * Initialize the unified registration cache system
 */
VitaRPS5Result registration_cache_init(void);

/**
 * Cleanup the unified registration cache system
 */
void registration_cache_cleanup(void);

/**
 * Check if a console is registered (thread-safe, with caching)
 * This is the primary API that should be used instead of direct calls
 * to console_registration_is_registered() or ui_core_is_console_registered()
 */
bool registration_cache_is_registered(const char* console_ip);

/**
 * Get full registration data for a console (thread-safe, with caching)
 * Returns VITARPS5_SUCCESS if found, VITARPS5_ERROR_NOT_FOUND if not registered
 */
VitaRPS5Result registration_cache_get_registration(
    const char* console_ip, ConsoleRegistration* registration);

/**
 * Add or update a console registration (thread-safe, auto-invalidates cache)
 */
VitaRPS5Result registration_cache_add_registration(
    const char* console_ip, const ConsoleRegistration* registration);

/**
 * Remove a console registration (thread-safe, auto-invalidates cache)
 */
VitaRPS5Result registration_cache_remove_registration(const char* console_ip);

/**
 * Invalidate cache entry for a specific console (thread-safe)
 * Use this when registration data changes outside of this API
 */
void registration_cache_invalidate_console(const char* console_ip);

/**
 * Clear all cache entries (thread-safe)
 * Use this for testing or when doing bulk operations
 */
void registration_cache_clear_all(void);

/**
 * Get cache statistics for monitoring/debugging
 */
typedef struct {
  uint32_t total_requests;
  uint32_t cache_hits;
  uint32_t cache_misses;
  uint32_t cache_entries;
  uint32_t expired_entries_cleaned;
} RegistrationCacheStats;

void registration_cache_get_stats(RegistrationCacheStats* stats);
void registration_cache_reset_stats(void);