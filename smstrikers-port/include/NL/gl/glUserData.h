#ifndef _GLUSERDATA_H_
#define _GLUSERDATA_H_

#include "types.h"
#include "NL/gl/glStateBundle.h" // Include the new header

enum eGLUserData
{
    GLUD_Skin = 0,
    GLUD_Light = 1,
    GLUD_ShadowVolume = 2,
    GLUD_Specular = 3,
    GLUD_ShadowBuffer = 4,
    GLUD_Translucent = 5,
    GLUD_EnvDiffuse = 6,
    GLUD_ConstantColour = 7,
    GLUD_Ambient = 8,
    GLUD_Diffuse = 9,
    GLUD_DirectionalLight = 10,
    GLUD_Viewport = 11,
    GLUD_Scissor = 12,
    GLUD_NoRasterizedAlpha = 13,
    GLUD_MobileDiffuse = 14,
    GLUD_NoFog = 15,
    GLUD_CoPlanar = 16,
    GLUD_OnePassFresnel = 17,
    GLUD_Num = 18,
};

struct GLViewportUserData
{
    /* 0x00 */ u16 x;
    /* 0x02 */ u16 y;
    /* 0x04 */ u16 w;
    /* 0x06 */ u16 h;
    /* 0x08 */ uintptr_t view;         // PORT: a matrix handle
    /* 0x0C */ uintptr_t projection;   // PORT: a matrix handle
}; // total size: 0x10

struct glModelStream
{
    uintptr_t address;
    u8 id;
    u8 stride;
    // PORT: host-only. 1 = the array is big-endian, i.e. it lives inside a model file.
    u8 beData;
    // PORT: host-only. How many bytes of array there are at `address`, or 0 if nobody knows.
    u32 dataSize;
}; // serialized stream records are still 0x06 bytes; the host copy is aligned/padded

struct glModelPacket
{
    uintptr_t userData;
    uintptr_t indexBuffer;
    u16 numVertices;
    u8 primType;
    u8 numStreams;
    glModelStream* streams;
    glStateBundle state;
    u32 materialset;
}; // serialized packet records are still 0x4A bytes; see bmd_endian.c

bool glUserHasType(eGLUserData type, const glModelPacket* pPacket);
void glUserDetach(eGLUserData type, glModelPacket* pPacket);
void glUserDup(glModelPacket* pDest, const glModelPacket* pSrc, bool bPerm);
void glUserAttach(const void* pUserData, glModelPacket* pPacket, bool bPerm);
void* glUserGetData(const void* pUserData);
void* glUserAlloc(eGLUserData type, unsigned long size, bool bPerm);

#endif // _GLUSERDATA_H_
