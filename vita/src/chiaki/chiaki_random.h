// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 - Cryptographic Random Number Generation
// Based on vitaki-fork/lib/include/chiaki/random.h

#ifndef CHIAKI_RANDOM_H
#define CHIAKI_RANDOM_H

#include <stdint.h>
#include <stdlib.h>

#include "chiaki_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Cryptographically secure random number generation
 * Critical for PS5 authentication and security
 *
 * @param buf Buffer to fill with random bytes
 * @param buf_size Number of bytes to generate
 * @return CHIAKI_ERR_SUCCESS on success, error code on failure
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_random_bytes_crypt(uint8_t *buf,
                                                        size_t buf_size);

/**
 * Generate a random 32-bit number
 * Note: This is for non-cryptographic use only
 *
 * @return Random 32-bit unsigned integer
 */
CHIAKI_EXPORT uint32_t chiaki_random_32(void);

#ifdef __cplusplus
}
#endif

#endif  // CHIAKI_RANDOM_H