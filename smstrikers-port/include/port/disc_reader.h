#ifndef PORT_DISC_READER_H
#define PORT_DISC_READER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Runs jobs in order on one reader thread. Blocks if the queue is full.
void PortDiscQueue(void (*work)(void*), void* ctx);

// Starts a separate worker. PortDiscJoin waits for completion and deletes the thread object.
void* PortDiscSpawn(void (*work)(void*), void* ctx);
void PortDiscJoin(void* thread);

#ifdef __cplusplus
}
#endif

// Release/acquire publication for reader state and completed data.
#if defined(_MSC_VER) && !defined(__clang__)
// MSVC builds this into the settings app on Windows and has no __atomic builtins; these are full barriers.
#include <intrin.h>

static inline void port_store_release_i32(int32_t* p, int32_t v)
{
    _InterlockedExchange((volatile long*)p, v);
}

static inline int32_t port_load_acquire_i32(const int32_t* p)
{
    return _InterlockedCompareExchange((volatile long*)p, 0, 0);
}

static inline void port_store_release_u64(unsigned long long* p, unsigned long long v)
{
    _InterlockedExchange64((volatile long long*)p, (long long)v);
}

static inline unsigned long long port_load_acquire_u64(const unsigned long long* p)
{
    return (unsigned long long)_InterlockedCompareExchange64((volatile long long*)p, 0, 0);
}
#else
static inline void port_store_release_i32(int32_t* p, int32_t v)
{
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

static inline int32_t port_load_acquire_i32(const int32_t* p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static inline void port_store_release_u64(unsigned long long* p, unsigned long long v)
{
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

static inline unsigned long long port_load_acquire_u64(const unsigned long long* p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
#endif

#endif
