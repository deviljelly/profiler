#ifndef UTIL_H_
#define UTIL_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __WIN32__
#include <windows.h>
#endif

#ifdef __linux
#include <pthread.h>
#endif

#ifdef __MVS__
#include <pthread.h>
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

#define UNLOCKED 0
#define LOCKED 1
#define NOT_WRITTEN 0
#define WRITTEN 1
#define MIN_LOCK_WAIT_NS 1000
#define MAX_LOCK_WAIT_NS 100000
#define LOCK_WAIT_STEP_NS 1000

//#define LOCKING
//#define TRACE
//#define DEBUG
#define INFO
#define WARN
#define _ERROR


#ifdef __linux__
#define JVMStringToPlatform(STR) (STR)
#define platformStringToJVM(STR) (STR)
#elif __AIX__
#define JVMStringToPlatform(STR) (STR)
#define platformStringToJVM(STR) (STR)
#elif __SOLARIS__
#define JVMStringToPlatform(STR) (STR)
#define platformStringToJVM(STR) (STR)
#elif __WIN32__
#define JVMStringToPlatform(STR) (STR)
#define platformStringToJVM(STR) (STR)
#elif __MVS__
#define JVMStringToPlatform(STR) ({         \
                     char *str=(STR);           \
                     size_t len = strlen(str);  \
                     char *buf=alloca(len+1);           \
                     memcpy(buf, str, len+1);   \
                     __a2e_l(buf, len);         \
                     buf;                       \
                  })

#define platformStringToJVM(STR) ({         \
                     char *str=(STR);           \
                     size_t len = strlen(str);  \
                     char *buf=alloca(len+1);           \
                     memcpy(buf, str, len+1);   \
                     __e2a_l(buf, len);         \
                     buf;                       \
                  })
#endif

#ifdef LOCKING
#define locking(...) \
{fprintf(stderr, "LOCKING: ");fprintf(stderr, __FUNCTION__);fprintf(stderr,": ");fprintf(stderr,__VA_ARGS__);fflush(stderr);}
#else
#define locking(...)
#endif

#ifdef TRACE
#define trace(...) \
{fprintf(stderr, "TRACE: ");fprintf(stderr, __FUNCTION__);fprintf(stderr,": ");fprintf(stderr,__VA_ARGS__);fflush(stderr);}
#else
#define trace(...)
#endif

#ifdef DEBUG
#define debug(...) \
{fprintf(stderr, "DEBUG: ");fprintf(stderr, __FUNCTION__);fprintf(stderr,": ");fprintf(stderr,__VA_ARGS__);fflush(stderr);}
#else
#define debug(...)
#endif

#ifdef INFO
#define info(...) \
{fprintf(stderr, "INFO: ");fprintf(stderr,__VA_ARGS__);fflush(stderr);}
#else
#define info(...)
#endif

#ifdef WARN
#define warn(...) \
{fprintf(stderr, "WARN: ");fprintf(stderr,__VA_ARGS__);fflush(stderr);}
#else
#define warn(...)
#endif

#ifdef _ERROR
#define error(...) \
{fprintf(stderr, "ERROR: ");fprintf(stderr, __FUNCTION__);fprintf(stderr,": ");fprintf(stderr,__VA_ARGS__);fflush(stderr);}
#else
#define error(...)
#endif

typedef uint32_t LockStructure;

typedef struct Buffer_struct Buffer;

struct Buffer_struct {
    uint32_t bufferLength;
    uint32_t bufferOffset;
    int shared;
    volatile LockStructure lock;
    uint8_t *buffer;
};


