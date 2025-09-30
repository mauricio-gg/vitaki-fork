// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#define _GNU_SOURCE

#include "vitaki_thread.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "vitaki_time.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

#ifdef __PSVITA__
#include <psp2/kernel/error.h>
#include <psp2/kernel/processmgr.h>
#endif

#if _WIN32
static DWORD WINAPI win32_thread_func(LPVOID param) {
  ChiakiThread *thread = (ChiakiThread *)param;
  thread->ret = thread->func(thread->arg);
  return 0;
}
#endif

#ifdef __SWITCH__
int64_t get_thread_limit() {
  uint64_t resource_limit_handle_value = INVALID_HANDLE;
  svcGetInfo(&resource_limit_handle_value, InfoType_ResourceLimit,
             INVALID_HANDLE, 0);
  int64_t thread_cur_value = 0, thread_lim_value = 0;
  svcGetResourceLimitCurrentValue(&thread_cur_value,
                                  resource_limit_handle_value,
                                  LimitableResource_Threads);
  svcGetResourceLimitLimitValue(&thread_lim_value, resource_limit_handle_value,
                                LimitableResource_Threads);
  // printf("thread_cur_value: %lu, thread_lim_value: %lu\n", thread_cur_value,
  // thread_lim_value);
  return thread_lim_value - thread_cur_value;
}
#endif

#ifdef __PSVITA__
// small buffer to hold thread/mutex/condition var names
static char name_buffer[256];

// container for function + arguments to be run in a thread
typedef struct {
  void *arg;
  ChiakiThreadFunc func;
} sce_thread_args_struct;

// tiny trampoline function to call a one-argument int-returning function
static int psp_thread_wrap(SceSize args, void *argp) {
  sce_thread_args_struct *sthread_args = (sce_thread_args_struct *)argp;
  return (int)sthread_args->func(sthread_args->arg);
}
#endif

CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_create(ChiakiThread *thread,
                                                   ChiakiThreadFunc func,
                                                   void *arg) {
#if _WIN32
  thread->func = func;
  thread->arg = arg;
  thread->ret = NULL;
  thread->thread = CreateThread(NULL, 0, win32_thread_func, thread, 0, 0);
  if (!thread->thread) return CHIAKI_ERR_THREAD;
#elif defined(__PSVITA__)
  snprintf(name_buffer, sizeof(name_buffer), "0x%08X", (unsigned int)thread);
  thread->thread_id = sceKernelCreateThread(name_buffer, psp_thread_wrap,
                                            0x10000100, 0x10000, 0, 0, NULL);
  sce_thread_args_struct sthread_args;
  sthread_args.arg = arg;
  sthread_args.func = func;
  if (sceKernelStartThread(thread->thread_id, sizeof(sthread_args),
                           &sthread_args) < 0) {
    return CHIAKI_ERR_THREAD;
  }
#else
#ifdef __SWITCH__
  if (get_thread_limit() <= 1) return CHIAKI_ERR_THREAD;
#endif
  int r = pthread_create(&thread->thread, NULL, func, arg);
  if (r != 0) return CHIAKI_ERR_THREAD;
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_join(ChiakiThread *thread,
                                                 void **retval) {
#if _WIN32
  int r = WaitForSingleObject(thread->thread, INFINITE);
  if (r != WAIT_OBJECT_0) return CHIAKI_ERR_THREAD;
  if (retval) *retval = thread->ret;
#elif defined(__PSVITA__)
  int r = sceKernelWaitThreadEnd(thread->thread_id, 0, 0);
  if (r < 0) {
    return CHIAKI_ERR_THREAD;
  }
  r = sceKernelDeleteThread(thread->thread_id);
  if (r < 0) {
    return CHIAKI_ERR_THREAD;
  }
  if (retval) {
    *retval = thread->ret;
  }
#else
  int r = pthread_join(thread->thread, retval);
  if (r != 0) return CHIAKI_ERR_THREAD;
#endif
  return CHIAKI_ERR_SUCCESS;
}

// #define CHIAKI_WINDOWS_THREAD_NAME

CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_set_name(ChiakiThread *thread,
                                                     const char *name) {
#if defined(_WIN32) && defined(CHIAKI_WINDOWS_THREAD_NAME)
  int len = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
  wchar_t *wstr = calloc(sizeof(wchar_t), len + 1);
  if (!wstr) return CHIAKI_ERR_MEMORY;
  MultiByteToWideChar(CP_UTF8, 0, name, -1, wstr, len);
  SetThreadDescription(thread->thread, wstr);
  free(wstr);
#else
#if defined(__GLIBC__) && !defined(__PSVITA__)
  int r = pthread_setname_np(thread->thread, name);
  if (r != 0) return CHIAKI_ERR_THREAD;
#else
  (void)thread;
  (void)name;
#endif
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_init(ChiakiMutex *mutex, bool rec) {
#if _WIN32
  InitializeCriticalSection(&mutex->cs);
  (void)rec;  // always recursive
#elif defined(__PSVITA__)
  snprintf(name_buffer, sizeof(name_buffer), "0x%08X", (unsigned int)mutex);
  mutex->mutex_id = sceKernelCreateMutex(
      name_buffer, rec ? SCE_KERNEL_MUTEX_ATTR_RECURSIVE : 0, 0, 0);
  if (mutex->mutex_id < 0) {
    return CHIAKI_ERR_UNKNOWN;
  }
#else
  pthread_mutexattr_t attr;
  int r = pthread_mutexattr_init(&attr);
  if (r != 0) return CHIAKI_ERR_UNKNOWN;

  pthread_mutexattr_settype(
      &attr, rec ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_DEFAULT);

  r = pthread_mutex_init(&mutex->mutex, &attr);

  pthread_mutexattr_destroy(&attr);

  if (r != 0) return CHIAKI_ERR_UNKNOWN;
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_fini(ChiakiMutex *mutex) {
#if _WIN32
  DeleteCriticalSection(&mutex->cs);
#elif defined(__PSVITA__)
  if (sceKernelDeleteMutex(mutex->mutex_id) < 0) {
    return CHIAKI_ERR_UNKNOWN;
  }
#else
  int r = pthread_mutex_destroy(&mutex->mutex);
  if (r != 0) return CHIAKI_ERR_UNKNOWN;
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_lock(ChiakiMutex *mutex) {
#if _WIN32
  EnterCriticalSection(&mutex->cs);
#elif defined(__PSVITA__)
  if (sceKernelLockMutex(mutex->mutex_id, 1, 0) < 0) {
    return CHIAKI_ERR_UNKNOWN;
  }
#else
  int r = pthread_mutex_lock(&mutex->mutex);
  if (r != 0) return CHIAKI_ERR_UNKNOWN;
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_trylock(ChiakiMutex *mutex) {
#if _WIN32
  int r = TryEnterCriticalSection(&mutex->cs);
  if (!r) return CHIAKI_ERR_IN_PROGRESS;
#elif defined(__PSVITA__)
  int r = sceKernelTryLockMutex(mutex->mutex_id, 1);
  if (r == (int)SCE_KERNEL_ERROR_MUTEX_FAILED_TO_OWN) {
    return CHIAKI_ERR_IN_PROGRESS;
  } else if (r < 0) {
    return CHIAKI_ERR_UNKNOWN;
  }
#else
  int r = pthread_mutex_trylock(&mutex->mutex);
  if (r == EBUSY)
    return CHIAKI_ERR_MUTEX_LOCKED;
  else if (r != 0)
    return CHIAKI_ERR_UNKNOWN;
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_unlock(ChiakiMutex *mutex) {
#if _WIN32
  LeaveCriticalSection(&mutex->cs);
#elif defined(__PSVITA__)
  if (sceKernelUnlockMutex(mutex->mutex_id, 1) < 0) {
    return CHIAKI_ERR_UNKNOWN;
  }
#else
  int r = pthread_mutex_unlock(&mutex->mutex);
  if (r != 0) return CHIAKI_ERR_UNKNOWN;
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_init(ChiakiCond *cond,
                                               ChiakiMutex *mutex) {
#if _WIN32
  InitializeConditionVariable(&cond->cond);
#elif defined(__PSVITA__)
  snprintf(name_buffer, sizeof(name_buffer), "0x%08X",
           ((unsigned int)cond) + 1);
  cond->cond_id = sceKernelCreateCond(name_buffer, 0, mutex->mutex_id, 0);
  if (cond->cond_id < 0) {
    return CHIAKI_ERR_UNKNOWN;
  }
#else
  pthread_condattr_t attr;
  int r = pthread_condattr_init(&attr);
  if (r != 0) return CHIAKI_ERR_UNKNOWN;
#if !__APPLE__
  r = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  if (r != 0) {
    pthread_condattr_destroy(&attr);
    return CHIAKI_ERR_UNKNOWN;
  }
#endif
  r = pthread_cond_init(&cond->cond, &attr);
  if (r != 0) {
    pthread_condattr_destroy(&attr);
    return CHIAKI_ERR_UNKNOWN;
  }
  pthread_condattr_destroy(&attr);
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_fini(ChiakiCond *cond) {
#if _WIN32
#elif defined(__PSVITA__)
  if (sceKernelDeleteCond(cond->cond_id) < 0) {
    return CHIAKI_ERR_UNKNOWN;
  }
#else
  int r = pthread_cond_destroy(&cond->cond);
  if (r != 0) return CHIAKI_ERR_UNKNOWN;
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_wait(ChiakiCond *cond,
                                               ChiakiMutex *mutex) {
#if _WIN32
  int r = SleepConditionVariableCS(&cond->cond, &mutex->cs, INFINITE);
  if (!r) return CHIAKI_ERR_THREAD;
#elif defined(__PSVITA__)
  int r = sceKernelWaitCond(cond->cond_id, 0);
  if (r < 0) {
    return CHIAKI_ERR_UNKNOWN;
  }
#else
  int r = pthread_cond_wait(&cond->cond, &mutex->mutex);
  if (r != 0) return CHIAKI_ERR_UNKNOWN;
#endif
  return CHIAKI_ERR_SUCCESS;
}

#if !__APPLE__ && !defined(_WIN32) && !defined(__PSVITA__)
static ChiakiErrorCode chiaki_cond_timedwait_abs(ChiakiCond *cond,
                                                 ChiakiMutex *mutex,
                                                 struct timespec *timeout) {
  int r = pthread_cond_timedwait(&cond->cond, &mutex->mutex, timeout);
  if (r != 0) {
    if (r == ETIMEDOUT) return CHIAKI_ERR_TIMEOUT;
    return CHIAKI_ERR_UNKNOWN;
  }
  return CHIAKI_ERR_SUCCESS;
}

static void set_timeout(struct timespec *timeout, uint64_t ms_from_now) {
#ifdef __PSVITA__
  int time_us = sceKernelGetProcessTimeWide();
  timeout->tv_sec = time_us / 1000000;
  timeout->tv_nsec = (time_us % 1000000) * 1000;
#else
  clock_gettime(CLOCK_MONOTONIC, timeout);
#endif
  timeout->tv_sec += ms_from_now / 1000;
  timeout->tv_nsec += (ms_from_now % 1000) * 1000000;
  if (timeout->tv_nsec > 1000000000) {
    timeout->tv_sec += timeout->tv_nsec / 1000000000;
    timeout->tv_nsec %= 1000000000;
  }
}
#endif

#if !__APPLE__
CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_timedjoin(ChiakiThread *thread,
                                                      void **retval,
                                                      uint64_t timeout_ms) {
#if _WIN32
  int r = WaitForSingleObject(thread->thread, timeout_ms);
  if (r != WAIT_OBJECT_0) return CHIAKI_ERR_THREAD;
  if (retval) *retval = thread->ret;
#elif defined(__PSVITA__)
  // FIXME ywnico: check implementation
  // Note: sceKernelWaitThreadEnd takes timeout in microseconds, not
  // milliseconds
  thread->timeout_us = timeout_ms * 1000;
  int r = sceKernelWaitThreadEnd(thread->thread_id, 0, &thread->timeout_us);
  if (r < 0) {
    return CHIAKI_ERR_THREAD;
  }
  r = sceKernelDeleteThread(thread->thread_id);
  if (r < 0) {
    return CHIAKI_ERR_THREAD;
  }
  if (retval) {
    *retval = thread->ret;
  }
#else
  struct timespec timeout;
  set_timeout(&timeout, timeout_ms);
  int r =
      pthread_clockjoin_np(thread->thread, retval, CLOCK_MONOTONIC, &timeout);
  if (r != 0) {
    if (r == ETIMEDOUT) return CHIAKI_ERR_TIMEOUT;
    return CHIAKI_ERR_THREAD;
  }
#endif
  return CHIAKI_ERR_SUCCESS;
}
#endif

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_timedwait(ChiakiCond *cond,
                                                    ChiakiMutex *mutex,
                                                    uint64_t timeout_ms) {
#if _WIN32
  int r = SleepConditionVariableCS(&cond->cond, &mutex->cs, (DWORD)timeout_ms);
  if (!r) {
    if (GetLastError() == ERROR_TIMEOUT) return CHIAKI_ERR_TIMEOUT;
    return CHIAKI_ERR_THREAD;
  }
  return CHIAKI_ERR_SUCCESS;
#elif defined(__PSVITA__)
  cond->timeout_us = timeout_ms * 1000;
  int r = sceKernelWaitCond(cond->cond_id, &cond->timeout_us);
  if (r == (int)SCE_KERNEL_ERROR_WAIT_TIMEOUT) {
    return CHIAKI_ERR_TIMEOUT;
  } else if (r < 0) {
    return CHIAKI_ERR_UNKNOWN;
  }
  return CHIAKI_ERR_SUCCESS;
#else
  struct timespec timeout;
#if __APPLE__
  timeout.tv_sec = (__darwin_time_t)(timeout_ms / 1000);
  timeout.tv_nsec = (long)((timeout_ms % 1000) * 1000000);
  int r =
      pthread_cond_timedwait_relative_np(&cond->cond, &mutex->mutex, &timeout);
  if (r != 0) {
    if (r == ETIMEDOUT) return CHIAKI_ERR_TIMEOUT;
    return CHIAKI_ERR_UNKNOWN;
  }
  return CHIAKI_ERR_SUCCESS;
#else
  set_timeout(&timeout, timeout_ms);
  return chiaki_cond_timedwait_abs(cond, mutex, &timeout);
#endif
#endif
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_wait_pred(ChiakiCond *cond,
                                                    ChiakiMutex *mutex,
                                                    ChiakiCheckPred check_pred,
                                                    void *check_pred_user) {
  while (!check_pred(check_pred_user)) {
    ChiakiErrorCode err = chiaki_cond_wait(cond, mutex);
    if (err != CHIAKI_ERR_SUCCESS) return err;
  }
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_timedwait_pred(
    ChiakiCond *cond, ChiakiMutex *mutex, uint64_t timeout_ms,
    ChiakiCheckPred check_pred, void *check_pred_user) {
#if __APPLE__ || defined(_WIN32) || defined(__PSVITA__)
  uint64_t start_time = chiaki_time_now_monotonic_ms();
  uint64_t elapsed = 0;
#else
  struct timespec timeout;
  set_timeout(&timeout, timeout_ms);
#endif
  while (!check_pred(check_pred_user)) {
#if __APPLE__ || defined(_WIN32) || defined(__PSVITA__)
    ChiakiErrorCode err =
        chiaki_cond_timedwait(cond, mutex, timeout_ms - elapsed);
#else
    ChiakiErrorCode err = chiaki_cond_timedwait_abs(cond, mutex, &timeout);
#endif
    if (err != CHIAKI_ERR_SUCCESS) return err;
#if __APPLE__ || defined(_WIN32) || defined(__PSVITA__)
    elapsed = chiaki_time_now_monotonic_ms() - start_time;
    if (elapsed >= timeout_ms) return CHIAKI_ERR_TIMEOUT;
#endif
  }
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_signal(ChiakiCond *cond) {
#if _WIN32
  WakeConditionVariable(&cond->cond);
#elif defined(__PSVITA__)
  int r = sceKernelSignalCond(cond->cond_id);
  if (r < 0) {
    return CHIAKI_ERR_UNKNOWN;
  }
#else
  int r = pthread_cond_signal(&cond->cond);
  if (r != 0) return CHIAKI_ERR_UNKNOWN;
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_broadcast(ChiakiCond *cond) {
#if _WIN32
  WakeAllConditionVariable(&cond->cond);
#elif defined(__PSVITA__)
  int r = sceKernelSignalCondAll(cond->cond_id);
  if (r < 0) {
    return CHIAKI_ERR_SUCCESS;
  }
#else
  int r = pthread_cond_broadcast(&cond->cond);
  if (r != 0) return CHIAKI_ERR_UNKNOWN;
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_init(ChiakiBoolPredCond *cond) {
  cond->pred = false;

  ChiakiErrorCode err = chiaki_mutex_init(&cond->mutex, false);
  if (err != CHIAKI_ERR_SUCCESS) return err;

  err = chiaki_cond_init(&cond->cond, &cond->mutex);
  if (err != CHIAKI_ERR_SUCCESS) {
    chiaki_mutex_fini(&cond->mutex);
    return err;
  }

  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_fini(ChiakiBoolPredCond *cond) {
  ChiakiErrorCode err = chiaki_cond_fini(&cond->cond);
  if (err != CHIAKI_ERR_SUCCESS) return err;

  err = chiaki_mutex_fini(&cond->mutex);
  if (err != CHIAKI_ERR_SUCCESS) return err;

  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_lock(ChiakiBoolPredCond *cond) {
  return chiaki_mutex_lock(&cond->mutex);
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_unlock(ChiakiBoolPredCond *cond) {
  return chiaki_mutex_unlock(&cond->mutex);
}

bool bool_pred_cond_check_pred(void *user) {
  ChiakiBoolPredCond *bool_pred_cond = user;
  return bool_pred_cond->pred;
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_wait(ChiakiBoolPredCond *cond) {
  return chiaki_cond_wait_pred(&cond->cond, &cond->mutex,
                               bool_pred_cond_check_pred, cond);
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_timedwait(ChiakiBoolPredCond *cond, uint64_t timeout_ms) {
  return chiaki_cond_timedwait_pred(&cond->cond, &cond->mutex, timeout_ms,
                                    bool_pred_cond_check_pred, cond);
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_signal(ChiakiBoolPredCond *cond) {
  ChiakiErrorCode err = chiaki_bool_pred_cond_lock(cond);
  if (err != CHIAKI_ERR_SUCCESS) return err;

  cond->pred = true;

  err = chiaki_bool_pred_cond_unlock(cond);
  if (err != CHIAKI_ERR_SUCCESS) return err;

  return chiaki_cond_signal(&cond->cond);
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_bool_pred_cond_broadcast(ChiakiBoolPredCond *cond) {
  ChiakiErrorCode err = chiaki_bool_pred_cond_lock(cond);
  if (err != CHIAKI_ERR_SUCCESS) return err;

  cond->pred = true;

  err = chiaki_bool_pred_cond_unlock(cond);
  if (err != CHIAKI_ERR_SUCCESS) return err;

  return chiaki_cond_broadcast(&cond->cond);
}
