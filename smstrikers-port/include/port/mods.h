// Mods, a folder per kind under mods/ beside the executable and in the user folder.

#ifndef PORT_MODS_H
#define PORT_MODS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The roots in load order; for the same file, a later root wins.
enum
{
    PORT_MODS_BESIDE_GAME,
    PORT_MODS_USER,
    PORT_MODS_ROOT_COUNT
};

// Writes <root>/mods/<kind> to `out`; 0 when the root is unknown or the path does not fit.
int PortModsFolder(int root, const char* userPath, const char* kind, char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif // PORT_MODS_H
