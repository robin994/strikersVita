// Bounds validator for serialized big-endian .cam files.
// Runtime loading decodes fields on demand in animcam.cpp; this helper deliberately
// never rewrites source bytes and exists to keep malformed-buffer cases unit-testable.

#include <stdint.h>

#include "port/endian.h"

#define CAM_CHUNK_ROOT       0x15501
#define CAM_CHUNK_KEY_COUNT  0x15508
#define CAM_CHUNK_CAMERA_POS 0x15509
#define CAM_CHUNK_TARGET_POS 0x1550C
#define CAM_CHUNK_CAMERA_ROT 0x15511
#define CAM_CHUNK_FOV        0x1550F
#define CAM_CHUNK_FOCAL_LEN  0x15510

static int cam_chunk_is_read(uint32_t type)
{
    return type == CAM_CHUNK_KEY_COUNT || type == CAM_CHUNK_CAMERA_POS
        || type == CAM_CHUNK_TARGET_POS || type == CAM_CHUNK_CAMERA_ROT
        || type == CAM_CHUNK_FOV || type == CAM_CHUNK_FOCAL_LEN;
}

static unsigned long cam_chunk_element(uint32_t type)
{
    switch (type)
    {
    case CAM_CHUNK_CAMERA_POS:
    case CAM_CHUNK_TARGET_POS: return 12;
    case CAM_CHUNK_CAMERA_ROT: return 16;
    case CAM_CHUNK_FOV:
    case CAM_CHUNK_FOCAL_LEN:  return 4;
    default:                   return 0;
    }
}

// Returns the number of bounded chunks, including the root. Zero means the
// buffer is not safe for the camera loader. Source bytes remain big-endian.
unsigned long port_cam_validate(const void* data, unsigned long size)
{
    const unsigned char* base = (const unsigned char*)data;
    const unsigned char* fileEnd;
    const unsigned char* cursor;
    const unsigned char* end;
    PortBEChunkView root;
    unsigned long n = 0;
    int haveCount = 0;
    uint32_t keyCount = 0;
    unsigned long pending[8][2];
    int nPending = 0;

    if (data == NULL || size < 8)
        return 0;

    fileEnd = base + size;
    if (!port_be_chunk_read(base, fileEnd, &root) ||
        (root.id & 0x00FFFFFFu) != CAM_CHUNK_ROOT)
        return 0;

    n++;
    cursor = root.raw + 8;
    end = root.next;
    while (cursor < end)
    {
        PortBEChunkView chunk;
        uint32_t type;

        if (!port_be_chunk_read(cursor, end, &chunk))
            return 0;
        cursor = chunk.next;
        n++;

        type = chunk.id & 0x00FFFFFFu;
        if (!cam_chunk_is_read(type))
            continue;

        if (type == CAM_CHUNK_KEY_COUNT)
        {
            if (chunk.payload_len < 4)
                return 0;
            keyCount = port_be32(chunk.payload);
            haveCount = 1;
        }
        else if (haveCount)
        {
            unsigned long element = cam_chunk_element(type);
            if (element == 0 || keyCount > chunk.payload_len / element)
                return 0;
        }
        else
        {
            if (nPending == 8)
                return 0;
            pending[nPending][0] = cam_chunk_element(type);
            pending[nPending][1] = chunk.payload_len;
            nPending++;
        }
    }

    if (!haveCount)
        return 0;
    while (nPending-- > 0)
    {
        if (pending[nPending][0] == 0 ||
            keyCount > pending[nPending][1] / pending[nPending][0])
            return 0;
    }

    return n;
}