#if defined (__WIN32__)
static inline int winnanosleep(const struct timespec *req, struct timespec *rem) {

    HANDLE timer = NULL;
    LARGE_INTEGER sleepTime;

    sleepTime.QuadPart = req->tv_sec * 1000000000 + req->tv_nsec / 100;

    timer = CreateWaitableTimer(NULL, TRUE, NULL);

    if (timer == NULL) {
        LPVOID buffer = NULL;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
                      GetLastError(), 0, (LPTSTR) &buffer, 0, NULL);
        error("nanosleep: CreateWaitableTimer failed: (%" PRIu64 ") %s\n", (uint64_t) GetLastError(), (LPTSTR) buffer);
        LocalFree(buffer);
        CloseHandle(timer);
        return -1;
    }

    if (!SetWaitableTimer(timer, &sleepTime, 0, NULL, NULL, 0)) {
        LPVOID buffer = NULL;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
                      GetLastError(), 0, (LPTSTR) &buffer, 0, NULL);
        error("nanosleep: SetWaitableTimer failed: (%" PRIu64 ") %s\n", (uint64_t) GetLastError(), (LPTSTR) buffer);
        LocalFree(buffer);
        CloseHandle(timer);
        return -1;
    }

    if (WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0) {
        LPVOID buffer = NULL;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
                      GetLastError(), 0, (LPTSTR) &buffer, 0, NULL);
        error("nanosleep: WaitForSingleObject failed: (%" PRIu64 ") %s\n", (uint64_t) GetLastError(), (LPTSTR) buffer);
        LocalFree(buffer);
        CloseHandle(timer);
        return -1;
    }

    CloseHandle(timer);

    return 0;

}
#endif


static inline uint32_t hashUint32(uint32_t x) {

    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x);
    return x;

}

static inline uint64_t hashUint64(uint64_t x) {

    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
    x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
    x = x ^ (x >> 31);
    return x;
}

static inline uint32_t jenkins_one_at_a_time_hash(const char *key, size_t len) {

    uint32_t hash, i;
    for (hash = i = 0; i < len; ++i) {
        hash += key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }

    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;

}

static inline uint64_t getTicks() {

#ifdef __x86_64__
    uint32_t lower;
    uint32_t upper;

    __asm__ __volatile__ ("rdtsc": "=a" (lower), "=d"  (upper));
    uint64_t ticks = ((uint64_t) upper << 32) + lower;

#elif __MVS__
    volatile uint64_t ticks;
   __stckf((unsigned long long *)&ticks);

#elif __i386__
    uint32_t lower;
    uint32_t upper;

    __asm__ __volatile__ ("rdtsc": "=a" (lower), "=d"  (upper));
    uint64_t ticks = ((uint64_t) upper << 32) + lower;

#elif __powerpc64__
    uint32_t lower;

    uint32_t upper;

    do{

        __asm__ __volatile__ ("mftbu %0" : "=r"(upper));
        __asm__ __volatile__ ("mftb %0" : "=r"(lower));
        __asm__ __volatile__ ("mftbu %0" : "=r"(upper1));

    } while(upper!=upper1);

    uint64_t ticks = ((uint64_t) upper << 32) + lower;

#endif

    return ticks;

}


#if defined __linux || defined __WIN32__
static inline uint32_t atomicIncrement(volatile uint32_t *value) {
    return __sync_add_and_fetch(value, 1);
}
#elif __MVS__


static inline uint32_t atomicIncrement(uint32_t *pointer) {

    uint32_t old = *pointer;
    uint32_t new = old+1;

        while(__cs1(&old, pointer, &new)) {
            new = old+1;
    }

            return old;

}
#endif


#ifdef __MVS__
static inline int nanosleep(const struct timespec *rqtp, struct timespec *rmtp) {

       struct timeval req = {.tv_sec = rqtp->tv_sec, .tv_usec = rqtp->tv_nsec/1000};

       return select(0,NULL,NULL,NULL, &req);

}
#endif


#ifdef __MVS__
static inline bool isLocked(volatile LockStructure *lock) {

       uint32_t locked = LOCKED;
       return __cs1((void*)locked, (void*)lock, (void*)locked);

}


static inline bool isUnlocked(volatile LockStructure *lock) {

   uint32_t locked = UNLOCKED;
   return __cs1((void*)locked, (void*)lock, (void*)locked);

}


static inline bool unlockIfLocked(volatile LockStructure *lock) {

    uint32_t locked = LOCKED;
    uint32_t unlocked = UNLOCKED;

    bool nowUnlocked = !__cs1((void*)&locked, (void*)lock, (void*)&unlocked);

    debug("unlockedIfLocked %d\n", nowUnlocked);
    return(nowUnlocked);

}


