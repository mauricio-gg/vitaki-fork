// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef VITAKI_THREAD_H
#define VITAKI_THREAD_H

#include "chiaki_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__PSVITA__)
#include <psp2/kernel/threadmgr.h>
#else
#include <pthread.h>
#endif

typedef void *(*ChiakiThreadFunc)(void *);

typedef struct chiaki_thread_t {
#if defined(_WIN32)
  HANDLE thread;
  ChiakiThreadFunc func;
  void *arg;
  void *ret;
#elif defined(__PSVITA__)
  SceUID thread_id;
  void *ret;
  SceUInt timeout_us;
#else
  pthread_t thread;
#endif
} ChiakiThread;

CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_create(ChiakiThread *thread,
                                                   ChiakiThreadFunc func,
                                                   void *arg);
CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_join(ChiakiThread *thread,
                                                 void **retval);
CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_timedjoin(ChiakiThread *thread,
                                                      void **retval,
                                                      uint64_t timeout_ms);
CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_set_name(ChiakiThread *thread,
                                                     const char *name);

typedef struct chiaki_mutex_t {
#if defined(_WIN32)
  CRITICAL_SECTION cs;
#elif defined(__PSVITA__)
  SceUID mutex_id;
#else
  pthread_mutex_t mutex;
#endif
} ChiakiMutex;

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_init(ChiakiMutex *mutex, bool rec);
CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_fini(ChiakiMutex *mutex);
CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_lock(ChiakiMutex *mutex);
CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_trylock(ChiakiMutex *mutex);
CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_unlock(ChiakiMutex *mutex);

typedef struct chiaki_cond_t {
#if defined(_WIN32)
  CONDITION_VARIABLE cond;
#elif defined(__PSVITA__)
  SceUID cond_id;
  unsigned int timeout_us;
#else
  pthread_cond_t cond;
#endif
} ChiakiCond;

typedef bool (*ChiakiCheckPred)(void *);

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_init(ChiakiCond *cond,
                                               ChiakiMutex *mutex);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_fini(ChiakiCond *cond);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_wait(ChiakiCond *cond,
                                               ChiakiMutex *mutex);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_timedwait(ChiakiCond *cond,
                                                    ChiakiMutex *mutex,
                                                    uint64_t timeout_ms);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_wait_pred(ChiakiCond *cond,
                                                    ChiakiMutex *mutex,
                                                    ChiakiCheckPred check_pred,
                                                    void *check_pred_user);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_timedwait_pred(
    ChiakiCond *cond, ChiakiMutex *mutex, uint64_t timeout_ms,
    ChiakiCheckPred check_pred, void *check_pred_user);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_signal(ChiakiCond *cond);
CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_broadcast(ChiakiCond *cond);

typedef struct chiaki_bool_pred_cond_t {
  ChiakiCond cond;
  ChiakiMutex mutex;
  bool pred;
} ChiakiBoolPredCond;

CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_init(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_fini(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_lock(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_unlock(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_wait(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_timedwait(ChiakiBoolPredCond *cond, uint64_t timeout_ms);
CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_signal(ChiakiBoolPredCond *cond);
CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_broadcast(ChiakiBoolPredCond *cond);

#ifdef __cplusplus
}

class ChiakiMutexLock {
 private:
  ChiakiMutex *const mutex;

 public:
  ChiakiMutexLock(ChiakiMutex *mutex) : mutex(mutex) {
    chiaki_mutex_lock(mutex);
  }
  ~ChiakiMutexLock() { chiaki_mutex_unlock(mutex); }
};
#endif

#endif  // CHIAKI_THREAD_H
