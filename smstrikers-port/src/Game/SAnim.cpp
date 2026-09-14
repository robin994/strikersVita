#include "Game/SAnim.h"
#include "port/morphwatch.h"
#include <stdlib.h>
extern "C" int port_region_owns(const void*);
#include "NL/nlWare.h"
#include "dolphin/os.h"
#include "port/endian.h"
#include "Game/PoseAccumulator.h"

#include "NL/nlMemory.h"
#include "NL/nlList.h"

#pragma inline_depth(8)
#pragma inline_max_size(0x10000)
#pragma inline_max_total_size(0x10000)

static bool SAnimReadNext(const u8** cursor, const u8* end, PortBEChunkView* view)
{
    if (!port_be_chunk_read(*cursor, end, view)) return false;
    *cursor = view->next;
    return true;
}

#pragma inline_depth(8)
#pragma inline_max_size(0x10000)
#pragma inline_max_total_size(0x10000)
/**
 * Offset/Address/Size: 0xD40 | 0x801E9F54 | size: 0x68C
 */
cSAnim* cSAnim::Initialize(nlChunk* pChunk)
{
    if (pChunk == NULL)
        return NULL;

    const u8* root = (const u8*)pChunk;
    const u32 rootSize = port_be32(root + 4);
    const u8* rootEnd = root + sizeof(nlChunk) + rootSize;
    PortBEChunkView rootView;
    if (!port_be_chunk_read(root, rootEnd, &rootView) || rootView.next != rootEnd ||
        !IsValidChunkID(rootView.id & 0x80FFFFFFu))
    {
        OSReport("Error: SANIM root is not a well-formed BE chunk\n");
        return NULL;
    }

    const u8* cursor = root + sizeof(nlChunk);
    PortBEChunkView chunk;
    if (!SAnimReadNext(&cursor, rootEnd, &chunk) || chunk.payload_len < 0x48)
    {
        OSReport("Error: truncated SANIM header\n");
        return NULL;
    }

    cSAnim* pRetval = (cSAnim*)nlMalloc(sizeof(cSAnim), 8, false);
    if (pRetval == NULL)
        return NULL;
    memset(pRetval, 0, sizeof(cSAnim));
    {
        const u8* disc = chunk.payload;
        pRetval->m_uHashID = port_be32(disc + 0x04);
        pRetval->m_nNumKeys = port_be32(disc + 0x08);
        pRetval->m_nNumNodes = port_be32(disc + 0x0C);
        pRetval->m_nNumMorphChannels = port_be32(disc + 0x10);
        pRetval->m_nNumRootKeys = port_be32(disc + 0x24);
        pRetval->m_fLinearSpeed = port_bef32(disc + 0x40);
        pRetval->m_nHierarchySignature = port_be32(disc + 0x44);
    }
    if (pRetval->m_nNumNodes > 0x10000u || pRetval->m_nNumKeys > 0x100000u ||
        pRetval->m_nNumMorphChannels > 0x10000u || pRetval->m_nNumRootKeys > 0x100000u)
    {
        OSReport("Error: unreasonable SANIM counts nodes=%lu keys=%lu morphs=%lu roots=%lu\n",
                 (unsigned long)pRetval->m_nNumNodes,
                 (unsigned long)pRetval->m_nNumKeys,
                 (unsigned long)pRetval->m_nNumMorphChannels,
                 (unsigned long)pRetval->m_nNumRootKeys);
        return NULL;
    }

    if (!SAnimReadNext(&cursor, rootEnd, &chunk) || chunk.payload_len == 0 ||
        memchr(chunk.payload, '\0', chunk.payload_len) == NULL)
    {
        OSReport("Error: SANIM name chunk is missing/truncated\n");
        return NULL;
    }
    pRetval->m_szName = (const char*)chunk.payload;

    /* These three serialized 4-byte pointer tables are placeholders on GC.
       Vita rebuilds native pointer arrays instead of overlaying them. */
    PortBEChunkView rotTable, transTable, scaleTable;
    if (!SAnimReadNext(&cursor, rootEnd, &rotTable) ||
        !SAnimReadNext(&cursor, rootEnd, &transTable) ||
        !SAnimReadNext(&cursor, rootEnd, &scaleTable) ||
        pRetval->m_nNumNodes > rotTable.payload_len / 4 ||
        pRetval->m_nNumNodes > transTable.payload_len / 4 ||
        pRetval->m_nNumNodes > scaleTable.payload_len / 4)
    {
        OSReport("Error: SANIM node pointer tables are truncated\n");
        return NULL;
    }

    const u32 nNodes = pRetval->m_nNumNodes;
    pRetval->m_pRotKeys = nlMalloc((nNodes ? nNodes : 1) * sizeof(void*), 8, false);
    pRetval->m_pTransKeys = (PackedTrans**)nlMalloc((nNodes ? nNodes : 1) * sizeof(void*), 8, false);
    pRetval->m_pScaleKeys = (PackedScale**)nlMalloc((nNodes ? nNodes : 1) * sizeof(void*), 8, false);
    if (pRetval->m_pRotKeys == NULL || pRetval->m_pTransKeys == NULL ||
        pRetval->m_pScaleKeys == NULL)
        return NULL;
    memset(pRetval->m_pRotKeys, 0, nNodes * sizeof(void*));
    memset(pRetval->m_pTransKeys, 0, nNodes * sizeof(void*));
    memset(pRetval->m_pScaleKeys, 0, nNodes * sizeof(void*));

    if (!SAnimReadNext(&cursor, rootEnd, &chunk) ||
        pRetval->m_nNumRootKeys > chunk.payload_len / sizeof(u16))
    {
        OSReport("Error: SANIM root rotation table is truncated\n");
        return NULL;
    }
    if (pRetval->m_nNumRootKeys != 0)
    {
        pRetval->m_pRootRot = (unsigned short*)nlMalloc(
            pRetval->m_nNumRootKeys * sizeof(unsigned short), 8, false);
        if (pRetval->m_pRootRot == NULL)
            return NULL;
        for (u32 k = 0; k < pRetval->m_nNumRootKeys; k++)
            pRetval->m_pRootRot[k] = port_be16(chunk.payload + k * sizeof(u16));
    }

    if (!SAnimReadNext(&cursor, rootEnd, &chunk) ||
        pRetval->m_nNumRootKeys > chunk.payload_len / sizeof(nlVector3))
    {
        OSReport("Error: SANIM root translation table is truncated\n");
        return NULL;
    }
    if (pRetval->m_nNumRootKeys != 0)
    {
        pRetval->m_pRootTrans = (nlVector3*)nlMalloc(
            pRetval->m_nNumRootKeys * sizeof(nlVector3), 8, false);
        if (pRetval->m_pRootTrans == NULL)
            return NULL;
        for (u32 k = 0; k < pRetval->m_nNumRootKeys; k++)
        {
            const u8* v = chunk.payload + k * sizeof(nlVector3);
            pRetval->m_pRootTrans[k].x = port_bef32(v + 0);
            pRetval->m_pRootTrans[k].y = port_bef32(v + 4);
            pRetval->m_pRootTrans[k].z = port_bef32(v + 8);
        }
    }

    u32 nodeIndex = 0;
    PortBEChunkView nodeChunk;
    while (cursor < rootEnd)
    {
        const u8* nodeRaw = cursor;
        if (!SAnimReadNext(&cursor, rootEnd, &nodeChunk))
        {
            OSReport("Error: SANIM node/morph chunk is truncated\n");
            return NULL;
        }
        const u32 type = nodeChunk.id & 0x80FFFFFFu;
        if (type != 0x80017100u && type != 0x1001u)
        {
            cursor = nodeRaw;
            break;
        }
        if (type == 0x1001u)
            continue;
        if (nodeIndex >= nNodes)
        {
            OSReport("Error: SANIM has more node chunks than declared\n");
            return NULL;
        }

        const u8* subCursor;
        const u8* subEnd;
        if (!port_be_chunk_children(&nodeChunk, &subCursor, &subEnd))
            return NULL;
        while (subCursor < subEnd)
        {
            PortBEChunkView sub;
            if (!SAnimReadNext(&subCursor, subEnd, &sub))
                return NULL;
            const u32 subType = sub.id & 0x80FFFFFFu;
            const unsigned long bytes = sub.payload_len;
            if (subType == 0x17101u)
            {
                if ((bytes & 1u) != 0) return NULL;
                u8* host = (u8*)nlMalloc(bytes ? bytes : 2, 8, false);
                if (host == NULL) return NULL;
                for (unsigned long o = 0; o < bytes; o += 2)
                {
                    const u16 v = port_be16(sub.payload + o);
                    memcpy(host + o, &v, sizeof v);
                }
                ((void**)pRetval->m_pRotKeys)[nodeIndex] = host;
            }
            else if (subType == 0x17102u)
            {
                if ((bytes & 3u) != 0) return NULL;
                u8* host = (u8*)nlMalloc(bytes ? bytes : 4, 8, false);
                if (host == NULL) return NULL;
                for (unsigned long o = 0; o < bytes; o += 4)
                {
                    const u32 v = port_be32(sub.payload + o);
                    memcpy(host + o, &v, sizeof v);
                }
                pRetval->m_pTransKeys[nodeIndex] = (PackedTrans*)host;
            }
            else if (subType == 0x17103u)
            {
                if ((bytes & 1u) != 0) return NULL;
                u8* host = (u8*)nlMalloc(bytes ? bytes : 2, 8, false);
                if (host == NULL) return NULL;
                for (unsigned long o = 0; o < bytes; o += 2)
                {
                    const u16 v = port_be16(sub.payload + o);
                    memcpy(host + o, &v, sizeof v);
                }
                pRetval->m_pScaleKeys[nodeIndex] = (PackedScale*)host;
            }
        }
        nodeIndex++;
    }
    if (nodeIndex != nNodes)
    {
        OSReport("Error: SANIM declared %lu node(s) but serialized %lu\n",
                 (unsigned long)nNodes, (unsigned long)nodeIndex);
        return NULL;
    }

    if (pRetval->m_pRootTrans != NULL)
    {
        nlVector3 v3PosStart;
        nlVector3 v3PosEnd;
        pRetval->GetRootTrans(0.0f, &v3PosStart);
        pRetval->GetRootTrans(1.0f, &v3PosEnd);
        float dist = nlSqrt(nlGetLengthSquared3D(
            v3PosEnd.x - v3PosStart.x, v3PosEnd.y - v3PosStart.y,
            v3PosEnd.z - v3PosStart.z), true);
        pRetval->m_fLinearSpeed = dist / ((float)pRetval->m_nNumKeys / 30.0f);
    }
    else
    {
        pRetval->m_fLinearSpeed = 0.0f;
    }

    if (!SAnimReadNext(&cursor, rootEnd, &nodeChunk) ||
        pRetval->m_nNumMorphChannels > nodeChunk.payload_len / 4)
        return NULL;
    u32* counts = (u32*)nlMalloc(
        (pRetval->m_nNumMorphChannels ? pRetval->m_nNumMorphChannels : 1) * sizeof(u32), 8, false);
    if (counts == NULL) return NULL;
    for (u32 i = 0; i < pRetval->m_nNumMorphChannels; i++)
        counts[i] = port_be32(nodeChunk.payload + i * 4);
    pRetval->m_pNumMorphKeys = counts;

    if (!SAnimReadNext(&cursor, rootEnd, &nodeChunk) ||
        pRetval->m_nNumMorphChannels > nodeChunk.payload_len / 4)
        return NULL;
    u32* ids = (u32*)nlMalloc(
        (pRetval->m_nNumMorphChannels ? pRetval->m_nNumMorphChannels : 1) * sizeof(u32), 8, false);
    if (ids == NULL) return NULL;
    for (u32 i = 0; i < pRetval->m_nNumMorphChannels; i++)
        ids[i] = port_be32(nodeChunk.payload + i * 4);
    pRetval->m_nMorphIds = ids;

    if (!SAnimReadNext(&cursor, rootEnd, &nodeChunk))
        return NULL;
    unsigned long required = 0;
    for (u32 i = 0; i < pRetval->m_nNumMorphChannels; i++)
    {
        const u32 count = pRetval->m_pNumMorphKeys[i];
        if (required > nodeChunk.payload_len || count > nodeChunk.payload_len - required)
            return NULL;
        required += count;
    }
    pRetval->m_pMorphKeys = (unsigned char*)nodeChunk.payload;

    if (!SAnimReadNext(&cursor, rootEnd, &nodeChunk) || nNodes > nodeChunk.payload_len / 4)
        return NULL;
    u32* props = (u32*)nlMalloc((nNodes ? nNodes : 1) * sizeof(u32), 8, false);
    if (props == NULL) return NULL;
    for (u32 i = 0; i < nNodes; i++)
        props[i] = port_be32(nodeChunk.payload + i * 4);
    pRetval->m_pNodeProperties = props;

    PortMorphWatchRegister(pRetval, &pRetval->m_pNumMorphKeys, "m_pNumMorphKeys");
    PortMorphWatchRegister(pRetval, &pRetval->m_pMorphKeys, "m_pMorphKeys");
    PortMorphWatchRegister(pRetval, &pRetval->m_nMorphIds, "m_nMorphIds");
    return pRetval;
}
#pragma inline_depth()

