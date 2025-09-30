#include "chiaki_thread_vita.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include VitaRPS5 logging system
#include "../utils/logger.h"

// COMPILATION VERIFICATION - This should always be PS Vita for VitaRPS5
#ifndef __PSVITA__
#error \
    "CRITICAL ERROR: __PSVITA__ not defined! Wrong toolchain or compilation environment!"
#endif

// Additional PS Vita macro verification
#ifndef VITA
#error \
    "CRITICAL ERROR: VITA macro not defined! VitaSDK not properly configured!"
#endif

// Force compilation-time verification
static int __attribute__((unused)) compilation_check_vita = 1;

#ifdef __PSVITA__

// PS Vita thread implementation compilation check
static int __attribute__((constructor)) vita_thread_init_check(void) {
  log_info(
      "[VITA_THREAD] PS Vita threading implementation ACTIVE (compiled with "
      "__PSVITA__)");
  return 0;
}

// Thread parameter structure for PS Vita
typedef struct {
  void *arg;
  ChiakiThreadFunc func;
} sce_thread_args_struct;

// Thread wrapper for PS Vita (vitaki-fork proven pattern)
static int psp_thread_wrap(SceSize args, void *argp) {
  log_info("===========================================");
  log_info("[WRAPPER] psp_thread_wrap() CALLED BY PS VITA KERNEL!");
  log_info("[WRAPPER] args=%d, argp=%p", args, argp);
  log_info("===========================================");

  sce_thread_args_struct *sthread_args = (sce_thread_args_struct *)argp;

  log_info("[WRAPPER] Calling thread function %p with arg %p",
           sthread_args->func, sthread_args->arg);
  int result = (int)sthread_args->func(sthread_args->arg);
  log_info("[WRAPPER] Thread function returned: %d", result);

  return result;
}

