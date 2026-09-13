#ifndef _GCSWIZZLER_H_
#define _GCSWIZZLER_H_

#include "types.h"
#include "NL/glx/glxTexture.h"

u32 GCTextureSize(eGXTextureFormat format, int width, int height, int numLevels, unsigned long texhandle);
// PORT: exact number of bytes occupied by a GameCube tiled texture on disc.
// Unlike GCTextureSize (kept for matching original game code), this accounts
// for GX block geometry/padding and is safe to use when validating .glt data.
u32 GCTextureEncodedSize(eGXTextureFormat format, int width, int height, int numLevels);
void GCSwizzle(void* pSwizzledData, const void* pLinearData, unsigned short width, unsigned short height, eGXTextureFormat format, bool bEndianSwap);

#endif // _GCSWIZZLER_H_
