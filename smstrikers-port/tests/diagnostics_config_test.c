#include "port/config.h"
#include "port/host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

int port_executable_dir(char* out, size_t size)
{
    (void)out; (void)size;
    return -1;
}

int port_setenv_default(const char* name, const char* value)
{
    return getenv(name) ? 1 : setenv(name, value, 1);
}

static void run_case(const char* directory, int mode)
{
    const pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0)
    {
        char ini[1024], log[1024];
        snprintf(ini, sizeof ini, "%s/case-%d.ini", directory, mode);
        snprintf(log, sizeof log, "%s/case-%d.log", directory, mode);
        // Each launch gets a fresh configuration, as on Vita after a restart.
        unsetenv("STRIKERS_DIAGNOSTICS");
        unsetenv("STRIKERS_FPS_OVERLAY");
        unsetenv("STRIKERS_AURORA_DISTINCT_CPU_CORES");
        unsetenv("STRIKERS_GXM_IMMEDIATE_DRAW_VIEW");
        unsetenv("STRIKERS_TASK_PROFILE");
        unsetenv("STRIKERS_PROBE_SKIN");
        unsetenv("STRIKERS_LOG");
        unsetenv("STRIKERS_GXM_DISABLE");
        unsetenv("STRIKERS_VITA_CORE3_MAX_TOTAL_PCT");
        unsetenv("STRIKERS_GXM_FIXED_UNIFORM_POOL");
        FILE* file = fopen(ini, "w");
        CHECK(file != NULL);
        if (mode != 0)
            fprintf(file, "diagnostics = %d\n", mode == 1 || mode == 3 ? 1 : 0);
        fprintf(file, "fps_overlay = %d\n", mode == 4 ? 0 : 1);
        fprintf(file, "task_profile = 1\nprobe_skin = 1\nlog_audio = 1\n"
                      "vita_packet_profile_period = 16\naurora_diagnostics = 1\n"
                      "gxm_disable = 0x418\ngxm_fixed_uniform_pool = 1\n"
                      "vita_core3_max_total_pct = 50\naurora_distinct_cpu_cores = 1\n"
                      "gxm_immediate_draw_view = 0\nlog = %s\n", log);
        CHECK(fclose(file) == 0);
        CHECK(setenv("STRIKERS_CONFIG", ini, 1) == 0);
        if (mode == 3)
            CHECK(setenv("STRIKERS_DIAGNOSTICS", "0", 1) == 0);
        if (mode == 5)
            CHECK(setenv("STRIKERS_DIAGNOSTICS", "1", 1) == 0);
        const int enabled = mode == 0 || mode == 1 || mode == 5;
        const int applied = PortConfigLoad();
        CHECK(applied > 0);
        CHECK(PortConfigLoad() == applied);
        CHECK(strcmp(PortConfigPath(), ini) == 0);
        CHECK(PortDiagnosticsEnabled() == enabled);
        CHECK(PortFpsOverlayEnabled() == (mode != 4));
        CHECK(strcmp(getenv("STRIKERS_GXM_DISABLE"), "0x418") == 0);
        CHECK(strcmp(getenv("STRIKERS_GXM_FIXED_UNIFORM_POOL"), "1") == 0);
        CHECK(strcmp(getenv("STRIKERS_AURORA_DISTINCT_CPU_CORES"), "1") == 0);
        CHECK(strcmp(getenv("STRIKERS_GXM_IMMEDIATE_DRAW_VIEW"), "0") == 0);
        CHECK(strcmp(getenv("STRIKERS_VITA_CORE3_MAX_TOTAL_PCT"), "50") == 0);
        const char* selectors[] = { "STRIKERS_TASK_PROFILE", "STRIKERS_PROBE_SKIN",
            "STRIKERS_LOG_AUDIO", "STRIKERS_VITA_PACKET_PROFILE_PERIOD",
            "STRIKERS_AURORA_DIAGNOSTICS", "STRIKERS_LOG" };
        for (size_t i = 0; i < sizeof selectors / sizeof selectors[0]; ++i)
            CHECK((getenv(selectors[i]) != NULL) == enabled);
        struct stat info;
        CHECK((stat(log, &info) == 0) == enabled);
        // The OFF gate must not rewrite or remove the selected probes in the INI.
        CHECK(stat(ini, &info) == 0 && info.st_size > 0);
        unlink(ini);
        unlink(log);
        exit(0);
    }
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(void)
{
    char directory[] = "/tmp/strikers-diagnostics-test-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    for (int mode = 0; mode < 6; ++mode)
        run_case(directory, mode);
    CHECK(rmdir(directory) == 0);
    puts("diagnostics config: default/ON/OFF, environment precedence, no OFF log, renderer and CPU3 preserved");
    return 0;
}
