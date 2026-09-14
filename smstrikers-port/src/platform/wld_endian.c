// Bounds validators for serialized big-endian .wld / physics chunk streams.
// Runtime world loading decodes fields on demand in world.cpp and keeps source
// buffers immutable. These helpers exist only for malformed-buffer unit tests.

#include <stdint.h>

#include "port/endian.h"

#define WLD_CHUNK_ROOT      0x19000
#define WLD_CHUNK_OBJECT    0x19003
#define WLD_CHUNK_LIGHT     0x19005
#define WLD_CHUNK_EMITTER   0x19101
#define WLD_CHUNK_HELPER    0x19201
#define WLD_CHUNK_PHYSICS   0x1D000
#define WLD_CHUNK_PHYS_NUM  0x1D001
#define WLD_CHUNK_PHYS_ELEM 0x1D002
#define WLD_PHYS_ELEM_SIZE  0xA0

static int wld_record_size(uint32_t type, unsigned long* size)
{
    switch (type)
    {
    case WLD_CHUNK_OBJECT:  *size = 0xE0; return 1;
    case WLD_CHUNK_LIGHT:   *size = 0xA0; return 1;
    case WLD_CHUNK_EMITTER: *size = 0xA0; return 1;
    case WLD_CHUNK_HELPER:  *size = 0x80; return 1;
    default: return 0;
    }
}

static int wld_is_count_chunk(uint32_t type)
{
    return type == 0x19001 || type == 0x19002 || type == 0x19004
        || type == 0x19100 || type == 0x19200;
}

static unsigned long validate_physics_view(const PortBEChunkView* root)
{
    const unsigned char* cursor;
    unsigned long n = 1;
    int haveCount = 0;
    int haveElements = 0;
    uint32_t count = 0;
    unsigned long elementsLen = 0;

    if (root == NULL || (root->id & 0x00FFFFFFu) != WLD_CHUNK_PHYSICS)
        return 0;

    cursor = root->raw + 8;
    while (cursor < root->next)
    {
        PortBEChunkView child;
        uint32_t type;

        if (!port_be_chunk_read(cursor, root->next, &child))
            return 0;
        cursor = child.next;
        n++;
        type = child.id & 0x00FFFFFFu;

        if (type == WLD_CHUNK_PHYS_NUM)
        {
            if (child.payload_len < 4)
                return 0;
            count = port_be32(child.payload);
            if (count > 0x10000u)
                return 0;
            haveCount = 1;
        }
        else if (type == WLD_CHUNK_PHYS_ELEM)
        {
            elementsLen = child.payload_len;
            haveElements = 1;
        }
    }

    if (!haveCount || !haveElements || count > elementsLen / WLD_PHYS_ELEM_SIZE)
        return 0;
    return n;
}

unsigned long port_phys_validate(const void* data, unsigned long size)
{
    const unsigned char* begin = (const unsigned char*)data;
    PortBEChunkView root;

    if (data == NULL || size < 8)
        return 0;
    if (!port_be_chunk_read(begin, begin + size, &root) ||
        (root.id & 0x00FFFFFFu) != WLD_CHUNK_PHYSICS)
        return 0;
    return validate_physics_view(&root);
}

// Returns the number of bounded chunks, including nested physics children.
// Zero means the file is unsafe for the immutable world loader.
unsigned long port_wld_validate(const void* data, unsigned long size)
{
    const unsigned char* begin = (const unsigned char*)data;
    PortBEChunkView root;
    const unsigned char* cursor;
    unsigned long n = 1;

    if (data == NULL || size < 8)
        return 0;
    if (!port_be_chunk_read(begin, begin + size, &root) ||
        (root.id & 0x00FFFFFFu) != WLD_CHUNK_ROOT)
        return 0;

    cursor = root.raw + 8;
    while (cursor < root.next)
    {
        PortBEChunkView child;
        uint32_t type;
        unsigned long expectedSize;

        if (!port_be_chunk_read(cursor, root.next, &child))
            return 0;
        cursor = child.next;
        n++;
        type = child.id & 0x00FFFFFFu;

        if (type == WLD_CHUNK_PHYSICS)
        {
            unsigned long physicsChunks = validate_physics_view(&child);
            if (physicsChunks == 0)
                return 0;
            n += physicsChunks - 1;
        }
        else if (wld_record_size(type, &expectedSize))
        {
            if (child.payload_len != expectedSize)
                return 0;
        }
        else if (wld_is_count_chunk(type))
        {
            if (child.payload_len < 4)
                return 0;
        }
    }

    return n;
}
