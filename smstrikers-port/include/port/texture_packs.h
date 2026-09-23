// Texture packs in Dolphin's naming, and the dumps to make them from.

#ifndef PORT_TEXTURE_PACKS_H
#define PORT_TEXTURE_PACKS_H

#include <dolphin/gx/GXStruct.h>

#ifdef __cplusplus
extern "C" {
#endif

// Registers mods/textures/ in each mods root, then STRIKERS_TEXTURES; later ones win.
void PortTexturesInit(const char* userPath);

// Look for the folders again and rescan them.
void PortTexturesReload(void);

// The folder at `index` and how many textures it registered, or NULL past the last.
const char* PortTexturesFolder(int index, int* registrations);

// Dumps go to <STRIKERS_TEXTURE_DUMP>/<disc id>/<disc file>/; 1 means texture_dumps/ in the user folder.
int PortTextureDumpEnabled(void);
void PortTextureDumpEnable(int on);
const char* PortTextureDumpDir(void);
unsigned PortTextureDumpWritten(void);

// The disc file the next textures come from, or NULL.
void PortTextureDumpFrom(const char* name);

// Async bundles: Expect ties the destination buffer to its file, FromBuffer looks it up at parse time.
void PortTextureDumpExpect(const void* buffer, const char* name);
void PortTextureDumpFromBuffer(const void* buffer);

// 1 before the port writes its own art into a disc texture, 0 after.
void PortTextureDumpSkip(int skip);

// The game made `obj`: on Switch its replacement starts loading, and with dumps on it is written as a PNG.
void PortTextureCreated(const GXTexObj* obj, const GXTlutObj* tlut);

#ifdef __cplusplus
}
#endif

#endif // PORT_TEXTURE_PACKS_H
