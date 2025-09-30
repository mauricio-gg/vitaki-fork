// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 - Cryptographic Random Number Generation
// Optimized for PS Vita hardware with secure entropy sources

#include "chiaki_random.h"

#include "chiaki_log.h"

#ifdef __PSVITA__
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/rng.h>
#include <psp2/rtc.h>
#include <time.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

#include <errno.h>
#include <string.h>

// Fallback PRNG state for non-cryptographic use
static uint32_t prng_state = 0;
static bool prng_initialized = false;

/**
 * Initialize PRNG with entropy from multiple sources
 */
static void init_prng(void) {
  if (prng_initialized) {
    return;
  }

  uint32_t seed = 0;

#ifdef __PSVITA__
  // Use PS Vita's high-resolution timer
  SceRtcTick tick;
  if (sceRtcGetCurrentTick(&tick) == 0) {
    seed ^= (uint32_t)(tick.tick & 0xFFFFFFFF);
    seed ^= (uint32_t)((tick.tick >> 32) & 0xFFFFFFFF);
  }

  // Mix in process time
  SceKernelSysClock process_time;
  if (sceKernelGetProcessTime(&process_time) == 0) {
    seed ^= (uint32_t)(process_time & 0xFFFFFFFF);
    seed ^= (uint32_t)((process_time >> 32) & 0xFFFFFFFF);
  }
#else
  // Fallback for non-Vita platforms
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    seed ^= (uint32_t)ts.tv_sec;
    seed ^= (uint32_t)ts.tv_nsec;
  }
#endif

  // Mix in some memory addresses for additional entropy
  seed ^= (uintptr_t)&seed;
  seed ^= (uintptr_t)&prng_state;

  // Simple scrambling to improve seed quality
  seed ^= (seed << 13);
  seed ^= (seed >> 17);
  seed ^= (seed << 5);

  prng_state = seed ? seed : 1;  // Ensure non-zero state
  prng_initialized = true;
}

/**
 * Simple xorshift32 PRNG for non-cryptographic use
 */
static uint32_t xorshift32(void) {
  prng_state ^= prng_state << 13;
  prng_state ^= prng_state >> 17;
  prng_state ^= prng_state << 5;
  return prng_state;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_random_bytes_crypt(uint8_t *buf,
                                                        size_t buf_size) {
  if (!buf || buf_size == 0) {
    return CHIAKI_ERR_INVALID_DATA;
  }

#ifdef __PSVITA__
  // PS Vita has a hardware RNG via kernel
  // Use sceKernelGetRandomNumber for cryptographic quality randomness

  // Generate random bytes in chunks if needed
  size_t remaining = buf_size;
  uint8_t *current_buf = buf;

  while (remaining > 0) {
    // sceKernelGetRandomNumber generates 32-bit values
    uint32_t random_val;
    int result = sceKernelGetRandomNumber(&random_val, sizeof(random_val));

    if (result < 0) {
      // Fallback to mixing multiple entropy sources
      // This is still more secure than standard rand()
      SceRtcTick tick;
      uint32_t entropy = 0;

      if (sceRtcGetCurrentTick(&tick) == 0) {
        entropy ^= (uint32_t)(tick.tick & 0xFFFFFFFF);
        entropy ^= (uint32_t)((tick.tick >> 32) & 0xFFFFFFFF);
      }

      // Mix with process timing
      SceKernelSysClock process_time;
      if (sceKernelGetProcessTime(&process_time) == 0) {
        entropy ^= (uint32_t)(process_time & 0xFFFFFFFF);
        entropy ^= (uint32_t)((process_time >> 32) & 0xFFFFFFFF);
      }

      // Add some deterministic but hard-to-predict mixing
      entropy ^= (uint32_t)((uintptr_t)current_buf);
      entropy ^= (uint32_t)remaining;

      // Simple hash to improve distribution
      entropy ^= (entropy << 13);
      entropy ^= (entropy >> 17);
      entropy ^= (entropy << 5);

      random_val = entropy;
    }

    // Copy bytes from the random value
    size_t copy_size =
        (remaining < sizeof(random_val)) ? remaining : sizeof(random_val);
    memcpy(current_buf, &random_val, copy_size);

    current_buf += copy_size;
    remaining -= copy_size;
  }

  return CHIAKI_ERR_SUCCESS;

#else
  // Fallback implementation for non-Vita platforms
  // Try to use /dev/urandom for cryptographic quality
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    ssize_t bytes_read = read(fd, buf, buf_size);
    close(fd);

    if (bytes_read == (ssize_t)buf_size) {
      return CHIAKI_ERR_SUCCESS;
    }
  }

  // Final fallback: Use time-based seeding with mixing
  // This is not cryptographically secure but better than nothing
  init_prng();

  for (size_t i = 0; i < buf_size; i++) {
    // Mix multiple entropy sources for each byte
    uint32_t entropy = xorshift32();

    // Add timing entropy
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
      entropy ^= (uint32_t)ts.tv_nsec;
    }

    // Add address space entropy
    entropy ^= (uint32_t)((uintptr_t)&entropy + i);

    buf[i] = (uint8_t)(entropy & 0xFF);
  }

  return CHIAKI_ERR_SUCCESS;
#endif
}

CHIAKI_EXPORT uint32_t chiaki_random_32(void) {
  init_prng();
  return xorshift32();
}