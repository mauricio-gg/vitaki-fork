// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 threading implementation for PS Vita

#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logger.h"
#include "vitaki_thread.h"  // Using vitaki threading implementation

// Mutex implementation
ChiakiErrorCode chiaki_mutex_init(ChiakiMutex *mutex, bool recursive) {
  if (!mutex) {
    return CHIAKI_ERR_INVALID_DATA;
  }

  const char *mutex_name = recursive ? "ChiakiMutexR" : "ChiakiMutex";

  mutex->mutex_id = sceKernelCreateMutex(mutex_name, 0, 0, NULL);
  if (mutex->mutex_id < 0) {
    log_error("Failed to create mutex: 0x%08X", mutex->mutex_id);
    return CHIAKI_ERR_UNKNOWN;
  }

  return CHIAKI_ERR_SUCCESS;
}

void chiaki_mutex_fini(ChiakiMutex *mutex) {
  if (!mutex || mutex->mutex_id < 0) {
    return;
  }

  sceKernelDeleteMutex(mutex->mutex_id);
  mutex->mutex_id = -1;
}

ChiakiErrorCode chiaki_mutex_lock(ChiakiMutex *mutex) {
  if (!mutex || mutex->mutex_id < 0) {
    return CHIAKI_ERR_INVALID_DATA;
  }

  int result = sceKernelLockMutex(mutex->mutex_id, 1, NULL);
  if (result < 0) {
    log_error("Failed to lock mutex: 0x%08X", result);
    return CHIAKI_ERR_UNKNOWN;
  }

  return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_mutex_unlock(ChiakiMutex *mutex) {
  if (!mutex || mutex->mutex_id < 0) {
    return CHIAKI_ERR_INVALID_DATA;
  }

  int result = sceKernelUnlockMutex(mutex->mutex_id, 1);
  if (result < 0) {
    log_error("Failed to unlock mutex: 0x%08X", result);
    return CHIAKI_ERR_UNKNOWN;
  }

  return CHIAKI_ERR_SUCCESS;
}

// Condition variable implementation
ChiakiErrorCode chiaki_cond_init(ChiakiCond *cond, ChiakiMutex *mutex) {
  if (!cond || !mutex) {
    return CHIAKI_ERR_INVALID_DATA;
  }

  cond->cond_id = sceKernelCreateCond("ChiakiCond", 0, mutex->mutex_id, NULL);
  if (cond->cond_id < 0) {
    log_error("Failed to create condition variable: 0x%08X", cond->cond_id);
    return CHIAKI_ERR_UNKNOWN;
  }

  cond->mutex = mutex;
  return CHIAKI_ERR_SUCCESS;
}

void chiaki_cond_fini(ChiakiCond *cond) {
  if (!cond || cond->cond_id < 0) {
    return;
  }

  sceKernelDeleteCond(cond->cond_id);
  cond->cond_id = -1;
  cond->mutex = NULL;
}

ChiakiErrorCode chiaki_cond_signal(ChiakiCond *cond) {
  if (!cond || cond->cond_id < 0) {
    return CHIAKI_ERR_INVALID_DATA;
  }

  int result = sceKernelSignalCond(cond->cond_id);
  if (result < 0) {
    log_error("Failed to signal condition variable: 0x%08X", result);
    return CHIAKI_ERR_UNKNOWN;
  }

  return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_cond_timedwait_pred(ChiakiCond *cond, ChiakiMutex *mutex,
                                           uint32_t timeout_ms,
                                           bool (*pred)(void *),
                                           void *pred_user) {
  if (!cond || !mutex || cond->cond_id < 0) {
    return CHIAKI_ERR_INVALID_DATA;
  }

  // Convert timeout to microseconds
  SceUInt timeout_us = timeout_ms * 1000;

  while (pred && !pred(pred_user)) {
    int result = sceKernelWaitCond(cond->cond_id, &timeout_us);
    if (result < 0) {
      if (result == 0x80020001) {  // Generic timeout error code
        return CHIAKI_ERR_TIMEOUT;
      }
      log_error("Failed to wait on condition variable: 0x%08X", result);
      return CHIAKI_ERR_UNKNOWN;
    }
  }

  return CHIAKI_ERR_SUCCESS;
}

// Note: Thread functions are already implemented in chiaki_thread_vita.c
// This file provides only mutex and condition variable implementations