/**
 * Offset/Address/Size: 0x91C | 0x801E9B30 | size: 0x424
 */
void cSAnim::BlendRot(int nodeIndex, int remappedNodeIndex, float tNorm, float weight, cPoseAccumulator* acc, bool additive) const
{
    if (remappedNodeIndex < 0 || (unsigned int)remappedNodeIndex >= m_nNumNodes ||
        m_pRotKeys == NULL || m_pNodeProperties == NULL || m_nNumKeys == 0)
    {
        acc->BlendRotIdentity(nodeIndex, weight);
        return;
    }

    void* pRawKeys = ((void**)m_pRotKeys)[remappedNodeIndex];
    if (pRawKeys != NULL)
    {
        unsigned int props = m_pNodeProperties[remappedNodeIndex];

        if (props & 0x2)
        {
            if (props & 0x1)
            {
                acc->BlendRotAroundZ(nodeIndex, ((unsigned short*)pRawKeys)[0], weight);
                return;
            }

            nlQuaternion q;
            q.x = 0.000061035156f * ((signed short*)pRawKeys)[0];
            q.y = 0.000061035156f * ((signed short*)pRawKeys)[1];
            q.z = 0.000061035156f * ((signed short*)pRawKeys)[2];
            q.w = 0.000061035156f * ((signed short*)pRawKeys)[3];
            acc->BlendRot(nodeIndex, &q, weight, additive);
            return;
        }

        if (1.0f == tNorm)
        {
            int lastIndex = m_nNumKeys - 1;

            if (props & 0x1)
            {
                acc->BlendRotAroundZ(nodeIndex, ((unsigned short*)pRawKeys)[lastIndex], weight);
                return;
            }

            signed short* pLast = ((signed short*)pRawKeys) + (lastIndex * 4);
            nlQuaternion q;
            q.x = 0.000061035156f * pLast[0];
            q.y = 0.000061035156f * pLast[1];
            q.z = 0.000061035156f * pLast[2];
            q.w = 0.000061035156f * pLast[3];
            acc->BlendRot(nodeIndex, &q, weight, additive);
            return;
        }

        float fRealIndex = tNorm * (m_nNumKeys - 1);
        int nKeyIndex = (int)fRealIndex;
        float fFrac = fRealIndex - nKeyIndex;
        float fWeight2 = weight * fFrac;
        float fWeight1 = weight - fWeight2;

        if (props & 0x1)
        {
            unsigned short* pKeys = (unsigned short*)pRawKeys;
            acc->BlendRotAroundZ(nodeIndex, pKeys[nKeyIndex], fWeight1);
        }
        else
        {
            signed short* pKey = ((signed short*)pRawKeys) + (nKeyIndex * 4);
            nlQuaternion q1;
            q1.x = 0.000061035156f * pKey[0];
            q1.y = 0.000061035156f * pKey[1];
            q1.z = 0.000061035156f * pKey[2];
            q1.w = 0.000061035156f * pKey[3];
            acc->BlendRot(nodeIndex, &q1, fWeight1, additive);
        }

        if (m_pNodeProperties[remappedNodeIndex] & 0x1)
        {
            unsigned short* pKeys = (unsigned short*)(((void**)m_pRotKeys)[remappedNodeIndex]);
            unsigned short* pKey = &pKeys[nKeyIndex];
            acc->BlendRotAroundZ(nodeIndex, pKey[1], fWeight2);
            return;
        }

        signed short* pKey = ((signed short*)(((void**)m_pRotKeys)[remappedNodeIndex])) + ((nKeyIndex + 1) * 4);
        nlQuaternion q2;
        q2.x = 0.000061035156f * pKey[0];
        q2.y = 0.000061035156f * pKey[1];
        q2.z = 0.000061035156f * pKey[2];
        q2.w = 0.000061035156f * pKey[3];
        acc->BlendRot(nodeIndex, &q2, fWeight2, additive);
        return;
    }

    acc->BlendRotIdentity(nodeIndex, weight);
}

