#ifndef _GLSTATEBUNDLE_H_
#define _GLSTATEBUNDLE_H_

#include "types.h"
#include <stdint.h>

// Runtime state is decoded from the packed GameCube record before use.  Keep
// the host copy naturally aligned: ARM emits LDRD/STRD for texturestate and a
// packed glModelPacket array would otherwise put every other u64 on a 2-byte
// boundary.
struct alignas(8) glStateBundle
{
    unsigned long long texturestate;
    u32 materialstate;
    u32 program;
    u32 raster;
    uintptr_t matrix;          // PORT: a GLMatrix address
    uintptr_t texture[6];      // PORT: id or PlatTexture*, see glx_GetTex
    unsigned char texconfig;
    unsigned char pad;
    u32 userStateKey;
};

struct gl_StateBitfield
{
    /* 0x00 */ s32 startBit;
    /* 0x04 */ s32 numBits;
}; // total size: 0x8

#endif // _GLSTATEBUNDLE_H_
