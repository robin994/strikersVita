#include "Game/SAnim/AnimRetargeter.h"

#include <string.h>

#include "NL/nlMemory.h"
#include "NL/nlWare.h"
#include "dolphin/os.h"
#include "port/endian.h"

static inline AnimRetarget* GetAnimRetargetWithSignature_ARL(AnimRetargetList* list, const cSAnim* anim)
{
    intptr_t offset;
    AnimRetarget* p;
    AnimRetarget* result = NULL;
    offset = (intptr_t)result;

    for (long i = list->m_NumAnimRetargets; i > 0; i--)
    {
        p = (AnimRetarget*)((char*)list->m_pAnimRetarget + offset);
        if (anim->m_nHierarchySignature == p->m_TargetHierarchySignature)
        {
            result = p;
            break;
        }
        offset += sizeof(AnimRetarget);
    }

    return result;
}

static bool ARLReadNext(const u8** cursor, const u8* end, PortBEChunkView* view)
{
    if (!port_be_chunk_read(*cursor, end, view))
        return false;
    *cursor = view->next;
    return true;
}

/**
 * Offset/Address/Size: 0x48 | 0x801EFFD8 | size: 0x10C
 */
AnimRetargetList* AnimRetargetList::Initialize(nlChunk* chunkData)
{
    if (chunkData == NULL)
        return NULL;

    const u8* root = (const u8*)chunkData;
    const u32 rootSize = port_be32(root + 4);
    const u8* rootEnd = root + sizeof(nlChunk) + rootSize;
    PortBEChunkView rootView;
    if (!port_be_chunk_read(root, rootEnd, &rootView) || rootView.next != rootEnd ||
        !IsValidChunkID(rootView.id & 0x80FFFFFFu))
    {
        OSReport("Error: retarget root is not a well-formed BE chunk\n");
        return NULL;
    }

    const u8* cursor = root + sizeof(nlChunk);
    PortBEChunkView header;
    if (!ARLReadNext(&cursor, rootEnd, &header) || header.payload_len < 0x0C)
        return NULL;

    AnimRetargetList* data =
        (AnimRetargetList*)nlMalloc(sizeof(AnimRetargetList), 8, false);
    if (data == NULL)
        return NULL;
    memset(data, 0, sizeof(AnimRetargetList));
    data->m_uHashID = port_be32(header.payload + 0x04);
    data->m_NumAnimRetargets = (long)(s32)port_be32(header.payload + 0x08);
    if (data->m_NumAnimRetargets < 0 || data->m_NumAnimRetargets > 0x10000)
        return NULL;

    PortBEChunkView container;
    if (!ARLReadNext(&cursor, rootEnd, &container) ||
        (container.id & 0x80FFFFFFu) != 0x80017106u)
        return NULL;

    const u8* childCursor;
    const u8* childEnd;
    if (!port_be_chunk_children(&container, &childCursor, &childEnd))
        return NULL;

    PortBEChunkView records;
    if (!ARLReadNext(&childCursor, childEnd, &records) ||
        (records.id & 0x80FFFFFFu) != 0x17107u ||
        (unsigned long)data->m_NumAnimRetargets > records.payload_len / 0x0C)
        return NULL;

    const s32 n = (s32)data->m_NumAnimRetargets;
    data->m_pAnimRetarget = (AnimRetarget*)nlMalloc(
        (n > 0 ? n : 1) * sizeof(AnimRetarget), 8, false);
    if (data->m_pAnimRetarget == NULL)
        return NULL;
    for (s32 j = 0; j < n; ++j)
    {
        const u8* rec = records.payload + j * 0x0C;
        data->m_pAnimRetarget[j].m_TargetHierarchySignature = port_be32(rec + 0x00);
        data->m_pAnimRetarget[j].m_NumBones = (long)(s32)port_be32(rec + 0x04);
        data->m_pAnimRetarget[j].m_pMap = NULL;
        if (data->m_pAnimRetarget[j].m_NumBones < 0 ||
            data->m_pAnimRetarget[j].m_NumBones > 0x10000)
            return NULL;
    }

    for (s32 i = 0; i < n; ++i)
    {
        PortBEChunkView map;
        if (!ARLReadNext(&childCursor, childEnd, &map) ||
            (map.id & 0x80FFFFFFu) != 0x17108u)
            return NULL;
        const unsigned long nBones = (unsigned long)data->m_pAnimRetarget[i].m_NumBones;
        if (nBones > map.payload_len / sizeof(s16))
            return NULL;
        signed short* hostMap = (signed short*)nlMalloc(
            (nBones ? nBones : 1) * sizeof(s16), 8, false);
        if (hostMap == NULL)
            return NULL;
        for (unsigned long j = 0; j < nBones; ++j)
            hostMap[j] = (s16)port_be16(map.payload + j * sizeof(s16));
        data->m_pAnimRetarget[i].m_pMap = hostMap;
    }

    return data;
}

/**
 * Offset/Address/Size: 0x0 | 0x801EFF90 | size: 0x48
 */
AnimRetarget* AnimRetargetList::GetAnimRetargetWithSignature(const cSAnim* anim)
{
    return GetAnimRetargetWithSignature_ARL(this, anim);
}
