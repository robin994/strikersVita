// Byte-swap pass for a .glg SKIN chunk, whose payloads glx_MakeSkinMesh reads straight from file.

#include <stdint.h>
#include <string.h>

#include "port/endian.h"

#define SKIN_CHUNK_BONE_MATRICES 0x1B00A   // u32 bone id + 4x4 matrix, 0x44 each
#define SKIN_CHUNK_BONE_MAP      0x1B00B   // u32 key, u32 value
#define SKIN_CHUNK_MORPHS        0x1B00C   // counts, id/count arrays, MorphDelta[]
#define SKIN_CHUNK_VERTICES      0x1B00D   // SkinVertex, 0x10 each
#define SKIN_CHUNK_PAIRS         0x1B00E   // SkinPair, 0x4 each
#define SKIN_CHUNK_STITCHING     0x1B010   // two ints, then index bytes

static void skin_swap_payload(uint8_t* data, uint32_t size, uint32_t type)
{
    switch (type)
    {
    case SKIN_CHUNK_BONE_MATRICES:
    case SKIN_CHUNK_BONE_MAP:
    case SKIN_CHUNK_MORPHS:
        // 4-byte fields throughout.
        port_be32_array(data, size / 4);
        break;

    case SKIN_CHUNK_VERTICES:
    {
        // nlVector3, then four signed bytes with no byte order.
        uint32_t n = size / 0x10;
        uint32_t i;
        for (i = 0; i < n; i++)
            port_be32_array(data + i * 0x10, 3);
        break;
    }

    case SKIN_CHUNK_PAIRS:
        // A vertex index and a weight, both 16-bit.
        port_be16_array(data, size / 2);
        break;

    case SKIN_CHUNK_STITCHING:
        // A packet index and count; the indices that follow are single bytes.
        if (size >= 8)
            port_be32_array(data, 2);
        break;

    default:
        // 0x1B009 and 0x1B00F are skipped by the loader, so their payloads are never read.
        break;
    }
}

// Convert a copied raw big-endian 0x1B008 SKIN chunk into a retained host-order copy.
// The source .glg stays immutable; glx_MakeSkinMesh parses this copy with
// unaligned-safe host-order reads rather than relying on native struct alignment.
unsigned long port_skin_swap(void* outerChunk)
{
    uint8_t* base = (uint8_t*)outerChunk;
    uint32_t outerId;
    uint32_t outerSize;
    uint8_t* p;
    uint8_t* end;
    unsigned long n = 0;

    if (outerChunk == NULL)
        return 0;

    outerId = port_be32(base + 0);
    outerSize = port_be32(base + 4);
    if ((outerId & ~0x7F000000u) != 0x8001B008u)
        return 0;
    p = base + 8;
    end = p + outerSize;

    /* Publish the outer header in host order only in this private copy. */
    memcpy(base + 0, &outerId, 4);
    memcpy(base + 4, &outerSize, 4);

    while (p < end)
    {
        uint32_t id, size;
        uint8_t* data;
        unsigned long len;

        if ((unsigned long)(end - p) < 8)
            return 0;
        id = port_be32(p + 0);
        size = port_be32(p + 4);
        if (size > (uint32_t)(end - p - 8) || ((id & 0x7F000000u) >> 24) > 16)
            return 0;

        data = port_chunk_payload(p, id, size, &len);
        if (data == NULL)
            return 0;
        skin_swap_payload(data, (uint32_t)len, id & ~0x7F000000u);

        /* Publish only this private child header in host order. */
        memcpy(p + 0, &id, 4);
        memcpy(p + 4, &size, 4);
        n++;
        p += 8 + size;
    }

    return p == end ? n : 0;
}
