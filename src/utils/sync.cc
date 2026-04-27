/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/utils/sync.h"
#include "src/utils/useful.h"

#ifdef _WIN32
int ConnxCondVarWait(connx_condvar_t* cv, connx_mutex_t* mutex, int64_t timeout_ms) {
    if (timeout_ms < 0) {
        SleepConditionVariableCS(cv, mutex, INFINITE);
        return 0;
    }

    if (SleepConditionVariableCS(cv, mutex, static_cast<DWORD>(timeout_ms)) == 0 &&
        GetLastError() == ERROR_TIMEOUT) {
        return 1;
    }
    return 0;
}

static void* dont_use_it = NULL;
struct connx_init_once_parameter {
    void (*init_function)(void);
};

static BOOL CALLBACK ConnxInitOnceCallback(connx_once_t*, void* Parameter, void**) {
    struct connx_init_once_parameter* p = (struct connx_init_once_parameter*)Parameter;
    p->init_function();
    return TRUE;
}

void ConnxOnceInit(connx_once_t* once, void (*init_function)(void)) {
    struct connx_init_once_parameter parameter;
    parameter.init_function = init_function;
    InitOnceExecuteOnce(once, ConnxInitOnceCallback, &parameter, &dont_use_it);
}

#else
void ConnxCondVarInit(connx_condvar_t* cv) {
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_cond_init(cv, &attr);
}

int ConnxCondVarWait(connx_condvar_t* cv, connx_mutex_t* mutex, int64_t timeout_ms) {
    int error = 0;
    if (timeout_ms < 0) {
        error = pthread_cond_wait(cv, mutex);
    } else {
        struct timeval now;
        gettimeofday(&now, nullptr);
        now.tv_sec += static_cast<time_t>(timeout_ms / 1000);
        now.tv_usec += static_cast<long>((timeout_ms % 1000) * 1000);
        long sec = now.tv_usec / 1000000;
        if (sec > 0) {
            now.tv_sec += sec;
            now.tv_usec -= sec * 1000000;
        }
        struct timespec abstime;
        abstime.tv_sec = now.tv_sec;
        abstime.tv_nsec = now.tv_usec * 1000;
        error = pthread_cond_timedwait(cv, mutex, &abstime);
    }
    return (error == ETIMEDOUT) ? 1 : 0;
}

void ConnxOnceInit(connx_once_t* once_control, void (*init_routine)(void)) {
    CONNX_ASSERT(pthread_once(once_control, init_routine) == 0);
}

#endif
