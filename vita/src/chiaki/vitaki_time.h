// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef VITAKI_TIME_H
#define VITAKI_TIME_H

#include <stdint.h>

#include "chiaki_common.h"

#ifdef __cplusplus
extern "C" {
#endif

CHIAKI_EXPORT uint64_t chiaki_time_now_monotonic_us();

static inline uint64_t chiaki_time_now_monotonic_ms() {
  return chiaki_time_now_monotonic_us() / 1000;
}

#ifdef __cplusplus
}
#endif

#endif  // CHIAKI_TIME_H
