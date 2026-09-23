// POSIX calls the SQLite VFS needs that libnx lacks. Horizon has no users or file owners.

#include <sys/types.h>
#include <unistd.h>

// Weak, so a driver archive that exports its own takes precedence.
__attribute__((weak)) uid_t geteuid(void)
{
    return 0;
}

int fchown(int fd, uid_t owner, gid_t group)
{
    (void)fd;
    (void)owner;
    (void)group;
    return 0;
}
