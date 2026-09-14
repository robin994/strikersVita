// Immutable validator for the 0x8001B008 SKIN container embedded in BMD/GLG files.
// The GameCube bytes stay big-endian. glx_MakeSkinMesh decodes the format-defined
// fields into host-owned structures when a mesh is instantiated.

#include <stdint.h>

#include "port/endian.h"

#define SKIN_CHUNK_BONE_MATRICES 0x1B00A
#define SKIN_CHUNK_BONE_MAP      0x1B00B
#define SKIN_CHUNK_MORPHS        0x1B00C
#define SKIN_CHUNK_VERTICES      0x1B00D
#define SKIN_CHUNK_PAIRS         0x1B00E
#define SKIN_CHUNK_STITCHING     0x1B010

#define SKIN_MAX_MORPHS 8u
#define SKIN_MAX_SOFTWARE_VERTS 0x1000u
#define SKIN_MORPH_DELTA_SIZE 0x10u

static int skin_validate_payload(const uint8_t* data, uint32_t size, uint32_t type)
{
    switch (type)
    {
    case SKIN_CHUNK_BONE_MATRICES:
        return (size % 0x44u) == 0;

    case SKIN_CHUNK_BONE_MAP:
        return (size % 8u) == 0;

    case SKIN_CHUNK_VERTICES:
        return (size % 0x10u) == 0 && (size / 0x10u) <= SKIN_MAX_SOFTWARE_VERTS;

    case SKIN_CHUNK_PAIRS:
        return (size % 4u) == 0;

    case SKIN_CHUNK_STITCHING:
        if (size < 8)
            return 0;
        {
            const uint32_t packetIndex = port_be32(data + 0);
            const uint32_t numPackets = port_be32(data + 4);
            return numPackets != 0 && packetIndex < numPackets;
        }

    case SKIN_CHUNK_MORPHS:
    {
        uint64_t totalDeltas = 0;
        uint32_t i;

        if (size < 12)
            return 0;

        const uint32_t numMorphs = port_be32(data + 0);
        const uint32_t numBaseVerts = port_be32(data + 4);
        if (numMorphs > SKIN_MAX_MORPHS || numBaseVerts > SKIN_MAX_SOFTWARE_VERTS)
            return 0;

        const uint64_t arraysBytes = (uint64_t)numMorphs * 8u;
        const uint64_t deltaCountOffset = 8u + arraysBytes;
        if (deltaCountOffset + 4u > size)
            return 0;

        const uint8_t* morphCounts = data + 8u + (uint64_t)numMorphs * 4u;
        for (i = 0; i < numMorphs; ++i)
        {
            totalDeltas += port_be32(morphCounts + i * 4u);
            if (totalDeltas > UINT32_MAX)
                return 0;
        }

        const uint32_t deltaCount = port_be32(data + (uint32_t)deltaCountOffset);
        const uint64_t deltaBytes = (uint64_t)deltaCount * SKIN_MORPH_DELTA_SIZE;
        if (deltaBytes > (uint64_t)size - deltaCountOffset - 4u || totalDeltas > deltaCount)
            return 0;

        const uint8_t* deltas = data + (uint32_t)deltaCountOffset + 4u;
        for (i = 0; i < deltaCount; ++i)
        {
            const uint32_t index = port_be32(deltas + i * SKIN_MORPH_DELTA_SIZE + 0x0Cu);
            if (index >= numBaseVerts)
                return 0;
        }
        return 1;
    }

    default:
        // 0x1B009 and 0x1B00F are metadata ignored by the runtime. Unknown
        // children are skipped the same way the original loader skipped them.
        return 1;
    }
}

// Returns the number of validated chunks including the outer SKIN container.
// Zero means the blob must not be retained or parsed.
unsigned long port_skin_validate(const void* outerChunk, unsigned long totalSize)
{
    const uint8_t* base = (const uint8_t*)outerChunk;
    const uint8_t* cursor;
    const uint8_t* end;
    uint32_t outerId;
    uint32_t outerSize;
    unsigned long count = 1;

    if (base == NULL || totalSize < 8)
        return 0;

    outerId = port_be32(base + 0);
    outerSize = port_be32(base + 4);
    if ((outerId & ~0x7F000000u) != 0x8001B008u ||
        outerSize != totalSize - 8)
        return 0;

    cursor = base + 8;
    end = cursor + outerSize;
    while (cursor < end)
    {
        PortBEChunkView view;
        if (!port_be_chunk_read(cursor, end, &view))
            return 0;
        if (!skin_validate_payload(view.payload, (uint32_t)view.payload_len,
                                   view.id & ~0x7F000000u))
            return 0;
        cursor = view.next;
        count++;
    }

    return cursor == end ? count : 0;
}
