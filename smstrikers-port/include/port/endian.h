#ifndef PORT_ENDIAN_H
#define PORT_ENDIAN_H
// Byte-swap helpers for GameCube assets. Functions over a pointer into the buffer rather than a
// BE<T> field wrapper, so sites convert one at a time.

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint16_t port_be16(const void* p)
{
    uint16_t v;
    memcpy(&v, p, sizeof v);      // memcpy, not a cast: the source may be
    return __builtin_bswap16(v);  // unaligned within a packed asset
}

static inline uint32_t port_be32(const void* p)
{
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return __builtin_bswap32(v);
}

static inline float port_bef32(const void* p)
{
    uint32_t v = port_be32(p);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static inline uint16_t port_u16_unaligned(const void* p)
{
    uint16_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

static inline uint32_t port_u32_unaligned(const void* p)
{
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

static inline float port_f32_unaligned(const void* p)
{
    float v;
    memcpy(&v, p, sizeof v);
    return v;
}

typedef struct PortBEChunkView
{
    const unsigned char* raw;
    const unsigned char* payload;
    const unsigned char* next;
    uint32_t id;
    uint32_t size;
    unsigned long payload_len;
} PortBEChunkView;

static inline void port_be16_array(void* p, unsigned long count)
{
    unsigned char* b = (unsigned char*)p;
    for (unsigned long i = 0; i < count; i++, b += 2)
    {
        uint16_t v = port_be16(b);
        memcpy(b, &v, sizeof v);
    }
}

static inline void port_be32_array(void* p, unsigned long count)
{
    unsigned char* b = (unsigned char*)p;
    for (unsigned long i = 0; i < count; i++, b += 4)
    {
        uint32_t v = port_be32(b);
        memcpy(b, &v, sizeof v);
    }
}

// An nlChunk's payload and the bytes of it inside the chunk: bits 24-30 of the id are an alignment
// exponent (nlChunk::GetAlignedData), the padding it costs is inside the chunk's size, and a chunk
// aligned past its own end, or with an exponent no asset uses (past 64 KB), yields NULL.
static inline const unsigned char* port_chunk_payload_const(const unsigned char* chunk,
                                                            uint32_t id, uint32_t size,
                                                            unsigned long* len)
{
    uintptr_t addr = (uintptr_t)(chunk + 8);
    uintptr_t end = addr + size;
    unsigned shift = (id & 0x7F000000u) >> 24;

    if (shift != 0)
    {
        uintptr_t alignment;
        if (shift > 16)
            return NULL;
        alignment = (uintptr_t)1 << shift;
        addr = (addr + alignment - 1) & ~(alignment - 1);
    }
    if (addr > end)
        return NULL;
    *len = (unsigned long)(end - addr);
    return (const unsigned char*)addr;
}

static inline unsigned char* port_chunk_payload(unsigned char* chunk, uint32_t id,
                                                uint32_t size, unsigned long* len)
{
    return (unsigned char*)port_chunk_payload_const(chunk, id, size, len);
}

static inline int port_be_chunk_read(const unsigned char* cursor,
                                     const unsigned char* end,
                                     PortBEChunkView* out)
{
    const unsigned char* payload;
    const unsigned char* chunk_end;
    uint32_t id;
    uint32_t size;
    unsigned long payload_len;

    if (cursor == NULL || end == NULL || out == NULL || cursor > end ||
        (unsigned long)(end - cursor) < 8)
        return 0;

    id = port_be32(cursor + 0);
    size = port_be32(cursor + 4);
    if (size > (uint32_t)(end - cursor - 8))
        return 0;

    chunk_end = cursor + 8 + size;
    payload = port_chunk_payload_const(cursor, id, size, &payload_len);
    if (payload == NULL || payload > chunk_end ||
        payload_len > (unsigned long)(chunk_end - payload))
        return 0;

    out->raw = cursor;
    out->payload = payload;
    out->next = chunk_end;
    out->id = id;
    out->size = size;
    out->payload_len = payload_len;
    return 1;
}

static inline int port_be_chunk_children(const PortBEChunkView* parent,
                                         const unsigned char** begin,
                                         const unsigned char** end)
{
    if (parent == NULL || parent->raw == NULL || begin == NULL || end == NULL)
        return 0;
    *begin = parent->raw + 8;
    *end = parent->next;
    return *begin <= *end;
}

#ifdef __cplusplus
}
#endif

#endif // PORT_ENDIAN_H
