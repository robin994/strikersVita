#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <strings.h>
#define strcmpi strcasecmp
#define __SWITCH__ 1
#define TARGET_PC 1
#include "../src/platform/dvd.c"

static void (*queued_work)(void*);
static void* queued_ctx;
static long read_result;
static int in_memory;
static int callback_result;
static int callback_count;

void OSReport(const char* fmt, ...) { (void)fmt; }
void PortDiscQueue(void (*work)(void*), void* ctx) { queued_work = work; queued_ctx = ctx; }
long port_disc_read(PortDisc* disc, void* dst, size_t len, unsigned long long offset)
{
    (void)disc; (void)len; (void)offset;
    if (read_result > 0) memset(dst, 0x5a, (size_t)read_result);
    return read_result;
}
int port_disc_in_memory(PortDisc* disc) { (void)disc; return in_memory; }
unsigned long long port_monotonic_ns(void) { return 0; }
void port_yield(void) { if (queued_work) { void (*work)(void*) = queued_work; queued_work = NULL; work(queued_ctx); } }
int PortConfigLoad(void) { return 0; }
void PortSetDiscGameName(const char* code) { (void)code; }
int port_executable_dir(char* buf, size_t size) { (void)buf; (void)size; return -1; }
int port_scan_dir(const char* path, void (*visit)(void*, const char*), void* user) { (void)path; (void)visit; (void)user; return -1; }
void port_fatal(const char* title, const char* message) { (void)title; (void)message; abort(); }
PortDisc* port_disc_open(const char* path, char* error, size_t size) { (void)path; (void)error; (void)size; return NULL; }
const char* port_disc_format(const PortDisc* disc) { (void)disc; return "raw"; }
int port_disc_looks_like_image(const char* path) { (void)path; return 0; }
int port_disc_walk(PortDisc* disc, PortDiscVisit visit, void* user, char* error, size_t size)
{ (void)disc; (void)visit; (void)user; (void)error; (void)size; return -1; }
static void completed(s32 result, DVDFileInfo* file) { (void)file; callback_result = result; ++callback_count; }

int main(void)
{
    DvdEntry entry = { .path = "test", .length = 64 };
    s_entries = &entry;
    s_count = 1;
    s_disc = (PortDisc*)&entry;
    for (int ram = 0; ram <= 1; ++ram)
    {
        in_memory = ram;
        const long outcomes[] = { -1, 0, 12, 32 };
        for (unsigned i = 0; i < sizeof outcomes / sizeof outcomes[0]; ++i)
        {
            DVDFileInfo file = {0};
            unsigned char buffer[32] = {0};
            read_result = outcomes[i];
            callback_count = 0;
            assert(DVDReadAsyncPrio(&file, buffer, sizeof buffer, 0, completed, 0) == TRUE);
            if (!ram) port_yield();
            assert(DVDGetCommandBlockStatus(&file.cb) == DVD_STATE_BUSY);
            assert(DVDGetCommandBlockStatus(&file.cb) == DVD_STATE_END);
            assert(callback_count == 1 && callback_result == read_result);
            assert(file.cb.transferredSize == (read_result > 0 ? read_result : 0));
            if (read_result > 0) assert(buffer[read_result - 1] == 0x5a);
            assert(DVDClose(&file));
        }
    }
    puts("Switch DVD: async and RAM completion, errors, short reads and callbacks passed");
    return 0;
}