ChiakiErrorCode chiaki_thread_create(ChiakiThread *thread,
                                     ChiakiThreadFunc func, void *arg) {
  log_info("==========================================");
  log_info("[VITA_THREAD] *** PS VITA IMPLEMENTATION ACTIVE ***");
  log_info(
      "[VITA_THREAD] chiaki_thread_create called with thread=%p, func=%p, "
      "arg=%p",
      thread, func, arg);
  log_info("[VITA_THREAD] Using VITAKI-FORK proven pattern (stack-based args)");
  log_info("==========================================");

  if (!thread || !func) {
    log_error("[VITA_THREAD] ERROR: Invalid parameters - thread=%p, func=%p",
              thread, func);
    return CHIAKI_ERR_INVALID_DATA;
  }

  thread->ret = NULL;
  thread->timeout_us = 0;

  // Generate thread name from thread pointer (like vitaki-fork)
  char name_buffer[32];
  snprintf(name_buffer, sizeof(name_buffer), "0x%08X", (unsigned int)thread);
  log_info("[VITA_THREAD] Generated thread name: %s", name_buffer);

  // Create thread with larger stack for session threads
  log_info("[VITA_THREAD] Calling sceKernelCreateThread...");
  thread->thread_id = sceKernelCreateThread(
      name_buffer, psp_thread_wrap,
      0x10000100,  // Default priority
      0x40000,     // 256KB stack (4x larger for session thread safety)
      0, 0, NULL);

  log_info("[VITA_THREAD] sceKernelCreateThread result: thread_id=0x%08X",
           thread->thread_id);
  if (thread->thread_id < 0) {
    log_error(
        "[VITA_THREAD] ERROR: sceKernelCreateThread failed with error=0x%08X",
        thread->thread_id);
    return CHIAKI_ERR_THREAD;
  }

  // VITAKI-FORK PATTERN: Pass arguments by value on stack
  sce_thread_args_struct sthread_args;
  sthread_args.arg = arg;
  sthread_args.func = func;

  log_info("[VITA_THREAD] Thread args on stack: &sthread_args=%p",
           &sthread_args);
  log_info("[VITA_THREAD] sthread_args.func = %p", sthread_args.func);
  log_info("[VITA_THREAD] sthread_args.arg = %p", sthread_args.arg);

  log_info("[VITA_THREAD] Calling sceKernelStartThread with args_size=%zu...",
           sizeof(sthread_args));
  int ret = sceKernelStartThread(thread->thread_id, sizeof(sthread_args),
                                 &sthread_args);

  log_info("[VITA_THREAD] sceKernelStartThread result: ret=0x%08X", ret);
  if (ret < 0) {
    log_error(
        "[VITA_THREAD] ERROR: sceKernelStartThread failed with error=0x%08X",
        ret);
    sceKernelDeleteThread(thread->thread_id);
    return CHIAKI_ERR_THREAD;
  }

  log_info(
      "[VITA_THREAD] SUCCESS: Thread created and started successfully "
      "(ID=0x%08X)",
      thread->thread_id);
  log_info(
      "[VITA_THREAD] NOTE: Using vitaki-fork proven stack-based argument "
      "pattern");
  return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_thread_join(ChiakiThread *thread, void **retval) {
  if (!thread) return CHIAKI_ERR_INVALID_DATA;

  int ret = sceKernelWaitThreadEnd(thread->thread_id, NULL, NULL);
  if (ret < 0) return CHIAKI_ERR_THREAD;

  if (retval) *retval = thread->ret;

  sceKernelDeleteThread(thread->thread_id);
  return CHIAKI_ERR_SUCCESS;
}

void chiaki_thread_set_name(ChiakiThread *thread, const char *name) {
  // PS Vita doesn't support changing thread name after creation
  // This function exists for compatibility with vitaki-fork API
}

ChiakiErrorCode chiaki_mutex_init(ChiakiMutex *mutex, bool recursive) {
  if (!mutex) return CHIAKI_ERR_INVALID_DATA;

  snprintf(mutex->name, sizeof(mutex->name), "ChiakiMutex_%p", mutex);
  mutex->recursive = recursive;

  // Create mutex with default attributes (compatible with VitaRPS5)
  SceUInt attr = 0;  // Use default attributes like existing VitaRPS5 code
  // Note: PS Vita SDK recursive mutexes may not be available

  mutex->mutex_id = sceKernelCreateMutex(mutex->name, attr, 0, NULL);
  if (mutex->mutex_id < 0) return CHIAKI_ERR_THREAD;

  return CHIAKI_ERR_SUCCESS;
}

void chiaki_mutex_fini(ChiakiMutex *mutex) {
  if (!mutex || mutex->mutex_id < 0) return;

  sceKernelDeleteMutex(mutex->mutex_id);
  mutex->mutex_id = -1;
}

ChiakiErrorCode chiaki_mutex_lock(ChiakiMutex *mutex) {
  if (!mutex || mutex->mutex_id < 0) return CHIAKI_ERR_INVALID_DATA;

  int ret = sceKernelLockMutex(mutex->mutex_id, 1, NULL);
  return (ret < 0) ? CHIAKI_ERR_THREAD : CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_mutex_trylock(ChiakiMutex *mutex) {
  if (!mutex || mutex->mutex_id < 0) return CHIAKI_ERR_INVALID_DATA;

  int ret = sceKernelTryLockMutex(mutex->mutex_id, 1);
  if (ret == 0x80020068)  // SCE_KERNEL_ERROR_MUTEX_LOCKED equivalent
    return CHIAKI_ERR_IN_PROGRESS;

  return (ret < 0) ? CHIAKI_ERR_THREAD : CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_mutex_unlock(ChiakiMutex *mutex) {
  if (!mutex || mutex->mutex_id < 0) return CHIAKI_ERR_INVALID_DATA;

  int ret = sceKernelUnlockMutex(mutex->mutex_id, 1);
  return (ret < 0) ? CHIAKI_ERR_THREAD : CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_cond_init(ChiakiCond *cond, ChiakiMutex *mutex) {
  if (!cond) return CHIAKI_ERR_INVALID_DATA;

  cond->mutex = mutex;
  snprintf(cond->name, sizeof(cond->name), "ChiakiCond_%p", cond);

  cond->cond_id = sceKernelCreateCond(cond->name,
                                      0,  // Use default attributes
                                      mutex ? mutex->mutex_id : -1, NULL);

  if (cond->cond_id < 0) return CHIAKI_ERR_THREAD;

  return CHIAKI_ERR_SUCCESS;
}

void chiaki_cond_fini(ChiakiCond *cond) {
  if (!cond || cond->cond_id < 0) return;

  sceKernelDeleteCond(cond->cond_id);
  cond->cond_id = -1;
}

ChiakiErrorCode chiaki_cond_signal(ChiakiCond *cond) {
  if (!cond || cond->cond_id < 0) return CHIAKI_ERR_INVALID_DATA;

  int ret = sceKernelSignalCond(cond->cond_id);
  return (ret < 0) ? CHIAKI_ERR_THREAD : CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_cond_broadcast(ChiakiCond *cond) {
  if (!cond || cond->cond_id < 0) return CHIAKI_ERR_INVALID_DATA;

  int ret = sceKernelSignalCondAll(cond->cond_id);
  return (ret < 0) ? CHIAKI_ERR_THREAD : CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_cond_wait(ChiakiCond *cond, ChiakiMutex *mutex) {
  if (!cond || cond->cond_id < 0 || !mutex || mutex->mutex_id < 0)
    return CHIAKI_ERR_INVALID_DATA;

  int ret = sceKernelWaitCond(cond->cond_id, NULL);
  return (ret < 0) ? CHIAKI_ERR_THREAD : CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_cond_timedwait(ChiakiCond *cond, ChiakiMutex *mutex,
                                      uint64_t timeout_ms) {
  if (!cond || cond->cond_id < 0 || !mutex || mutex->mutex_id < 0)
    return CHIAKI_ERR_INVALID_DATA;

  SceUInt timeout = timeout_ms * 1000;  // Convert to microseconds
  int ret = sceKernelWaitCond(cond->cond_id, &timeout);

  if (ret == 0x800201A8)  // SCE_KERNEL_ERROR_WAIT_TIMEOUT equivalent
    return CHIAKI_ERR_TIMEOUT;

  return (ret < 0) ? CHIAKI_ERR_THREAD : CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_sem_init(ChiakiSem *sem, int32_t initial_count,
                                const char *name) {
  if (!sem) return CHIAKI_ERR_INVALID_DATA;

  snprintf(sem->name, sizeof(sem->name), "%s", name ? name : "ChiakiSem");

  sem->sema_id = sceKernelCreateSema(sem->name,
                                     0,  // Use default attributes
                                     initial_count, INT32_MAX, NULL);

  if (sem->sema_id < 0) return CHIAKI_ERR_THREAD;

  return CHIAKI_ERR_SUCCESS;
}

void chiaki_sem_fini(ChiakiSem *sem) {
  if (!sem || sem->sema_id < 0) return;

  sceKernelDeleteSema(sem->sema_id);
  sem->sema_id = -1;
}

ChiakiErrorCode chiaki_sem_wait(ChiakiSem *sem) {
  if (!sem || sem->sema_id < 0) return CHIAKI_ERR_INVALID_DATA;

  int ret = sceKernelWaitSema(sem->sema_id, 1, NULL);
  return (ret < 0) ? CHIAKI_ERR_THREAD : CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_sem_trywait(ChiakiSem *sem) {
  if (!sem || sem->sema_id < 0) return CHIAKI_ERR_INVALID_DATA;

  SceUInt timeout = 0;
  int ret = sceKernelWaitSema(sem->sema_id, 1, &timeout);

  if (ret == 0x800201A8)  // SCE_KERNEL_ERROR_WAIT_TIMEOUT equivalent
    return CHIAKI_ERR_IN_PROGRESS;

  return (ret < 0) ? CHIAKI_ERR_THREAD : CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_sem_timedwait(ChiakiSem *sem, uint64_t timeout_ms) {
  if (!sem || sem->sema_id < 0) return CHIAKI_ERR_INVALID_DATA;

  SceUInt timeout = timeout_ms * 1000;  // Convert to microseconds
  int ret = sceKernelWaitSema(sem->sema_id, 1, &timeout);

  if (ret == 0x800201A8)  // SCE_KERNEL_ERROR_WAIT_TIMEOUT equivalent
    return CHIAKI_ERR_TIMEOUT;

  return (ret < 0) ? CHIAKI_ERR_THREAD : CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_sem_post(ChiakiSem *sem) {
  if (!sem || sem->sema_id < 0) return CHIAKI_ERR_INVALID_DATA;

  int ret = sceKernelSignalSema(sem->sema_id, 1);
  return (ret < 0) ? CHIAKI_ERR_THREAD : CHIAKI_ERR_SUCCESS;
}

void chiaki_thread_sleep_ms(uint64_t ms) { sceKernelDelayThread(ms * 1000); }

uint64_t chiaki_thread_get_time_ms(void) {
  return sceKernelGetProcessTimeWide() / 1000;
}

#else  // Non-Vita implementation - DISABLED FOR VITARPS5

#error \
    "CRITICAL ERROR: Non-Vita threading implementation should NEVER be compiled for VitaRPS5!"

// This code should never execute for PS Vita builds
ChiakiErrorCode chiaki_thread_create(ChiakiThread *thread,
                                     void *(*func)(void *), void *arg,
                                     const char *name) {
  if (!thread || !func) return CHIAKI_ERR_INVALID_DATA;

  thread->func = func;
  thread->arg = arg;

  int ret = pthread_create(&thread->thread, NULL, func, arg);

  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

ChiakiErrorCode chiaki_thread_join(ChiakiThread *thread, void **retval) {
  if (!thread) return CHIAKI_ERR_INVALID_DATA;

  int ret = pthread_join(thread->thread, retval);
  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

void chiaki_thread_set_name(const char *name) {
#ifdef __linux__
  pthread_setname_np(pthread_self(), name);
#endif
}

ChiakiErrorCode chiaki_mutex_init(ChiakiMutex *mutex, bool recursive) {
  if (!mutex) return CHIAKI_ERR_INVALID_DATA;

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);

  if (recursive) pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

  int ret = pthread_mutex_init(&mutex->mutex, &attr);
  pthread_mutexattr_destroy(&attr);

  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

void chiaki_mutex_fini(ChiakiMutex *mutex) {
  if (!mutex) return;

  pthread_mutex_destroy(&mutex->mutex);
}

ChiakiErrorCode chiaki_mutex_lock(ChiakiMutex *mutex) {
  if (!mutex) return CHIAKI_ERR_INVALID_DATA;

  int ret = pthread_mutex_lock(&mutex->mutex);
  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

ChiakiErrorCode chiaki_mutex_trylock(ChiakiMutex *mutex) {
  if (!mutex) return CHIAKI_ERR_INVALID_DATA;

  int ret = pthread_mutex_trylock(&mutex->mutex);
  if (ret == EBUSY) return CHIAKI_ERR_IN_PROGRESS;

  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

ChiakiErrorCode chiaki_mutex_unlock(ChiakiMutex *mutex) {
  if (!mutex) return CHIAKI_ERR_INVALID_DATA;

  int ret = pthread_mutex_unlock(&mutex->mutex);
  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

ChiakiErrorCode chiaki_cond_init(ChiakiCond *cond, ChiakiMutex *mutex) {
  if (!cond) return CHIAKI_ERR_INVALID_DATA;

  int ret = pthread_cond_init(&cond->cond, NULL);
  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

void chiaki_cond_fini(ChiakiCond *cond) {
  if (!cond) return;

  pthread_cond_destroy(&cond->cond);
}

ChiakiErrorCode chiaki_cond_signal(ChiakiCond *cond) {
  if (!cond) return CHIAKI_ERR_INVALID_DATA;

  int ret = pthread_cond_signal(&cond->cond);
  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

ChiakiErrorCode chiaki_cond_broadcast(ChiakiCond *cond) {
  if (!cond) return CHIAKI_ERR_INVALID_DATA;

  int ret = pthread_cond_broadcast(&cond->cond);
  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

ChiakiErrorCode chiaki_cond_wait(ChiakiCond *cond, ChiakiMutex *mutex) {
  if (!cond || !mutex) return CHIAKI_ERR_INVALID_DATA;

  int ret = pthread_cond_wait(&cond->cond, &mutex->mutex);
  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

ChiakiErrorCode chiaki_cond_timedwait(ChiakiCond *cond, ChiakiMutex *mutex,
                                      uint64_t timeout_ms) {
  if (!cond || !mutex) return CHIAKI_ERR_INVALID_DATA;

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += timeout_ms / 1000;
  ts.tv_nsec += (timeout_ms % 1000) * 1000000;
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000;
  }

  int ret = pthread_cond_timedwait(&cond->cond, &mutex->mutex, &ts);
  if (ret == ETIMEDOUT) return CHIAKI_ERR_TIMEOUT;

  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

ChiakiErrorCode chiaki_sem_init(ChiakiSem *sem, int32_t initial_count,
                                const char *name) {
  if (!sem) return CHIAKI_ERR_INVALID_DATA;

  snprintf(sem->name, sizeof(sem->name), "%s", name ? name : "ChiakiSem");

#ifdef __APPLE__
  // macOS doesn't support unnamed semaphores
  sem->sem = sem_open(sem->name, O_CREAT | O_EXCL, 0644, initial_count);
  if (sem->sem == SEM_FAILED) {
    // Try without O_EXCL in case it already exists
    sem->sem = sem_open(sem->name, O_CREAT, 0644, initial_count);
    if (sem->sem == SEM_FAILED) return CHIAKI_ERR_THREAD;
  }
#else
  sem->sem = malloc(sizeof(sem_t));
  if (!sem->sem) return CHIAKI_ERR_MEMORY;

  if (sem_init(sem->sem, 0, initial_count) != 0) {
    free(sem->sem);
    return CHIAKI_ERR_THREAD;
  }
#endif

  return CHIAKI_ERR_SUCCESS;
}

void chiaki_sem_fini(ChiakiSem *sem) {
  if (!sem) return;

#ifdef __APPLE__
  if (sem->sem != SEM_FAILED) {
    sem_close(sem->sem);
    sem_unlink(sem->name);
  }
#else
  if (sem->sem) {
    sem_destroy(sem->sem);
    free(sem->sem);
  }
#endif
}

ChiakiErrorCode chiaki_sem_wait(ChiakiSem *sem) {
  if (!sem || !sem->sem) return CHIAKI_ERR_INVALID_DATA;

  int ret = sem_wait(sem->sem);
  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

ChiakiErrorCode chiaki_sem_trywait(ChiakiSem *sem) {
  if (!sem || !sem->sem) return CHIAKI_ERR_INVALID_DATA;

  int ret = sem_trywait(sem->sem);
  if (ret != 0 && errno == EAGAIN) return CHIAKI_ERR_IN_PROGRESS;

  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

ChiakiErrorCode chiaki_sem_timedwait(ChiakiSem *sem, uint64_t timeout_ms) {
  if (!sem || !sem->sem) return CHIAKI_ERR_INVALID_DATA;

#ifdef __APPLE__
  // macOS doesn't support sem_timedwait, use a loop with trywait
  uint64_t start = chiaki_thread_get_time_ms();
  while (chiaki_thread_get_time_ms() - start < timeout_ms) {
    if (sem_trywait(sem->sem) == 0) return CHIAKI_ERR_SUCCESS;
    usleep(1000);  // Sleep 1ms
  }
  return CHIAKI_ERR_TIMEOUT;
#else
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += timeout_ms / 1000;
  ts.tv_nsec += (timeout_ms % 1000) * 1000000;
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000;
  }

  int ret = sem_timedwait(sem->sem, &ts);
  if (ret != 0 && errno == ETIMEDOUT) return CHIAKI_ERR_TIMEOUT;

  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
#endif
}

ChiakiErrorCode chiaki_sem_post(ChiakiSem *sem) {
  if (!sem || !sem->sem) return CHIAKI_ERR_INVALID_DATA;

  int ret = sem_post(sem->sem);
  return (ret == 0) ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_THREAD;
}

void chiaki_thread_sleep_ms(uint64_t ms) { usleep(ms * 1000); }

uint64_t chiaki_thread_get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

#endif  // __PSVITA__

// Wait on condition with predicate check and timeout
// This function works on all platforms
ChiakiErrorCode chiaki_cond_timedwait_pred(ChiakiCond *cond, ChiakiMutex *mutex,
                                           uint64_t timeout_ms,
                                           ChiakiCheckPred check_pred,
                                           void *check_pred_user) {
  if (!cond || !mutex || !check_pred) return CHIAKI_ERR_INVALID_DATA;

  uint64_t start_time = chiaki_thread_get_time_ms();
  uint64_t elapsed = 0;

  while (!check_pred(check_pred_user)) {
    ChiakiErrorCode err =
        chiaki_cond_timedwait(cond, mutex, timeout_ms - elapsed);
    if (err != CHIAKI_ERR_SUCCESS) return err;

    elapsed = chiaki_thread_get_time_ms() - start_time;
    if (elapsed >= timeout_ms) return CHIAKI_ERR_TIMEOUT;
  }

  return CHIAKI_ERR_SUCCESS;
}