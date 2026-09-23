// Process setup and teardown on Horizon, in libnx's hooks around main().

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <switch.h>

#include "port/config.h"
#include "port/host.h"
#include "port/switch/clocks.h"
#include "port/switch/memory.h"
#include "port/switch/sqlite_vfs.h"

static int s_nxlink = -1;

// Sync periodically because closing from HOME can bypass normal shutdown.
static Thread s_logSync;
static bool s_logSyncRunning;
static volatile bool s_logSyncStop;
static bool s_logToFile;

// PortLogAsync queues messages for the log thread instead of writing on the caller's thread.
#define PORT_LOG_RING 128
#define PORT_LOG_LINE 640
static char s_logRing[PORT_LOG_RING][PORT_LOG_LINE];
static unsigned s_logHead;
static unsigned s_logTail;
static unsigned long long s_logDropped;
static Mutex s_logMutex;

void PortLogAsync(const char* line)
{
    unsigned next;
    if (!s_logSyncRunning)
        return;
    mutexLock(&s_logMutex);
    next = (s_logHead + 1u) % PORT_LOG_RING;
    if (next == s_logTail)
    {
        s_logDropped++;   // reported by the drain, never written from here
    }
    else
    {
        size_t n = strlen(line);
        if (n >= PORT_LOG_LINE)
            n = PORT_LOG_LINE - 1;
        memcpy(s_logRing[s_logHead], line, n);
        s_logRing[s_logHead][n] = '\0';
        s_logHead = next;
    }
    mutexUnlock(&s_logMutex);
}

static void log_drain(void)
{
    for (;;)
    {
        char line[PORT_LOG_LINE];
        unsigned long long dropped = 0;
        mutexLock(&s_logMutex);
        if (s_logTail == s_logHead)
        {
            dropped = s_logDropped;
            s_logDropped = 0;
            mutexUnlock(&s_logMutex);
            if (dropped != 0)
                fprintf(stderr, "[log] %llu line(s) dropped; the ring filled\n", dropped);
            return;
        }
        memcpy(line, s_logRing[s_logTail], sizeof line);
        s_logTail = (s_logTail + 1u) % PORT_LOG_RING;
        mutexUnlock(&s_logMutex);
        fputs(line, stderr);
    }
}

static void log_thread_start(void);

// strikers.ini's log key: stdout and stderr share one file, since Horizon allows one writer.
int PortSwitchLogToFile(const char* path)
{
    if (s_nxlink >= 0)
        return 0;
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return -1;
    const int savedOut = dup(STDOUT_FILENO);
    if (savedOut < 0)
    {
        close(fd);
        return -1;
    }
    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0)
    {
        dup2(savedOut, STDOUT_FILENO);
        close(savedOut);
        close(fd);
        return -1;
    }
    close(savedOut);
    close(fd);
    // Unbuffered: a Horizon process that dies takes its stdio buffers with it.
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    s_logToFile = true;
    log_thread_start();
    return 0;
}

