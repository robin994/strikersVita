// The real structs against the sizes bmd_endian.c asserts of its C mirrors.

#include <cstddef>

#include "NL/gl/glStateBundle.h"
#include "NL/gl/glUserData.h"

// Serialized BMD records remain packed; these are the decoded host layouts.
static_assert(sizeof(glStateBundle) == (sizeof(uintptr_t) == 4 ? 56 : 88),
              "glStateBundle size does not match the host pointer width");
static_assert(alignof(glStateBundle) >= 8,
              "glStateBundle must keep texturestate naturally aligned");
static_assert(offsetof(glStateBundle, texturestate) == 0,
              "glStateBundle texturestate offset changed");

static_assert(sizeof(glModelPacket) == (sizeof(uintptr_t) == 4 ? 80 : 128),
              "glModelPacket size does not match the host pointer width");
static_assert(alignof(glModelPacket) >= 8 && (sizeof(glModelPacket) % 8) == 0,
              "glModelPacket array stride must preserve texturestate alignment");
static_assert(offsetof(glModelPacket, state) == (sizeof(uintptr_t) == 4 ? 16 : 32),
              "glModelPacket state offset does not match PortPacket");

static_assert(sizeof(glModelStream) == (sizeof(uintptr_t) == 4 ? 12 : 16),
              "glModelStream size does not match the host pointer width");
