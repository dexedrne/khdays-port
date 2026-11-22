/* The synchronization and performance counters shared by runtime services. */
#ifndef KHDAYS_HOST_SYNC_H
#define KHDAYS_HOST_SYNC_H
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <stdint.h>
#include <limits.h>
#define MAX_PATH PATH_MAX
#include <time.h>
#include <string.h>
#include <strings.h>

/* The C library names the Windows sources use. */
#define _strdup strdup
#define _stricmp strcasecmp
#define _strnicmp strncasecmp

typedef int32_t LONG;
typedef long long LONGLONG;
typedef long long LONG64;
typedef unsigned long long ULONGLONG;
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;
typedef pthread_rwlock_t SRWLOCK;
#define InitializeSRWLock(lock) pthread_rwlock_init(lock, NULL)
#define InterlockedExchangeAdd(ptr, value) __atomic_fetch_add((ptr), (value), __ATOMIC_SEQ_CST)
#define SRWLOCK_INIT PTHREAD_RWLOCK_INITIALIZER
#define AcquireSRWLockExclusive pthread_rwlock_wrlock
#define TryAcquireSRWLockExclusive(lock) (pthread_rwlock_trywrlock(lock) == 0)
#define AcquireSRWLockShared pthread_rwlock_rdlock
#define ReleaseSRWLockExclusive pthread_rwlock_unlock
#define ReleaseSRWLockShared pthread_rwlock_unlock

#define InterlockedExchange(ptr, value) __atomic_exchange_n((ptr), (value), __ATOMIC_SEQ_CST)
#define InterlockedIncrement(ptr) __atomic_add_fetch((ptr), 1, __ATOMIC_SEQ_CST)
#define InterlockedDecrement(ptr) __atomic_sub_fetch((ptr), 1, __ATOMIC_SEQ_CST)
/* As on Windows: Or and And return the value before, Add64 and Increment64 the value after. */
#define InterlockedOr(ptr, value) __atomic_fetch_or((ptr), (value), __ATOMIC_SEQ_CST)
#define InterlockedAnd(ptr, value) __atomic_fetch_and((ptr), (value), __ATOMIC_SEQ_CST)
#define InterlockedExchange64(ptr, value) __atomic_exchange_n((ptr), (LONG64)(value), __ATOMIC_SEQ_CST)
#define InterlockedIncrement64(ptr) __atomic_add_fetch((ptr), 1, __ATOMIC_SEQ_CST)
#define InterlockedAdd64(ptr, value) __atomic_add_fetch((ptr), (LONG64)(value), __ATOMIC_SEQ_CST)
static inline LONG64 InterlockedCompareExchange64(volatile LONG64 *ptr, LONG64 value, LONG64 expected)
{
    __atomic_compare_exchange_n(ptr, &expected, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
static inline LONG InterlockedCompareExchange(volatile LONG *ptr, LONG value, LONG expected)
{
    __atomic_compare_exchange_n(ptr, &expected, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
static inline void QueryPerformanceCounter(LARGE_INTEGER *value)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    value->QuadPart = now.tv_sec * 1000000000LL + now.tv_nsec;
}
static inline void QueryPerformanceFrequency(LARGE_INTEGER *value)
{
    value->QuadPart = 1000000000LL;
}
static inline ULONGLONG GetTickCount64(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart / 1000000;
}
#endif
#endif