// FastLoad: the CPU at 1785 MHz and the GPU at its minimum. 
void PortSwitchCpuBoost(int on)
{
    static int s_on = -1;
    static unsigned long long s_since;
    if (on == s_on)
        return;
    const Result rc = appletSetCpuBoostMode(on ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
    const unsigned long long now = port_monotonic_ns();
    char line[96];
    if (on)
        snprintf(line, sizeof line, "[clocks] load boost on, rc 0x%x\n", (unsigned)rc);
    else
        snprintf(line, sizeof line, "[clocks] load boost off after %.1f s, rc 0x%x\n",
                 (double)(now - s_since) / 1e9, (unsigned)rc);
    PortLogAsync(line);
    s_on = on;
    s_since = now;
}

static void log_sync_main(void* arg)
{
    (void)arg;
    for (unsigned tick = 1; !s_logSyncStop; tick++)
    {
        svcSleepThread(100000000LL);
        log_drain();
        if (s_logToFile && tick % 10 == 0)
            fsync(STDOUT_FILENO);
    }
    log_drain();
}

static void log_thread_start(void)
{
    if (s_logSyncRunning)
        return;
    if (R_SUCCEEDED(threadCreate(&s_logSync, log_sync_main, NULL, NULL, 0x10000, 0x3B, -2)))
        s_logSyncRunning = R_SUCCEEDED(threadStart(&s_logSync));
}

// newlib's own, from its <malloc.h>, which the game tree's MSL shim shadows.
struct mallinfo
{
    size_t arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost;
};
struct mallinfo mallinfo(void);

// libnx maps nearly all of the process's memory as heap at startup; sbrk hands it to newlib.
extern char* fake_heap_end;

void PortSwitchHeapInfo(unsigned long long* inUse, unsigned long long* available)
{
    const struct mallinfo info = mallinfo();
    *inUse = (unsigned long long)info.uordblks;
    *available =
        (unsigned long long)info.fordblks + (unsigned long long)(fake_heap_end - (char*)sbrk(0));
}

// Holding Capture saves the last 30 seconds where the host game allows it; recording takes 96 MiB.
static void start_recording(void)
{
    const char* want = getenv("STRIKERS_VIDEO_CAPTURE");
    if (want != NULL && *want != '\0' && atoi(want) == 0)
        return;

    bool supported = false;
    Result rc = appletIsGamePlayRecordingSupported(&supported);
    if (R_FAILED(rc) || !supported)
    {
        fprintf(stderr, "[capture] video recording unavailable: supported %d, rc 0x%x\n",
                supported ? 1 : 0, (unsigned)rc);
        return;
    }

    rc = appletInitializeGamePlayRecording();
    fprintf(stderr, "[capture] video recording %s, rc 0x%x\n", R_SUCCEEDED(rc) ? "on" : "not started",
            (unsigned)rc);
}

void userAppInit(void)
{
    // Output goes to nxlink under `nxlink -s`, else nowhere until the `log` key opens a file.
    if (R_SUCCEEDED(socketInitializeDefault()))
    {
        s_nxlink = nxlinkStdio();
        if (s_nxlink < 0)
            socketExit();
    }

    char dir[512];
    if (port_executable_dir(dir, sizeof dir) == 0)
    {
        chdir(dir);
        // Store saves and caches beside the .nro, using paths relative to the SD card root.
        const char* onCard = strncmp(dir, "sdmc:", 5) == 0 ? dir + 5 : dir;
        char sub[600];
        snprintf(sub, sizeof sub, "%s/user", onCard);
        setenv("STRIKERS_USER_DIR", sub, 0);
        snprintf(sub, sizeof sub, "%s/cache", onCard);
        setenv("STRIKERS_CACHE_DIR", sub, 0);
    }

    if (R_SUCCEEDED(romfsInit()))
        setenv("STRIKERS_RESOURCES_DIR", "romfs:/", 0);

    mutexInit(&s_logMutex);
    if (s_nxlink >= 0)
    {
        log_thread_start();
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }

    // Launched from the album the process gets a few hundred MiB, less than the game's own arena.
    AppletType type = appletGetAppletType();
    if (type != AppletType_Application && type != AppletType_SystemApplication)
    {
        fprintf(stderr, "[port] applet mode (type %d) has too little memory; exiting\n", (int)type);
        ErrorApplicationConfig error;
        if (R_SUCCEEDED(errorApplicationCreate(&error,
                "Super Mario Strikers needs the full memory of title mode.",
                "Super Mario Strikers cannot run from the album, which limits homebrew to a few "
                "hundred MiB.\n\nStart it from the HOME menu instead: hold R while launching any "
                "installed game, then pick it in the homebrew menu.")))
            errorApplicationShow(&error);
        exit(1);
    }

    // start_recording reads the ini, which is otherwise first loaded as the disc opens.
    PortConfigLoad();
    start_recording();

    // Stays on until the startup shaders are compiled.
    PortSwitchCpuBoost(1);

    // NVK only exposes the conformant desktop GPUs it knows without this; Tegra X1 is not one.
    setenv("NVK_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 0);

    // Use the Horizon VFS for SD card and romfs paths.
    if (port_sqlite_vfs_register() != 0)
        fprintf(stderr, "[port] SQLite file layer registration failed; caches will not persist\n");

    psmInitialize();
}

// Increase the default thread stack for Tint's recursive resolver and Dawn's error paths.
#define PORT_SWITCH_THREAD_STACK (8u * 1024u * 1024u)

int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                          void* (*start)(void*), void* arg);

int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                          void* (*start)(void*), void* arg)
{
    pthread_attr_t sized;
    size_t size = 0;
    if (attr != NULL)
    {
        sized = *attr;
        pthread_attr_getstacksize(&sized, &size);
    }
    else
    {
        pthread_attr_init(&sized);
    }
    if (size == 0)
        pthread_attr_setstacksize(&sized, PORT_SWITCH_THREAD_STACK);
    return __real_pthread_create(thread, &sized, start, arg);
}

// libnx's abort exits without a report; a fault makes Atmosphere or an emulator log where it was.
__attribute__((noreturn)) void abort(void)
{
    fflush(NULL);
    volatile int* fault = NULL;
    *fault = 0;
    for (;;)
    {
    }
}

void userAppExit(void)
{
    PortSwitchCpuBoost(0);
    if (s_logSyncRunning)
    {
        s_logSyncStop = true;
        threadWaitForExit(&s_logSync);
        threadClose(&s_logSync);
        if (s_logToFile)
            fsync(STDOUT_FILENO);
    }
    psmExit();
    romfsExit();
    if (s_nxlink >= 0)
    {
        close(s_nxlink);
        socketExit();
    }
}
