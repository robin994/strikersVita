
#ifndef PORT_HOST_H
#define PORT_HOST_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Nanoseconds from an unspecified epoch, never going backwards.
unsigned long long port_monotonic_ns(void);

// Sleep for at least `ns`. Accuracy matters here rather than being a nicety: this is what paces the
// frame loop at 60Hz, so a sleep that rounds up to the scheduler's default granularity drops the
// frame rate outright.
void port_sleep_ns(unsigned long long ns);

// Sleep until a port_monotonic_ns() deadline: an absolute sleep on Linux, a relative one elsewhere.
void port_sleep_until_ns(unsigned long long deadline_ns);

// Cut the calling thread's Linux timer slack from its 50 us default; a no-op elsewhere.
void port_tighten_timer_slack(void);

// Give the rest of the time slice to any other thread and come straight back: the spin half of the
// frame limiter's wait, since every host sleeps late by an amount that scales with the request.
void port_yield(void);

// malloc with an alignment stronger than max_align_t.
void* port_aligned_alloc(size_t alignment, size_t size);
void port_aligned_free(void* ptr);

// Current operation mode: 1 docked, 0 handheld, -1 on other platforms.
int port_docked(void);

// Local time, into caller-provided storage. Returns 0 on success.
int port_localtime(time_t when, struct tm* out);

// Absolute path of the directory holding the running executable, with no trailing separator.
int port_executable_dir(char* buf, size_t size);

// Call `visit` once per entry in the directory `path`, skipping "." and ".".
int port_setenv_default(const char* name, const char* value);

int port_scan_dir(const char* path,
                  void (*visit)(void* user, const char* name),
                  void* user);

// The port's allocation region: bytes handed out by the bump allocator against the size it
// reserved.
void port_region_stats(size_t* used, size_t* total);

#ifdef __cplusplus
}
#endif

#endif // PORT_HOST_H