/**
 * Offset/Address/Size: 0x608 | 0x801E981C | size: 0x314
 */
void cSAnim::BlendScale(int nodeIndex, int remappedNodeIndex, float tNorm, float weight, cPoseAccumulator* acc, bool additive) const
{
    if (remappedNodeIndex < 0 || (unsigned int)remappedNodeIndex >= m_nNumNodes ||
        m_pScaleKeys == NULL || m_pNodeProperties == NULL || m_nNumKeys == 0)
    {
        acc->BlendScaleIdentity(nodeIndex, weight);
        return;
    }

    PackedScale* pKeys = m_pScaleKeys[remappedNodeIndex];
    if (pKeys != NULL)
    {
        if (m_pNodeProperties[remappedNodeIndex] & 0x8)
        {
            nlVector3 v;
            v.x = 0.000244140625f * pKeys[0].x;
            v.y = 0.000244140625f * pKeys[0].y;
            v.z = 0.000244140625f * pKeys[0].z;
            acc->BlendScale(nodeIndex, &v, weight, additive);
            return;
        }

        if (1.0f == tNorm)
        {
            PackedScale* pLastKey = &pKeys[m_nNumKeys - 1];
            nlVector3 v;
            v.x = 0.000244140625f * pLastKey->x;
            v.y = 0.000244140625f * pLastKey->y;
            v.z = 0.000244140625f * pLastKey->z;
            acc->BlendScale(nodeIndex, &v, weight, additive);
            return;
        }

        float fRealIndex = tNorm * (m_nNumKeys - 1);
        int nKeyIndex = (int)fRealIndex;
        float fFrac = fRealIndex - nKeyIndex;
        float fWeight2 = weight * fFrac;
        float fWeight1 = weight - fWeight2;

        PackedScale* pKey = &pKeys[nKeyIndex];
        nlVector3 v1;
        v1.x = 0.000244140625f * pKey->x;
        v1.y = 0.000244140625f * pKey->y;
        v1.z = 0.000244140625f * pKey->z;
        acc->BlendScale(nodeIndex, &v1, fWeight1, additive);

        PackedScale* pNextKey = &m_pScaleKeys[remappedNodeIndex][nKeyIndex + 1];
        nlVector3 v2;
        v2.x = 0.000244140625f * pNextKey->x;
        v2.y = 0.000244140625f * pNextKey->y;
        v2.z = 0.000244140625f * pNextKey->z;
        acc->BlendScale(nodeIndex, &v2, fWeight2, additive);
    }
    else
    {
        acc->BlendScaleIdentity(nodeIndex, weight);
    }
}

