#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <strings.h>
#define strcmpi strcasecmp
#define TARGET_PC 1
#include "../src/platform/dvd.c"

static long read_result;

void OSReport(const char* fmt, ...) { (void)fmt; }
long port_disc_read(PortDisc* disc, void* dst, size_t len, unsigned long long offset)
{
    (void)disc;
    (void)offset;
    if (read_result > 0)
    {
        assert((size_t)read_result <= len);
        memset(dst, 0x5a, (size_t)read_result);
    }
    return read_result;
}
int PortConfigLoad(void) { return 0; }
void PortSetDiscGameName(const char* code) { (void)code; }
int port_executable_dir(char* buf, size_t size) { (void)buf; (void)size; return -1; }
int port_scan_dir(const char* path, void (*visit)(void*, const char*), void* user)
{ (void)path; (void)visit; (void)user; return -1; }
void port_fatal(const char* title, const char* message) { (void)title; (void)message; abort(); }
PortDisc* port_disc_open(const char* path, char* error, size_t size)
{ (void)path; (void)error; (void)size; return NULL; }
const char* port_disc_format(const PortDisc* disc) { (void)disc; return "raw"; }
int port_disc_looks_like_image(const char* path) { (void)path; return 0; }
int port_disc_walk(PortDisc* disc, PortDiscVisit visit, void* user, char* error, size_t size)
{ (void)disc; (void)visit; (void)user; (void)error; (void)size; return -1; }
unsigned long long port_monotonic_ns(void) { return 0; }

static void reset_file(DVDFileInfo* file, u32 length)
{
    memset(file, 0, sizeof *file);
    file->startAddr = 0;
    file->length = length;
    file->cb.state = DVD_STATE_END;
}

int main(void)
{
    DvdEntry entry = { .path = "test", .length = 64 };
    DVDFileInfo file;
    unsigned char buffer[32] = {0};
    s_entries = &entry;
    s_count = 1;
    s_disc = (PortDisc*)&entry;

    // A successful synchronous host/Vita read must report that the async
    // command was accepted. DolphinFile::ReadAsync treats FALSE as fatal.
    reset_file(&file, entry.length);
    read_result = 32;
    assert(DVDReadAsyncPrio(&file, buffer, 32, 0, NULL, 0) == TRUE);
    assert(file.cb.transferredSize == 32);
    assert(DVDGetCommandBlockStatus(&file.cb) == DVD_STATE_BUSY);
    assert(DVDGetCommandBlockStatus(&file.cb) == DVD_STATE_END);

    // Immediate failures and short reads are not accepted as complete data.
    reset_file(&file, entry.length);
    read_result = -1;
    assert(DVDReadAsyncPrio(&file, buffer, 32, 0, NULL, 0) == FALSE);
    assert(DVDGetCommandBlockStatus(&file.cb) == DVD_STATE_FATAL_ERROR);

    reset_file(&file, entry.length);
    read_result = 12;
    assert(DVDReadAsyncPrio(&file, buffer, 32, 0, NULL, 0) == FALSE);
    assert(DVDGetCommandBlockStatus(&file.cb) == DVD_STATE_FATAL_ERROR);

    // The GameCube layer rounds the final transfer to 32 bytes. Only bytes
    // remaining in the logical file are required from the ISO.
    entry.length = 45;
    reset_file(&file, entry.length);
    read_result = 13;
    assert(DVDReadAsyncPrio(&file, buffer, 32, 32, NULL, 0) == TRUE);
    assert(file.cb.transferredSize == 13);
    assert(DVDGetCommandBlockStatus(&file.cb) == DVD_STATE_BUSY);
    assert(DVDGetCommandBlockStatus(&file.cb) == DVD_STATE_END);

    puts("Vita/host DVD async contract passed");
    return 0;
}
