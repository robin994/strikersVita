#include "Game/SHierarchy.h"
#include "port/endian.h"
#include "Game/AI/FuzzyDebugger.h"
#include "NL/nlWare.h"
#include "types.h"

/**
 * Offset/Address/Size: 0x354 | 0x801EE340 | size: 0x3E0
 */
cSHierarchy* cSHierarchy::Initialize(nlChunk* pChunk)
{
    if (pChunk == NULL)
        return NULL;

    const u8* root = (const u8*)pChunk;
    const u32 rootID = port_be32(root);
    const u32 rootSize = port_be32(root + 4);
    if (!IsValidChunkID(rootID & 0x80FFFFFFu))
    {
        OSReport("Error: invalid hierarchy root id %08lx\n", (unsigned long)rootID);
        return NULL;
    }

    const u8* cursor = root + sizeof(nlChunk);
    const u8* rootEnd = cursor + rootSize;
    PortBEChunkView chunks[11];
    for (unsigned i = 0; i < sizeof(chunks) / sizeof(chunks[0]); ++i)
    {
        if (!port_be_chunk_read(cursor, rootEnd, &chunks[i]))
        {
            OSReport("Error: truncated hierarchy at child %u\n", i);
            return NULL;
        }
        cursor = chunks[i].next;
    }

    // PORT: deserialize the hierarchy into aligned host memory. These tables
    // used to point directly into byte-packed GameCube asset data, making every
    // later indexed access a potential ARM alignment fault.
    cSHierarchy* pRetval;
    {
        const u8* disc = chunks[0].payload;
        if (chunks[0].payload_len < 0x2C)
        {
            OSReport("Error: truncated hierarchy header\n");
            return NULL;
        }
        pRetval = (cSHierarchy*)nlMalloc(sizeof(cSHierarchy), 8, false);
        if (pRetval == NULL)
            return NULL;
        memset(pRetval, 0, sizeof(cSHierarchy));
        // PORT: cIdentifier keeps the name hash at disc 0x04, and every inventory lookup is by that value.
        pRetval->m_uHashID = port_be32(disc + 0x04);
        pRetval->m_nNumNodes = (int)port_be32(disc + 0x08);
        pRetval->m_nPelvisNodeIndex = (int)port_be32(disc + 0x24);
        pRetval->m_nSpineNodeIndex = (int)port_be32(disc + 0x28);
    }
    const u32 nNodes = (u32)pRetval->m_nNumNodes;
    if (pRetval->m_nNumNodes <= 0 || nNodes > 0x10000u)
    {
        OSReport("Error: unreasonable hierarchy node count %lu\n", (unsigned long)nNodes);
        return NULL;
    }

    if (chunks[1].payload_len == 0 ||
        memchr(chunks[1].payload, '\0', chunks[1].payload_len) == NULL)
        return NULL;
    pRetval->m_szName = (const char*)chunks[1].payload;

    {
        const u8* disc = chunks[2].payload;
        if (nNodes > chunks[2].payload_len / 4)
            return NULL;
        pRetval->m_pNodeID = (u32*)nlMalloc(nNodes * sizeof(u32), 8, false);
        if (pRetval->m_pNodeID == NULL)
            return NULL;
        for (u32 i = 0; i < nNodes; i++)
            pRetval->m_pNodeID[i] = port_be32(disc + i * 4);
    }

    {
        const u8* disc = chunks[3].payload;
        if (nNodes > chunks[3].payload_len / 4)
            return NULL;
        pRetval->m_pParent = (int*)nlMalloc(nNodes * sizeof(int), 8, false);
        if (pRetval->m_pParent == NULL)
            return NULL;
        for (u32 i = 0; i < nNodes; i++)
            pRetval->m_pParent[i] = (int)(s32)port_be32(disc + i * 4);
    }

    {
        const u8* disc = chunks[4].payload;
        if (nNodes > chunks[4].payload_len / 4)
            return NULL;
        pRetval->m_pNumChildren = (int*)nlMalloc(nNodes * sizeof(int), 8, false);
        if (pRetval->m_pNumChildren == NULL)
            return NULL;
        for (u32 i = 0; i < nNodes; i++)
        {
            pRetval->m_pNumChildren[i] = (int)(s32)port_be32(disc + i * 4);
            if (pRetval->m_pNumChildren[i] < 0 ||
                (u32)pRetval->m_pNumChildren[i] > nNodes)
                return NULL;
        }
    }

    // PORT: one host pointer per node, so the file's 4-byte entries will not do.
    if (nNodes > chunks[5].payload_len / 4)
        return NULL;
    pRetval->m_pChildren = (int**)nlMalloc(nNodes * sizeof(int*), 8, false);
    if (pRetval->m_pChildren == NULL)
        return NULL;
    memset(pRetval->m_pChildren, 0, nNodes * sizeof(int*));

    // m_pPushPop is scratch; do not leave it pointing into the asset buffer.
    if (nNodes > chunks[6].payload_len / 4)
        return NULL;
    pRetval->m_pPushPop = (int*)nlMalloc(nNodes * sizeof(int), 8, false);
    if (pRetval->m_pPushPop == NULL)
        return NULL;
    memset(pRetval->m_pPushPop, 0, nNodes * sizeof(int));

    {
        const u8* disc = chunks[7].payload;
        u32 nTotalChildren = 0;
        for (u32 i = 0; i < nNodes; i++)
        {
            if ((u32)pRetval->m_pNumChildren[i] > nNodes - nTotalChildren)
                return NULL;
            nTotalChildren += (u32)pRetval->m_pNumChildren[i];
        }
        if (nTotalChildren > chunks[7].payload_len / 4)
            return NULL;

        int* childStorage = (int*)nlMalloc((nTotalChildren ? nTotalChildren : 1) * sizeof(int), 8, false);
        if (childStorage == NULL)
            return NULL;
        for (u32 i = 0; i < nTotalChildren; i++)
        {
            childStorage[i] = (int)(s32)port_be32(disc + i * 4);
            if (childStorage[i] < 0 || (u32)childStorage[i] >= nNodes)
                return NULL;
        }

        int* pChild = childStorage;
        for (u32 i = 0; i < nNodes; i++)
        {
            pRetval->m_pChildren[i] = pRetval->m_pNumChildren[i] > 0 ? pChild : NULL;
            pChild += pRetval->m_pNumChildren[i];
        }
    }

    int nCurrentDepth = 0;
    pRetval->BuildPushPopFlags(0, 0, nCurrentDepth);

    {
        const u8* disc = chunks[8].payload;
        if (nNodes > chunks[8].payload_len / 4)
            return NULL;
        pRetval->m_pMirrorTable = (int*)nlMalloc(nNodes * sizeof(int), 8, false);
        if (pRetval->m_pMirrorTable == NULL)
            return NULL;
        for (u32 i = 0; i < nNodes; i++)
            pRetval->m_pMirrorTable[i] = (int)(s32)port_be32(disc + i * 4);
    }

    {
        const u8* disc = chunks[9].payload;
        if (nNodes > chunks[9].payload_len / sizeof(nlVector3))
            return NULL;
        pRetval->m_pV3TranslationOffset =
            (nlVector3*)nlMalloc(nNodes * sizeof(nlVector3), 8, false);
        if (pRetval->m_pV3TranslationOffset == NULL)
            return NULL;
        for (u32 i = 0; i < nNodes; i++)
        {
            const u8* v = disc + i * sizeof(nlVector3);
            pRetval->m_pV3TranslationOffset[i].x = port_bef32(v + 0);
            pRetval->m_pV3TranslationOffset[i].y = port_bef32(v + 4);
            pRetval->m_pV3TranslationOffset[i].z = port_bef32(v + 8);
        }
    }

    if (nNodes > chunks[10].payload_len)
        return NULL;
    pRetval->m_pPreserveBoneLength = (u8*)nlMalloc(nNodes, 8, false);
    if (pRetval->m_pPreserveBoneLength == NULL)
        return NULL;
    memcpy(pRetval->m_pPreserveBoneLength, chunks[10].payload, nNodes);

    return pRetval;
}