/**
 * Offset/Address/Size: 0x404 | 0x801E9618 | size: 0x204
 */
void cSAnim::BlendTrans(int nAccumulatorNode, int nSAnimNode, float fTime, float fWeight, cPoseAccumulator* pAccumulator, bool bMirror) const
{
    if (pAccumulator->m_BaseSHierarchy->PreserveBoneLength(nAccumulatorNode))
    {
        return;
    }

    if (nSAnimNode < 0 || (unsigned int)nSAnimNode >= m_nNumNodes ||
        m_pTransKeys == NULL || m_pNodeProperties == NULL || m_nNumKeys == 0)
    {
        pAccumulator->BlendTransIdentity(nAccumulatorNode, fWeight);
        return;
    }

    PackedTrans* pKeys = m_pTransKeys[nSAnimNode];
    if (pKeys != NULL)
    {
        if (m_pNodeProperties[nSAnimNode] & 0x4)
        {
            nlVector3 v;
            v.x = pKeys[0].x;
            v.y = pKeys[0].y;
            v.z = pKeys[0].z;
            pAccumulator->BlendTrans(nAccumulatorNode, &v, fWeight, bMirror);
            return;
        }

        if (1.0f == fTime)
        {
            PackedTrans* pLastKey = &pKeys[m_nNumKeys - 1];
            nlVector3 v;
            v.x = pLastKey->x;
            v.y = pLastKey->y;
            v.z = pLastKey->z;
            pAccumulator->BlendTrans(nAccumulatorNode, &v, fWeight, bMirror);
            return;
        }

        int nKeyIndex = (int)(fTime * (float)(m_nNumKeys - 1));
        float fFrac = fTime * (float)(m_nNumKeys - 1) - (float)nKeyIndex;
        float fWeight2 = fWeight * fFrac;
        float fWeight1 = fWeight - fWeight2;

        PackedTrans* pKey = &pKeys[nKeyIndex];
        nlVector3 v1;
        v1.x = pKey->x;
        v1.y = pKey->y;
        v1.z = pKey->z;
        pAccumulator->BlendTrans(nAccumulatorNode, &v1, fWeight1, bMirror);

        PackedTrans* pNextKey = &m_pTransKeys[nSAnimNode][nKeyIndex + 1];
        nlVector3 v2;
        v2.x = pNextKey->x;
        v2.y = pNextKey->y;
        v2.z = pNextKey->z;
        pAccumulator->BlendTrans(nAccumulatorNode, &v2, fWeight2, bMirror);
    }
    else
    {
        pAccumulator->BlendTransIdentity(nAccumulatorNode, fWeight);
    }
}

