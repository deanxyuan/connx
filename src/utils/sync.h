/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_SYNC_H_
#define CONNX_SRC_UTILS_SYNC_H_

#ifdef _WIN32
#    include <windows.h>
#    include <stdint.h>

typedef CRITICAL_SECTION connx_mutex_t;
typedef CONDITION_VARIABLE connx_condvar_t;
typedef INIT_ONCE connx_once_t;
typedef SRWLOCK connx_rwlock_t;

#    define CONNX_ONCE_INIT INIT_ONCE_STATIC_INIT

// mutex
#    define ConnxMutexInit(m)    InitializeCriticalSection(m)
#    define ConnxMutexLock(m)    EnterCriticalSection(m)
#    define ConnxMutexUnlock(m)  LeaveCriticalSection(m)
#    define ConnxMutexDestroy(m) DeleteCriticalSection(m)

// condvar
#    define ConnxCondVarInit(cv)      InitializeConditionVariable(cv)
#    define ConnxCondVarDestroy(cv)   (cv)
#    define ConnxCondVarSignal(cv)    WakeConditionVariable(cv)
#    define ConnxCondVarBroadcast(cv) WakeAllConditionVariable(cv)

#else
#    include <errno.h>
#    include <pthread.h>
#    include <sys/time.h>
#    include <stdint.h>

typedef pthread_mutex_t connx_mutex_t;
typedef pthread_cond_t connx_condvar_t;
typedef pthread_once_t connx_once_t;
typedef pthread_rwlock_t connx_rwlock_t;

#    define CONNX_ONCE_INIT PTHREAD_ONCE_INIT

#    define ConnxMutexInit(m)         pthread_mutex_init(m, NULL)
#    define ConnxMutexLock(m)         pthread_mutex_lock(m)
#    define ConnxMutexUnlock(m)       pthread_mutex_unlock(m)
#    define ConnxMutexDestroy(m)      pthread_mutex_destroy(m)

// condvar
void ConnxCondVarInit(connx_condvar_t* cv);
#    define ConnxCondVarDestroy(cv)   pthread_cond_destroy(cv)
#    define ConnxCondVarSignal(cv)    pthread_cond_signal(cv)
#    define ConnxCondVarBroadcast(cv) pthread_cond_broadcast(cv)

#endif

// Return 1 in timeout, 0 in other cases
int ConnxCondVarWait(connx_condvar_t* cv, connx_mutex_t* mutex, int64_t timeout_ms);
void ConnxOnceInit(connx_once_t* once, void (*init_function)(void));

namespace connx {

class Mutex final {
public:
    Mutex() { ConnxMutexInit(&mtx_); }
    ~Mutex() { ConnxMutexDestroy(&mtx_); }

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void Lock() { ConnxMutexLock(&mtx_); }
    void Unlock() { ConnxMutexUnlock(&mtx_); }

    operator connx_mutex_t*() { return &mtx_; }

private:
    connx_mutex_t mtx_;
};

class MutexLock final {
public:
    explicit MutexLock(Mutex* m)
        : mtx_(*m) {
        ConnxMutexLock(mtx_);
    }
    explicit MutexLock(connx_mutex_t* m)
        : mtx_(m) {
        ConnxMutexLock(mtx_);
    }
    ~MutexLock() { ConnxMutexUnlock(mtx_); }

    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;

private:
    connx_mutex_t* mtx_;
};

class ConditionVariable final {
public:
    ConditionVariable() { ConnxCondVarInit(&cv_); }
    ~ConditionVariable() { ConnxCondVarDestroy(&cv_); }

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    void Signal() { ConnxCondVarSignal(&cv_); }
    void Broadcast() { ConnxCondVarBroadcast(&cv_); }

    int Wait(Mutex* m) { return ConnxCondVarWait(&cv_, *m, -1); }

    // return 1 means timeout
    int Wait(Mutex* m, int64_t timeout_ms) { return ConnxCondVarWait(&cv_, *m, timeout_ms); }

    operator connx_condvar_t*() { return &cv_; }

private:
    connx_condvar_t cv_;
};

} // namespace connx

#endif // CONNX_SRC_UTILS_SYNC_H_
