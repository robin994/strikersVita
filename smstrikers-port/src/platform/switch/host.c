// Host services on Horizon through newlib and libnx.

#include "port/host.h"
#include "port/region.h"

#include <switch.h>

#include <dirent.h>
#include <malloc.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// newlib defines it but declares it only under its own feature macros.
void* memalign(size_t alignment, size_t size);

// libnx fills these from the loader's argument string; argv[0] is the .nro's own sdmc: path.
extern int __system_argc;
extern char** __system_argv;

#define PORT_SWITCH_HOME "sdmc:/switch/strikers"

unsigned long long port_monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

void port_sleep_ns(unsigned long long ns)
{
    struct timespec req;
    req.tv_sec = (time_t)(ns / 1000000000ull);
    req.tv_nsec = (long)(ns % 1000000000ull);
    nanosleep(&req, NULL);
}

void port_sleep_until_ns(unsigned long long deadline_ns)
{
    const unsigned long long now = port_monotonic_ns();
    if (deadline_ns > now)
        port_sleep_ns(deadline_ns - now);
}

void port_tighten_timer_slack(void) {}

void port_yield(void)
{
    sched_yield();
}

// Horizon has no per-process CPU accounting a homebrew application can read.
unsigned long long port_process_cpu_ns(void)
{
    return 0;
}

int port_process_energy(unsigned long long* energyNj, unsigned long long* idleWakeups)
{
    (void)energyNj;
    (void)idleWakeups;
    return 0;
}

void* port_aligned_alloc(size_t alignment, size_t size)
{
    // newlib declares posix_memalign but does not implement it; free() releases memalign blocks.
    return memalign(alignment, size);
}

void port_aligned_free(void* ptr)
{
    free(ptr);
}

int port_localtime(time_t when, struct tm* out)
{
    return localtime_r(&when, out) != NULL ? 0 : -1;
}

int port_executable_dir(char* buf, size_t size)
{
    // Launched from a network loader, argv[0] is not a path on the card; the install directory is.
    const char* exe = (__system_argc > 0 && __system_argv != NULL) ? __system_argv[0] : NULL;
    if (exe == NULL || strncmp(exe, "sdmc:/", 6) != 0 || strrchr(exe, '/') == NULL)
    {
        if (sizeof PORT_SWITCH_HOME > size)
            return -1;
        memcpy(buf, PORT_SWITCH_HOME, sizeof PORT_SWITCH_HOME);
        return 0;
    }
    const size_t n = (size_t)(strrchr(exe, '/') - exe);
    if (n >= size)
        return -1;
    memcpy(buf, exe, n);
    buf[n] = '\0';
    return 0;
}

int port_scan_dir(const char* path,
                  void (*visit)(void* user, const char* name),
                  void* user)
{
    DIR* d = opendir(path);
    struct dirent* e;
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.' &&
            (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0')))
            continue;
        visit(user, e->d_name);
    }
    closedir(d);
    return 0;
}

int port_setenv_default(const char* name, const char* value)
{
    if (getenv(name) != NULL)
        return 1;
    return setenv(name, value, 0) == 0 ? 0 : -1;
}

int port_docked(void)
{
    return appletGetOperationMode() == AppletOperationMode_Console ? 1 : 0;
}

int PortSwitchLanguage(void)
{
    static int s_checked;
    static int s_language = PORT_LANGUAGE_UNSET;
    u64 code;
    SetLanguage language;

    if (s_checked)
        return s_language;
    s_checked = 1;
    if (port_region() == PORT_REGION_USA || R_FAILED(setInitialize()))
        return s_language;
    if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &language)))
    {
        switch (language)
        {
        case SetLanguage_ENUS:
        case SetLanguage_ENGB:  s_language = PORT_LANGUAGE_ENGLISH; break;
        case SetLanguage_DE:    s_language = PORT_LANGUAGE_GERMAN; break;
        case SetLanguage_FR:
        case SetLanguage_FRCA:  s_language = PORT_LANGUAGE_FRENCH; break;
        case SetLanguage_ES:
        case SetLanguage_ES419: s_language = PORT_LANGUAGE_SPANISH; break;
        case SetLanguage_IT:    s_language = PORT_LANGUAGE_ITALIAN; break;
        case SetLanguage_JA:
            if (port_region() == PORT_REGION_JAPAN)
                s_language = PORT_LANGUAGE_JAPANESE;
            break;
        default: break;
        }
    }
    setExit();
    return s_language;
}