/**
 * Offset/Address/Size: 0x3CC | 0x801E95E0 | size: 0x38
 */
void cSAnim::Destroy()
{
    nlDeleteList<cSAnimCallback>(&m_pCallbackList);
    m_pCallbackList = 0;
}

// The by-value inline return gives MWCC the target signed-conversion slot order.
static inline s16 SAnimRootDiffIdentity(s16 value)
{
    return value;
}

/**
 * Offset/Address/Size: 0x2EC | 0x801E9500 | size: 0xE0
 */
void cSAnim::GetRootRot(float fTime, unsigned short* pRootRot) const
{
    float fRealIndex;
    int nIndex;

    if (m_nNumRootKeys != 0)
    {
        if (fTime == 1.0f || m_nNumRootKeys == 1)
        {
            *pRootRot = m_pRootRot[m_nNumRootKeys - 1];
            return;
        }

        fRealIndex = fTime * (m_nNumRootKeys - 1);
        nIndex = (int)fRealIndex;
        unsigned short* pRoots = m_pRootRot;
        unsigned short val0 = pRoots[nIndex];
        s16 diff = (s16)(pRoots[nIndex + 1] - val0);
        *pRootRot = val0 + (int)((fRealIndex - (float)nIndex) * SAnimRootDiffIdentity(diff));
        return;
    }
    *pRootRot = 0;
}