static inline bool lockIfUnlocked(volatile LockStructure *lock) {

    uint32_t locked = LOCKED;
    uint32_t unlocked = UNLOCKED;

    bool nowLocked = !__cs1((void*)&unlocked, (void*)lock, (void*)&locked);

    debug("lockIfUnlocked %d\n", nowLocked);
    return(nowLocked);

}
#endif


static inline uint64_t lock(volatile LockStructure *lock, bool log) {

    uint64_t sleepNS = MIN_LOCK_WAIT_NS;
    uint64_t sleptForNS = 0;

#if defined __linux || defined __WIN32__
    while(__sync_val_compare_and_swap(lock, UNLOCKED, LOCKED)) {
#elif __MVS__

        uint32_t locked = LOCKED;
        uint32_t unlocked = UNLOCKED;

        if(log) {
            locking("Locking1, %d %p %d\n", pthread_self(), lock, *lock);
        }

        uint32_t notGotLock = 0;

        notGotLock = __cs1((void*)&unlocked, (void*)lock, (void*)&locked);

        if(log) {
            locking("Locking2, %d %p %d %d\n", pthread_self(), lock, *lock, notGotLock);
        }

        while(notGotLock) {

            if(log) {
                locking("Fell Through, %d %p %d %d\n", pthread_self(), lock, *lock, notGotLock);
            }

#endif
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = sleepNS;

#ifdef __linux
            nanosleep(&ts, NULL);
#elif __MVS__
            nanosleep(&ts, NULL);
#elif __WIN32__
            winnanosleep((const struct timespec *) &ts, NULL);
#endif

            sleptForNS += sleepNS;
            sleepNS += LOCK_WAIT_STEP_NS;
            if (sleepNS > MAX_LOCK_WAIT_NS) {
                sleepNS = MAX_LOCK_WAIT_NS;
            }
#ifdef __MVS__
            locked = LOCKED;
            unlocked = UNLOCKED;
            notGotLock = __cs1((void*)&unlocked, (void*)lock, (void*)&locked);
#endif
        }

        if(log) {
            locking("locked, %d %p %d, slept for %d\n", pthread_self(), lock, *lock, sleptForNS);
        }

        return sleptForNS;

}


static inline void unlock(volatile LockStructure *lock, bool log) {

#ifdef __linux
    __sync_lock_test_and_set(lock, UNLOCKED);
#elif __MVS__
    uint32_t locked = LOCKED;
    uint32_t unlocked = UNLOCKED;
    __cs1((void*)&locked, (void*)lock, (void*)&unlocked);

    if(log) {
        locking("Unlocked, %d %p %d\n", pthread_self(), lock, *lock);
    }
#endif

}


static inline uint32_t compareAndSwapPtrBool(volatile uintptr_t *targetPtr, volatile void *oldPtr, volatile void *newPtr) {
//debug("target: %p old:%p new:%p\n", targetPtr, oldPtr, newPtr);
#ifdef __linux
    return __sync_bool_compare_and_swap(targetPtr, (uintptr_t) oldPtr, (uintptr_t) newPtr);
#elif __MVS__
    volatile void *tmpOldPtr = (oldPtr);
    volatile void *tmpNewPtr = (newPtr);
   //debug("target: %p tmpOldPtr: %p old:%p new:%p\n", targetPtr, tmpOldPtr, oldPtr, newPtr);
    return !__csg((void *)&tmpOldPtr, (void*)targetPtr, (void*)&newPtr);
#endif

}


static inline uint32_t alreadyWritten(volatile uint32_t *writtenFlag) {

#if defined __linux || defined __WIN32__
    uint32_t prev = __sync_val_compare_and_swap(writtenFlag, NOT_WRITTEN, WRITTEN);
#elif __MVS__
    uint32_t prev = *writtenFlag;
    uint32_t written = WRITTEN;
    __cs1((void*)&prev, (void*)writtenFlag, (void*)&written);
#endif

    if (prev == NOT_WRITTEN) {
        return 0;
    } else {
        return 1;
    }

}

#endif /* UTIL_H_ */