/**
 * Offset/Address/Size: 0xD0 | 0x801EE0BC | size: 0x284
 */
void cSHierarchy::BuildPushPopFlags(int nNode, int nParentDepth, int& nCurrentDepth)
{
    int nNumChildren;
    int i;

    if (nParentDepth != nCurrentDepth)
    {
        m_pPushPop[nNode - 1] = nParentDepth - nCurrentDepth;
        nCurrentDepth = nParentDepth;
    }

    nNumChildren = m_pNumChildren[nNode];
    if (nNumChildren != 0)
    {
        m_pPushPop[nNode] = 1;
        nCurrentDepth = nCurrentDepth + 1;
        nParentDepth = nCurrentDepth;

        for (i = 0; i < nNumChildren; i++)
        {
            BuildPushPopFlags(GetChild(nNode, i), nParentDepth, nCurrentDepth);
        }
    }
    else
    {
        m_pPushPop[nNode] = 0;
    }
}

/**
 * Offset/Address/Size: 0xB8 | 0x801EE0A4 | size: 0x18
 */
int cSHierarchy::GetChild(int i, int j) const
{
    nlAssert(i < m_nNumNodes);
    nlAssert(j < m_pNumChildren[i]);
    return m_pChildren[i][j];
}

/**
 * Offset/Address/Size: 0x78 | 0x801EE064 | size: 0x40
 */
