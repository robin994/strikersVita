// SQLite VFS for Horizon: reads at EOF fail, writes past EOF leave gaps, and paths carry sdmc:.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sqlite3.h>

#include "port/switch/sqlite_vfs.h"

#define PORT_SQLITE_VFS_NAME "strikers_switch"

// Log the failing line and errno, which SQLite's error codes omit.
#define PORT_SQLITE_FAIL(what, code)                                                          \
    (fprintf(stderr, "[sql] %s failed at line %d, errno %d\n", (what), __LINE__, errno), (code))

typedef struct PortFile
{
    sqlite3_file base;
    int fd;
    // Cached file size for short reads and zero-filling gaps.
    sqlite3_int64 size;
} PortFile;

static int port_close(sqlite3_file* file)
{
    PortFile* self = (PortFile*)file;
    if (self->fd >= 0)
    {
        close(self->fd);
        self->fd = -1;
    }
    return SQLITE_OK;
}

static int port_read(sqlite3_file* file, void* buffer, int amount, sqlite3_int64 offset)
{
    PortFile* self = (PortFile*)file;
    char* out = (char*)buffer;
    int filled = 0;

    // A read at end of file fails here instead of returning zero bytes, so read only what is there.
    sqlite3_int64 have = self->size - offset;
    if (have < 0)
        have = 0;
    const int wanted = (sqlite3_int64)amount < have ? amount : (int)have;

    if (wanted > 0)
    {
        if (lseek(self->fd, (off_t)offset, SEEK_SET) != (off_t)offset)
            return PORT_SQLITE_FAIL("read seek", SQLITE_IOERR_READ);

        while (filled < wanted)
        {
            const ssize_t got = read(self->fd, out + filled, (size_t)(wanted - filled));
            if (got < 0)
                return PORT_SQLITE_FAIL("read", SQLITE_IOERR_READ);
            if (got == 0)
                break;
            filled += (int)got;
        }
    }

    if (filled < amount)
    {
        // SQLite treats the rest as zeroes, but only when told the read came up short.
        memset(out + filled, 0, (size_t)(amount - filled));
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int port_write_at(PortFile* self, const void* buffer, int amount, sqlite3_int64 offset)
{
    const char* in = (const char*)buffer;
    int written = 0;

    if (lseek(self->fd, (off_t)offset, SEEK_SET) != (off_t)offset)
        return PORT_SQLITE_FAIL("write seek", SQLITE_IOERR_WRITE);

    while (written < amount)
    {
        const ssize_t put = write(self->fd, in + written, (size_t)(amount - written));
        if (put <= 0)
            return PORT_SQLITE_FAIL("write", SQLITE_IOERR_WRITE);
        written += (int)put;
    }

    if (offset + amount > self->size)
        self->size = offset + amount;
    return SQLITE_OK;
}

static int port_write(sqlite3_file* file, const void* buffer, int amount, sqlite3_int64 offset)
{
    PortFile* self = (PortFile*)file;

    // Zero-fill any gap: a page SQLite has not written must read back as zeroes.
    if (offset > self->size)
    {
        static const char zeroes[512] = { 0 };
        sqlite3_int64 at = self->size;
        while (at < offset)
        {
            const sqlite3_int64 left = offset - at;
            const int chunk = left < (sqlite3_int64)sizeof zeroes ? (int)left : (int)sizeof zeroes;
            const int ret = port_write_at(self, zeroes, chunk, at);
            if (ret != SQLITE_OK)
                return ret;
            at += chunk;
        }
    }

    return port_write_at(self, buffer, amount, offset);
}

static int port_truncate(sqlite3_file* file, sqlite3_int64 size)
{
    PortFile* self = (PortFile*)file;
    if (ftruncate(self->fd, (off_t)size) != 0)
        return PORT_SQLITE_FAIL("truncate", SQLITE_IOERR_TRUNCATE);
    self->size = size;
    return SQLITE_OK;
}

static int port_sync(sqlite3_file* file, int flags)
{
    PortFile* self = (PortFile*)file;
    (void)flags;
    // Flush committed writes before the process can be closed from HOME.
    if (fsync(self->fd) == 0)
        return SQLITE_OK;
    return PORT_SQLITE_FAIL("fsync", SQLITE_IOERR_FSYNC);
}

static int port_file_size(sqlite3_file* file, sqlite3_int64* size)
{
    PortFile* self = (PortFile*)file;
    const off_t end = lseek(self->fd, 0, SEEK_END);
    if (end < 0)
    {
        *size = self->size;
        return SQLITE_OK;
    }
    self->size = (sqlite3_int64)end;
    *size = self->size;
    return SQLITE_OK;
}

// Requires one connection per database with serialized access. This VFS provides no file locking.
static int port_lock(sqlite3_file* file, int level)
{
    (void)file;
    (void)level;
    return SQLITE_OK;
}

static int port_unlock(sqlite3_file* file, int level)
{
    (void)file;
    (void)level;
    return SQLITE_OK;
}

static int port_check_reserved_lock(sqlite3_file* file, int* result)
{
    (void)file;
    *result = 0;
    return SQLITE_OK;
}

static int port_file_control(sqlite3_file* file, int op, void* arg)
{
    (void)file;
    (void)arg;
    return op == SQLITE_FCNTL_SIZE_HINT ? SQLITE_OK : SQLITE_NOTFOUND;
}

static int port_sector_size(sqlite3_file* file)
{
    (void)file;
    return 512;
}

static int port_device_characteristics(sqlite3_file* file)
{
    (void)file;
    return 0;
}

static const sqlite3_io_methods kPortIoMethods = {
    .iVersion = 1,
    .xClose = port_close,
    .xRead = port_read,
    .xWrite = port_write,
    .xTruncate = port_truncate,
    .xSync = port_sync,
    .xFileSize = port_file_size,
    .xLock = port_lock,
    .xUnlock = port_unlock,
    .xCheckReservedLock = port_check_reserved_lock,
    .xFileControl = port_file_control,
    .xSectorSize = port_sector_size,
    .xDeviceCharacteristics = port_device_characteristics,
};

static int port_open(sqlite3_vfs* vfs, const char* name, sqlite3_file* file, int flags, int* outFlags)
{
    PortFile* self = (PortFile*)file;
    int posixFlags = (flags & SQLITE_OPEN_READONLY) != 0 ? O_RDONLY : O_RDWR;

    (void)vfs;
    memset(self, 0, sizeof *self);
    self->base.pMethods = NULL;
    self->fd = -1;

    // Temp files are held in memory instead (SQLITE_TEMP_STORE=3).
    if (name == NULL)
        return SQLITE_CANTOPEN;

    if ((flags & SQLITE_OPEN_CREATE) != 0)
        posixFlags |= O_CREAT;
    if ((flags & SQLITE_OPEN_EXCLUSIVE) != 0)
        posixFlags |= O_EXCL;

    self->fd = open(name, posixFlags, 0666);
    if (self->fd < 0 && (posixFlags & O_RDWR) != 0 && (flags & SQLITE_OPEN_CREATE) == 0)
    {
        // Retry without write access for read-only databases.
        self->fd = open(name, O_RDONLY);
        if (self->fd >= 0 && outFlags != NULL)
            *outFlags = SQLITE_OPEN_READONLY;
    }
    if (self->fd < 0)
        return PORT_SQLITE_FAIL(name, SQLITE_CANTOPEN);

    const off_t end = lseek(self->fd, 0, SEEK_END);
    self->size = end < 0 ? 0 : (sqlite3_int64)end;
    self->base.pMethods = &kPortIoMethods;

    if (outFlags != NULL && *outFlags == 0)
        *outFlags = flags;
    return SQLITE_OK;
}

static int port_delete(sqlite3_vfs* vfs, const char* name, int syncDir)
{
    (void)vfs;
    (void)syncDir;
    if (remove(name) != 0)
    {
        struct stat info;
        if (stat(name, &info) != 0)
            return SQLITE_OK;
        return PORT_SQLITE_FAIL("delete", SQLITE_IOERR_DELETE);
    }
    return SQLITE_OK;
}

static int port_access(sqlite3_vfs* vfs, const char* name, int flags, int* result)
{
    struct stat info;

    (void)vfs;
    (void)flags;
    *result = stat(name, &info) == 0 ? 1 : 0;
    return SQLITE_OK;
}

static int port_full_pathname(sqlite3_vfs* vfs, const char* name, int outSize, char* out)
{
    (void)vfs;

    // Preserve absolute paths and libnx device paths such as romfs:/.
    if (name[0] == '/' || strchr(name, ':') != NULL)
    {
        sqlite3_snprintf(outSize, out, "%s", name);
        return SQLITE_OK;
    }

    char cwd[256];
    if (getcwd(cwd, sizeof cwd) == NULL)
        sqlite3_snprintf(outSize, out, "%s", name);
    else
        sqlite3_snprintf(outSize, out, "%s/%s", cwd, name);
    return SQLITE_OK;
}

static int port_randomness(sqlite3_vfs* vfs, int amount, char* out)
{
    (void)vfs;
    for (int i = 0; i < amount; i++)
        out[i] = (char)(rand() & 0xFF);
    return amount;
}

static int port_sleep(sqlite3_vfs* vfs, int microseconds)
{
    (void)vfs;
    usleep((useconds_t)microseconds);
    return microseconds;
}

static int port_current_time(sqlite3_vfs* vfs, double* out)
{
    (void)vfs;
    // Julian day number, as SQLite counts time.
    *out = 2440587.5 + (double)time(NULL) / 86400.0;
    return SQLITE_OK;
}

static int port_get_last_error(sqlite3_vfs* vfs, int size, char* out)
{
    (void)vfs;
    (void)size;
    (void)out;
    return SQLITE_OK;
}

static sqlite3_vfs s_vfs = {
    .iVersion = 1,
    .szOsFile = sizeof(PortFile),
    .mxPathname = 512,
    .zName = PORT_SQLITE_VFS_NAME,
    .xOpen = port_open,
    .xDelete = port_delete,
    .xAccess = port_access,
    .xFullPathname = port_full_pathname,
    .xRandomness = port_randomness,
    .xSleep = port_sleep,
    .xCurrentTime = port_current_time,
    .xGetLastError = port_get_last_error,
};

int port_sqlite_vfs_register(void)
{
    return sqlite3_vfs_register(&s_vfs, 1) == SQLITE_OK ? 0 : -1;
}