/**
 * Offset/Address/Size: 0x1E0 | 0x801E93F4 | size: 0x10C
 */
void cSAnim::GetRootTrans(float t, nlVector3* out) const
{
    if (m_nNumRootKeys != 0)
    {
        if (t == 1.0f || m_nNumRootKeys == 1)
        {
            *out = m_pRootTrans[m_nNumRootKeys - 1];
            return;
        }

        float fRealIndex = t * (m_nNumRootKeys - 1);
        int nIndex = (int)fRealIndex;
        float fWeight = fRealIndex - nIndex;
        nlVec3WeightedSum(*out, 1.0f - fWeight, m_pRootTrans[nIndex], fWeight, m_pRootTrans[nIndex + 1]);

        return;
    }
    out->x = 0.0f;
    out->y = 0.0f;
    out->z = 0.0f;
}

/**
 * Offset/Address/Size: 0x160 | 0x801E9374 | size: 0x80
 */
void cSAnim::CreateCallback(float fTime, uintptr_t nParam1, void (*funcCallback)(uintptr_t))
{
    cSAnimCallback* pCallback;
    pCallback = (cSAnimCallback*)nlMalloc(sizeof(cSAnimCallback), 8, 0);

    if (pCallback != NULL)
    {
        pCallback->m_fTime = fTime;
        pCallback->m_nParam1 = nParam1;
        pCallback->m_funcCallback = funcCallback;
    }

    nlListAddStart<cSAnimCallback>(&m_pCallbackList, pCallback, NULL);
}