int cSHierarchy::GetNodeIndexByID(unsigned int id) const
{
    for (int i = 0; i < m_nNumNodes; i++)
    {
        if (id == m_pNodeID[i])
        {
            return i;
        }
    }
    return -1;
}

/**
 * Offset/Address/Size: 0x68 | 0x801EE054 | size: 0x10
 */
u32 cSHierarchy::GetNodeID(int i) const
{
    nlAssert(i < m_nNumNodes);
    return m_pNodeID[i];
}

/**
 * Offset/Address/Size: 0x58 | 0x801EE044 | size: 0x10
 */
int cSHierarchy::GetNumChildren(int i) const
{
    nlAssert(i < m_nNumNodes);
    return m_pNumChildren[i];
}

/**
 * Offset/Address/Size: 0x48 | 0x801EE034 | size: 0x10
 */
int cSHierarchy::GetMirroredNode(int i) const
{
    nlAssert(i < m_nNumNodes);
    return m_pMirrorTable[i];
}

/**
 * Offset/Address/Size: 0x38 | 0x801EE024 | size: 0x10
 */
int cSHierarchy::GetPushPop(int i) const
{
    nlAssert(i < m_nNumNodes);
    return m_pPushPop[i];
}

/**
 * Offset/Address/Size: 0x28 | 0x801EE014 | size: 0x10
 */
int cSHierarchy::GetParent(int i) const
{
    nlAssert(i < m_nNumNodes);
    return m_pParent[i];
}

/**
 * Offset/Address/Size: 0x18 | 0x801EE004 | size: 0x10
 */
nlVector3& cSHierarchy::GetTranslationOffset(int i) const
{
    nlAssert(i < m_nNumNodes);
    return m_pV3TranslationOffset[i];
}

/**
 * Offset/Address/Size: 0x0 | 0x801EDFEC | size: 0x18
 */
bool cSHierarchy::PreserveBoneLength(int i) const
{
    nlAssert(i < m_nNumNodes);
    return m_pPreserveBoneLength[i];
}

void cSHierarchy::Dump() const
{
    int nStackIndex = -1;
    int i = 0;
    int j;

    nlAssert(m_nNumNodes > 0 && m_pNodeID);

    do
    {
        nlAssert(i < m_nNumNodes);
        if ((j = nStackIndex + 1) != 0)
        {
            do
            {
                nlPrintf("------------------------------");
            } while (--j != 0);
        }
        nlPrintf("0x%08x (%2d)\n", m_pNodeID[i], m_pParent[i]);
        nStackIndex += m_pPushPop[i];
        i++;
    } while (i < m_nNumNodes);
}
