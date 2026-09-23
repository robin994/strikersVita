#include "port/mods.h"

#include "port/host.h"

#include <stdio.h>
#include <string.h>

int PortModsFolder(int root, const char* userPath, const char* kind, char* out, size_t size)
{
    char base[1024];
    size_t n;
    int written;

    if (root == PORT_MODS_BESIDE_GAME)
    {
        if (port_executable_dir(base, sizeof base) != 0)
            return 0;
    }
    else if (root == PORT_MODS_USER && userPath != NULL && *userPath != '\0')
    {
        if (snprintf(base, sizeof base, "%s", userPath) >= (int)sizeof base)
            return 0;
    }
    else
    {
        return 0;
    }

    n = strlen(base);
    while (n > 0 && (base[n - 1] == '/' || base[n - 1] == '\\'))
        base[--n] = '\0';
    written = snprintf(out, size, "%s/mods/%s", base, kind);
    return written > 0 && (size_t)written < size;
}
