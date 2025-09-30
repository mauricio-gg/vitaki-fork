// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "vitaki_time.h"

#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __PSVITA__
#include <psp2/kernel/processmgr.h>
#endif

CHIAKI_EXPORT uint64_t chiaki_time_now_monotonic_us() {
#if _WIN32
  LARGE_INTEGER f;
  if (!QueryPerformanceFrequency(&f)) return 0;
  LARGE_INTEGER v;
  if (!QueryPerformanceCounter(&v)) return 0;
  v.QuadPart *= 1000000;
  v.QuadPart /= f.QuadPart;
  return v.QuadPart;
#elif __PSVITA__
  SceKernelSysClock clk;
  sceKernelGetProcessTime(&clk);
  return clk;
#else
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return time.tv_sec * 1000000 + time.tv_nsec / 1000;
#endif
}
