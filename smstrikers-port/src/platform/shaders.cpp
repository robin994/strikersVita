#include "port/shaders.h"

#if defined(PORT_USE_AURORA)

#include "port/host.h"

#include <stdio.h>

#include <aurora/gfx.h>

namespace
{

// Past this the game moves on anyway and the rest finish in the background.
constexpr double kCapSeconds = 120.0;

bool s_waiting;
bool s_reported;
unsigned long long s_waitStart;

unsigned queued()
{
    uint32_t n = 0;
    aurora_get_pipeline_counts(&n, NULL);
    return n;
}

unsigned created()
{
    uint32_t n = 0;
    aurora_get_pipeline_counts(NULL, &n);
    return n;
}

double seconds_since(unsigned long long t)
{
    return (double)(port_monotonic_ns() - t) / 1e9;
}

} // namespace

void PortShaderStageBegin(void)
{
    if (queued() == 0)
        fprintf(stderr, "[shaders] all %u pipelines the cache knows are compiled\n", created());
    else
        fprintf(stderr, "[shaders] %u pipelines to compile, %u done; the memory card screen waits for them\n",
                queued(), created());
}

int PortShaderStagePending(void)
{
    if (s_reported || queued() == 0)
        return 0;
    if (!s_waiting)
    {
        s_waiting = true;
        s_waitStart = port_monotonic_ns();
        fprintf(stderr, "[shaders] memory card screen waiting for %u pipelines\n", queued());
    }
    return seconds_since(s_waitStart) < kCapSeconds ? 1 : 0;
}

int PortShaderStagePercent(void)
{
    uint32_t waiting = 0, done = 0;
    aurora_get_pipeline_counts(&waiting, &done);
    const unsigned long long total = (unsigned long long)done + waiting;
    return total != 0 ? (int)(100ull * done / total) : 100;
}

void PortShaderStageEnd(void)
{
    if (s_reported)
        return;
    s_reported = true;
    if (!s_waiting)
        fprintf(stderr, "[shaders] %u pipelines ready before the memory card screen finished\n",
                created());
    else if (queued() == 0)
        fprintf(stderr, "[shaders] %u pipelines ready; the memory card screen waited %.1f s\n",
                created(), seconds_since(s_waitStart));
    else
        fprintf(stderr,
                "[shaders] %u pipelines still compiling after %.0f s; moving on, the rest finish "
                "in the background\n",
                queued(), seconds_since(s_waitStart));
}

#endif // PORT_USE_AURORA
