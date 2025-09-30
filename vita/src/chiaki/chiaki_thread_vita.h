#ifndef CHIAKI_THREAD_VITA_H
#define CHIAKI_THREAD_VITA_H

#include "chiaki_common.h"  // For ChiakiErrorCode

#ifdef __PSVITA__
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#else
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#endif

// Thread function type (matches vitaki-fork implementation)
typedef void *(*ChiakiThreadFunc)(void *);

// Thread structure (vitaki-fork compatible)
typedef struct chiaki_thread_t {
#ifdef __PSVITA__
  SceUID thread_id;
  void *ret;
  SceUInt timeout_us;
#else
  pthread_t thread;
  void *ret;
#endif
} ChiakiThread;

// Mutex structure
typedef struct chiaki_mutex_t {
#ifdef __PSVITA__
  SceUID mutex_id;
  bool recursive;
  char name[32];
#else
  pthread_mutex_t mutex;
#endif
} ChiakiMutex;

// Condition variable structure
typedef struct chiaki_cond_t {
#ifdef __PSVITA__
  SceUID cond_id;
  ChiakiMutex *mutex;
  char name[32];
#else
  pthread_cond_t cond;
#endif
} ChiakiCond;

// Semaphore structure
typedef struct {
#ifdef __PSVITA__
  SceUID sema_id;
  char name[32];
#else
  sem_t *sem;
  char name[32];
#endif
} ChiakiSem;

// Stop pipe is defined in vita_stop_pipe.h

// Thread functions (vitaki-fork compatible signatures)
ChiakiErrorCode chiaki_thread_create(ChiakiThread *thread,
                                     ChiakiThreadFunc func, void *arg);
ChiakiErrorCode chiaki_thread_join(ChiakiThread *thread, void **retval);
void chiaki_thread_set_name(ChiakiThread *thread, const char *name);

// Mutex functions
ChiakiErrorCode chiaki_mutex_init(ChiakiMutex *mutex, bool recursive);
void chiaki_mutex_fini(ChiakiMutex *mutex);
ChiakiErrorCode chiaki_mutex_lock(ChiakiMutex *mutex);
ChiakiErrorCode chiaki_mutex_trylock(ChiakiMutex *mutex);
ChiakiErrorCode chiaki_mutex_unlock(ChiakiMutex *mutex);

// Condition variable functions
ChiakiErrorCode chiaki_cond_init(ChiakiCond *cond, ChiakiMutex *mutex);
void chiaki_cond_fini(ChiakiCond *cond);
ChiakiErrorCode chiaki_cond_signal(ChiakiCond *cond);
ChiakiErrorCode chiaki_cond_broadcast(ChiakiCond *cond);
ChiakiErrorCode chiaki_cond_wait(ChiakiCond *cond, ChiakiMutex *mutex);
ChiakiErrorCode chiaki_cond_timedwait(ChiakiCond *cond, ChiakiMutex *mutex,
                                      uint64_t timeout_ms);

// Predicate function type for conditional waits
typedef bool (*ChiakiCheckPred)(void *user);

// Wait on condition with predicate check
ChiakiErrorCode chiaki_cond_timedwait_pred(ChiakiCond *cond, ChiakiMutex *mutex,
                                           uint64_t timeout_ms,
                                           ChiakiCheckPred check_pred,
                                           void *check_pred_user);

// Semaphore functions
ChiakiErrorCode chiaki_sem_init(ChiakiSem *sem, int32_t initial_count,
                                const char *name);
void chiaki_sem_fini(ChiakiSem *sem);
ChiakiErrorCode chiaki_sem_wait(ChiakiSem *sem);
ChiakiErrorCode chiaki_sem_trywait(ChiakiSem *sem);
ChiakiErrorCode chiaki_sem_timedwait(ChiakiSem *sem, uint64_t timeout_ms);
ChiakiErrorCode chiaki_sem_post(ChiakiSem *sem);

// Sleep function
void chiaki_thread_sleep_ms(uint64_t ms);

// Get current time in milliseconds
uint64_t chiaki_thread_get_time_ms(void);

#endif  // CHIAKI_THREAD_VITA_H