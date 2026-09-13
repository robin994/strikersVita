// The real structs against the sizes bmd_endian.c asserts of its C mirrors.

#include "NL/gl/glStateBundle.h"
#include "NL/gl/glUserData.h"

// texturestate 8 | materialstate, program, raster 4 | matrix + texture[6] ptrs | texconfig 1 | pad
// 1 | userStateKey 4
static_assert(sizeof(glStateBundle) == 8 + 4 + 4 + 4 + 1 + 1 + 4
                                          + 7 * sizeof(uintptr_t),
              "glStateBundle must match PortStateBundle in bmd_endian.c: "
              "matrix and texture[6] are addresses and must be pointer-width");
static_assert(sizeof(glStateBundle) == (sizeof(uintptr_t) == 4 ? 54 : 82),
              "glStateBundle size does not match the host pointer width");

// userData, indexBuffer, streams ptrs | numVertices 2 | primType 1 | numStreams 1 | state |
// materialset 4
static_assert(sizeof(glModelPacket) == 3 * sizeof(void*) + 2 + 1 + 1
                                           + sizeof(glStateBundle) + 4,
              "glModelPacket must match PortPacket in bmd_endian.c");
static_assert(sizeof(glModelPacket) == (sizeof(uintptr_t) == 4 ? 74 : 114),
              "glModelPacket size does not match the host pointer width");

// address ptr | id 1 | stride 1 | beData 1 | dataSize 4
static_assert(sizeof(glModelStream) == sizeof(uintptr_t) + 1 + 1 + 1 + 4,
              "glModelStream must match PortStream in bmd_endian.c: "
              "address is the vertex array and must be pointer-width");
static_assert(sizeof(glModelStream) == (sizeof(uintptr_t) == 4 ? 11 : 15),
              "glModelStream size does not match the host pointer width");