/**
 * Offset/Address/Size: 0x0 | 0x801E9214 | size: 0x160
 */
float cSAnim::GetMorphWeight(int channel, float fTime) const
{
    // PORT: Checked before any dereference: the read that faults is m_pNumMorphKeys[channel] itself.
    if (!port_region_owns(m_pNumMorphKeys) || !port_region_owns(m_pMorphKeys)
        || channel < 0 || (unsigned int)channel >= m_nNumMorphChannels)
    {
        static int nMorphLog = 0;
        if (nMorphLog++ < 8)
        {
            OSReport("[morph] BAD ch=%d nch=%u counts=%p(%d) keys=%p(%d) this=%p\n",
                     channel, (unsigned int)m_nNumMorphChannels,
                     (const void*)m_pNumMorphKeys, port_region_owns(m_pNumMorphKeys),
                     (const void*)m_pMorphKeys, port_region_owns(m_pMorphKeys),
                     (const void*)this);
        }
        return 0.0f;
    }

    const u8* keys = m_pMorphKeys;
    int numKeys = m_pNumMorphKeys[channel];
    if (numKeys <= 0)
        return 0.0f;
    int i = 0;

    for (i = 0; i < channel; i++)
    {
        keys += m_pNumMorphKeys[i];
    }

    if (numKeys == 1 || fTime == 1.0f)
    {
        float weight = (float)keys[numKeys - 1] / 255.0f;
        return weight;
    }

    float fRealIndex = fTime * (float)(numKeys - 1);
    int nIndex = (int)fRealIndex;
    float fWeightB = fRealIndex - (float)nIndex;
    return (1.0f - fWeightB) * ((float)keys[nIndex] / 255.0f) + fWeightB * ((float)keys[nIndex + 1] / 255.0f);
}
