#include "NL/glx/glxLoadModel.h"
#include "port/endian.h"
#include "NL/nlWare.h"
#include "dolphin/os.h"
extern "C" {
unsigned long port_skin_swap(void*);
unsigned long port_bmd_packet_count(unsigned long);
unsigned long port_bmd_stream_count(unsigned long);
void port_bmd_convert_packets(void*, const void*, unsigned long);
void port_bmd_convert_streams(void*, const void*, unsigned long);
void port_bmd_stream_sizes(void*, unsigned long, unsigned long);
void port_bmd_swap_indices(void*, unsigned long);
unsigned long port_bmd_model_count(unsigned long);
void port_bmd_convert_models(void*, const void*, unsigned long, unsigned long);
}
#include "NL/glx/glxMemory.h"
#include "NL/gl/glMemory.h"
#include "NL/gl/glModel.h"
#include "NL/gl/glState.h"
#include "NL/gl/glTexture.h"
#include "NL/gl/glUserData.h"
#include "NL/glx/glxDisplayList.h"
#include "NL/glx/glxTexture.h"
#include "NL/nlFile.h"
#include "NL/nlFileGC.h"
#include "NL/nlString.h"
#include "NL/nlDLRing.h"
#include "NL/nlMemory.h"
#include "NL/platvmath.h"
#include "Game/GL/GLInventory.h"
#include "Game/GL/GLTextureAnim.h"
#include "Game/GL/GLVertexAnim.h"
#include "Game/GL/GLMaterial.h"
#include "Game/GL/ShaderSkinMesh.h"
#include "Game/SAnim.h"
#include "Game/Sys/debug.h"
#include "dolphin/os/OSCache.h"
#include <string.h>

extern GLInventory glInventory;

static bool glIgnoreDuplicateModels;

static glModel* glxLoadModelFromMemory(char* data, int size, unsigned long* pNumModels, bool bLoadTextures);

// BMD model chunk type IDs.
enum BMDChunkType
{
    BMD_CHUNK_FILE_INFO = 0x1B001,
    BMD_CHUNK_REF_DATA = 0x1B002,
    BMD_CHUNK_MODELS = 0x1B003,
    BMD_CHUNK_PACKETS = 0x1B004,
    BMD_CHUNK_STREAMS = 0x1B005,
    BMD_CHUNK_DISPLAY_LIST = 0x1B006,
    BMD_CHUNK_INDEX_DATA = 0x1B007,
    BMD_CHUNK_SKIN = (int)0x8001B008u,
    BMD_CHUNK_TEXTURE_ANIM = 0x1B00F,
    BMD_CHUNK_VERTEX_ANIM = 0x1B011,
    BMD_CHUNK_MATERIAL_LIST = 0x1B012,
};

static const int gl_stream_stride[15] = {
    12, 3, 4, 4, 4, 4, 4, 4, 4, 12, 12, 12, 1, 16, 16
};

static void* NLVIRTUALALLOC(unsigned long size)
{
    if (nlVirtualLargestBlock() >= size + 0x100)
    {
        return nlVirtualAlloc(size, false);
    }

    OSReport("VIRTUAL MEMORY WARNING ~ NLVIRTUALALLOC had to fall back to MRAM\nLargest block: %d Total free: %d\n",
        nlVirtualLargestBlock(),
        nlVirtualTotalFree());
    return nlMalloc(size, 0x20, false);
}

// Skin mesh chunk type IDs (lower 24 bits of m_ID); switch index = (type - 0x1B009).
enum SkinChunkType
{
    SKIN_CHUNK_0x1B009 = 0x1B009,
    SKIN_CHUNK_BONE_MATRICES = 0x1B00A,
    SKIN_CHUNK_BONE_MAP_LIST = 0x1B00B,
    SKIN_CHUNK_MORPH = 0x1B00C,
    SKIN_CHUNK_SOFTWARE_VERTICES = 0x1B00D,
    SKIN_CHUNK_SKIN_PAIRS = 0x1B00E,
    SKIN_CHUNK_0x1B00F = 0x1B00F,
    SKIN_CHUNK_STITCHING = 0x1B010,
};

/**
 * Offset/Address/Size: 0xF08 | 0x801C0B28 | size: 0x8
 */
void glSetIgnoreDuplicateModels(bool ignore)
{
    glIgnoreDuplicateModels = ignore;
}

/**
 * Offset/Address/Size: 0xC08 | 0x801C0828 | size: 0x2A0
 */
GLSkinMesh* glx_MakeSkinMesh(nlChunk* outerChunk, glModel* models)
{
    if (outerChunk == NULL || models == NULL)
        return NULL;

    ShaderSkinMesh* mesh = new (nlMalloc(sizeof(ShaderSkinMesh), 8, false)) ShaderSkinMesh();
    if (mesh == NULL)
        return NULL;

    mesh->pModel = models;

    u32 i;
    u32 count;
    const u8* outerRaw = (const u8*)outerChunk;
    const u32 outerSize = port_u32_unaligned(outerRaw + 4);
    const u8* cursor = outerRaw + 8;
    const u8* end = cursor + outerSize;

    while (cursor < end)
    {
        if ((unsigned long)(end - cursor) < 8)
            return NULL;

        const u32 id = port_u32_unaligned(cursor + 0);
        const u32 serializedSize = port_u32_unaligned(cursor + 4);
        if (serializedSize > (u32)(end - cursor - 8))
            return NULL;

        u32 chunkType = id & ~0x7F000000u;
        unsigned long payloadLen = 0;
        const u8* data = port_chunk_payload_const(cursor, id, serializedSize, &payloadLen);
        if (data == NULL)
            return NULL;
        const u32 chunkSize = (u32)payloadLen;

        switch (chunkType)
        {
        case 0x1B009:
            break;
        case 0x1B00A:
        {
            if (chunkSize % 0x44 != 0)
                return NULL;
            count = chunkSize / 0x44;
            for (i = 0; i < count; i++)
            {
                u32 boneID = port_u32_unaligned(data);
                nlMatrix4 src;
                nlMatrix4 inv;
                memcpy(&src, data + 4, sizeof(nlMatrix4));
                data += 0x44;
                nlInvertMatrix(inv, src);
                mesh->SetBoneMatrix(boneID, &inv);
            }
            break;
        }
        case 0x1B00B:
        {
            if (chunkSize % 8 != 0)
                return NULL;
            BoneMapList* node = new (nlMalloc(sizeof(BoneMapList), 8, false)) BoneMapList;
            if (node == NULL)
                return NULL;

            count = chunkSize >> 3;
            node->m_next = NULL;
            for (i = 0; i < count; i++)
            {
                unsigned long key = port_u32_unaligned(data + 0);
                unsigned long value = port_u32_unaligned(data + 4);
                data += 8;
                node->boneMap.Add(key, value);
            }
            nlRingAddEnd<BoneMapList>(&mesh->boneMaps, node);
            break;
        }
        case 0x1B00D:
            if (chunkSize % 0x10 != 0)
                return NULL;
            mesh->SetSoftwareVertices((int)(chunkSize >> 4), (const SkinVertex*)data);
            break;
        case 0x1B00E:
            if (chunkSize % 4 != 0)
                return NULL;
            mesh->AppendSkinPairList((int)(chunkSize >> 2), (const SkinPair*)data);
            break;
        case 0x1B00C:
        {
            if (chunkSize < 8)
                return NULL;
            u32 numMorphs = port_u32_unaligned(data + 0);
            const u32 numBaseVerts = port_u32_unaligned(data + 4);
            if (numMorphs > (chunkSize - 8) / 8)
                return NULL;
            mesh->numMorphs = (int)numMorphs;
            mesh->numBaseVerts = numBaseVerts;
            data += 8;
            mesh->SetMorphIDs((const u32*)data);
            data += numMorphs * 4;
            mesh->SetMorphNumDeltas((const u32*)data);
            data += numMorphs * 4;
            const u32 used = 8 + numMorphs * 8;
            if (chunkSize - used < 4)
                return NULL;
            const u32 deltaCount = port_u32_unaligned(data);
            if (deltaCount > (chunkSize - used - 4) / sizeof(MorphDelta))
                return NULL;
            mesh->SetMorphDeltas((int)deltaCount, (const MorphDelta*)(data + 4));
            break;
        }
        case 0x1B00F:
            break;
        case 0x1B010:
            if (chunkSize < 8)
                return NULL;
            mesh->AppendStitchingInfo((int)port_u32_unaligned(data + 4),
                                      (int)port_u32_unaligned(data + 0),
                                      (int)chunkSize - 8, data + 8);
            break;
        }

        cursor += 8 + serializedSize;
    }

    mesh->StitchModel();
    return mesh;
}

/**
 * Offset/Address/Size: 0x1D0 | 0x801BFDF0 | size: 0xA38
 */
static glModel* glxLoadModelFromMemory(char* data, int size, unsigned long* pNumModels, bool bLoadTextures)
{
    if (data == NULL || size < (int)sizeof(nlChunk))
        return NULL;

    const u8* fileBegin = (const u8*)data;
    const u8* fileEnd = fileBegin + (unsigned long)size;
    PortBEChunkView first;
    if (!port_be_chunk_read(fileBegin, fileEnd, &first))
    {
        OSReport("Error: model data does not start with a bounded BE chunk\n");
        return NULL;
    }

    const bool hasBmdHeader = ((first.id & ~0x7F000000u) == 0x8001B100u);
    const u8* outerCursor = hasBmdHeader ? first.raw + 8 : fileBegin;
    const u8* outerEnd = hasBmdHeader ? first.next : fileEnd;
    glModel* lastModels = NULL;

    while (outerCursor < outerEnd)
    {
        PortBEChunkView outer;
        if (!port_be_chunk_read(outerCursor, outerEnd, &outer) ||
            (outer.id & ~0x7F000000u) != 0x8001B000u)
        {
            OSReport("Error: model container is malformed or has unexpected id 0x%08lx\n",
                     (unsigned long)(outerCursor + 4 <= outerEnd ? port_be32(outerCursor) : 0));
            nlFree(data);
            return NULL;
        }
        outerCursor = outer.next;

        u32 numModels = 0;
        int numPacketEntries = 0;
        int numStreamEntries = 0;
        uintptr_t refDataPtr = 0;
        glModel* pModels = NULL;
        glModelPacket* pPackets = NULL;
        u8* pStreamData = NULL;
        u8* pDisplayListData = NULL;
        u32 vertexDataSize = 0;
        u8* pIndexData = NULL;
        bool hasSkinData = false;

        const u8* innerCursor = outer.raw + 8;
        const u8* innerEnd = outer.next;
        while (innerCursor < innerEnd)
        {
            PortBEChunkView chunkView;
            if (!port_be_chunk_read(innerCursor, innerEnd, &chunkView))
            {
                OSReport("Error: malformed inner BMD chunk\n");
                nlFree(data);
                return NULL;
            }
            innerCursor = chunkView.next;
            const int id = (int)(chunkView.id & ~0x7F000000u);
            const u32 chunkSize = (u32)chunkView.payload_len;
            const u8* chunkData = chunkView.payload;
            switch (id)
                {
                case BMD_CHUNK_FILE_INFO:
                    break;
                case BMD_CHUNK_REF_DATA:
                {
                    if ((chunkSize & 3u) != 0)
                        return NULL;
                    void* p = glResourceAlloc(chunkSize, GLM_Matrix);
                    if (p == NULL && chunkSize != 0)
                        return NULL;
                    refDataPtr = (uintptr_t)p;
                    memcpy(p, chunkData, chunkSize);
                    // PORT: matrices, and the file is big-endian.
                    port_be32_array(p, chunkSize / 4);
                    break;
                }
                case BMD_CHUNK_MODELS:
                {
                    if (chunkSize == 0 || (chunkSize & 0xFu) != 0)
                        return NULL;
                    numModels = chunkSize >> 4;
                    if (pNumModels != NULL)
                        *pNumModels = numModels;
                    pModels = (glModel*)glResourceAlloc(
                        numModels * sizeof(glModel), GLM_Header);
                    if (pModels == NULL)
                        return NULL;
                    port_bmd_convert_models(pModels, chunkData, numModels,
                                            sizeof(glModel));
                    {
                        glModel* pEnt = pModels;
                        glModel* pEntEnd = pModels + numModels;
                        for (; pEnt < pEntEnd; pEnt++)
                        {
                            if (glIgnoreDuplicateModels)
                            {
                                if (glInventory.GetModel(pEnt->id) != NULL)
                                {
                                    continue;
                                }
                            }
                            glInventory.AddModel(pEnt->id, pEnt);
                        }
                    }
                    break;
                }
                case BMD_CHUNK_PACKETS:
                {
                    if (chunkSize % 0x4A != 0)
                        return NULL;
                    numPacketEntries = (int)port_bmd_packet_count(chunkSize);
                    if (numPacketEntries <= 0)
                        return NULL;
                    pPackets = (glModelPacket*)glResourceAlloc(
                        numPacketEntries * sizeof(glModelPacket), GLM_Header);
                    if (pPackets == NULL)
                        return NULL;
                    port_bmd_convert_packets(pPackets, chunkData, numPacketEntries);
                    break;
                }
                case BMD_CHUNK_STREAMS:
                {
                    if (chunkSize % 0x06 != 0)
                        return NULL;
                    numStreamEntries = (int)port_bmd_stream_count(chunkSize);
                    if (numStreamEntries <= 0)
                        return NULL;
                    pStreamData = (u8*)glResourceAlloc(
                        numStreamEntries * sizeof(glModelStream), GLM_Header);
                    if (pStreamData == NULL)
                        return NULL;
                    port_bmd_convert_streams(pStreamData, chunkData, numStreamEntries);
                    break;
                }
                case BMD_CHUNK_DISPLAY_LIST:
                {
                    if (chunkSize == 0)
                        return NULL;
                    vertexDataSize = chunkSize;
                    pDisplayListData = (u8*)glResourceAlloc(chunkSize, GLM_VertexData);
                    if (pDisplayListData == NULL)
                        return NULL;
                    memcpy(pDisplayListData, chunkData, chunkSize);
                    DCFlushRange(pDisplayListData, chunkSize);
                    break;
                }
                case BMD_CHUNK_INDEX_DATA:
                {
                    if (chunkSize == 0 || (chunkSize & 1u) != 0)
                        return NULL;
                    pIndexData = (u8*)nlMalloc(chunkSize, 8, true);
                    if (pIndexData == NULL)
                        return NULL;
                    memcpy(pIndexData, chunkData, chunkSize);
                    port_bmd_swap_indices(pIndexData, chunkSize);
                    DCFlushRange(pIndexData, chunkSize);
                    break;
                }
                case BMD_CHUNK_TEXTURE_ANIM:
                {
                    // PORT: 4-byte big-endian fields.
                    const u8* p32 = (const u8*)chunkData;
                    if (chunkSize < 16)
                        return NULL;
                    unsigned long canonID = port_be32(p32);
                    p32 += 4;
                    if (glInventory.GetTextureAnim(canonID) == NULL)
                    {
                        unsigned long num = port_be32(p32);
                        p32 += 4;
                        if (num > (chunkSize - 16) / 8)
                            return NULL;
                        unsigned long mode = port_be32(p32);
                        p32 += 4;
                        float start = port_bef32(p32);
                        p32 += 4;
                        GLTextureAnim* pAnim = new (nlMalloc(sizeof(GLTextureAnim), 8, false)) GLTextureAnim();
                        pAnim->m_unk_0x00 = (s32)canonID;
                        pAnim->SetNumTextures(num);
                        pAnim->m_mode = mode;
                        pAnim->SetFrame((int)start);
                        unsigned long index;
                        GLAnimTex animTex;
                        for (index = 0; index < num; index++)
                        {
                            unsigned long hashID = port_be32(p32);
                            p32 += 4;
                            animTex.textureHandle = hashID;
                            animTex.time = port_bef32(p32);
                            p32 += 4;
                            pAnim->SetTexture(index, animTex);
                        }
                        glInventory.AddTextureAnim(canonID, pAnim);
                    }
                    else
                    {
                        tDebugPrintManager::Print(DC_LOADER, "skipping duplicate texanim 0x%08X\n", canonID);
                    }
                    break;
                }
                case BMD_CHUNK_VERTEX_ANIM:
                {
                    // PORT: 4-byte big-endian fields.
                    const u8* p32 = (const u8*)chunkData;
                    if (chunkSize < 16)
                        return NULL;
                    unsigned long hashID = port_be32(p32 + 0);
                    unsigned long numFrames = port_be32(p32 + 4);
                    unsigned long numVerts = port_be32(p32 + 8);
                    unsigned long fps = port_be32(p32 + 12);
                    p32 += 16;
                    GLVertexAnim* pAnim = new (nlMalloc(sizeof(GLVertexAnim), 8, false)) GLVertexAnim();
                    pAnim->m_uHashID = hashID;
                    pAnim->m_nNumFrames = numFrames;
                    pAnim->m_nNumVertices = numVerts;
                    pAnim->m_fFrameRate = (float)fps;
                    const unsigned long payloadBytes = chunkSize - 16;
                    if (numFrames != 0 && numVerts > payloadBytes / 12 / numFrames)
                        return NULL;
                    unsigned long vertexBytes = numFrames * 12 * numVerts;
                    if (vertexBytes > payloadBytes)
                        return NULL;
                    nlVector3* pVertices = (nlVector3*)glResourceAlloc(vertexBytes, GLM_VertexData);
                    if (pVertices == NULL && vertexBytes != 0)
                        return NULL;
                    memcpy(pVertices, p32, vertexBytes);
                    // PORT: and the vertices themselves are big-endian floats.
                    port_be32_array(pVertices, vertexBytes / 4);
                    DCFlushRange(pVertices, vertexBytes);
                    pAnim->m_pVertices = pVertices;
                    pAnim->m_pModel = glInventory.GetModel(hashID);
                    pAnim->Reset();
                    glInventory.AddVertexAnim(hashID, pAnim);
                    break;
                }
                case BMD_CHUNK_MATERIAL_LIST:
                {
                    // PORT: decode into a host array; never rewrite the source .glg.
                    const u8* p32 = (const u8*)chunkData;
                    if (chunkSize < 8) return NULL;
                    unsigned long modelID = port_be32(p32 + 0);
                    unsigned long numMats = port_be32(p32 + 4);
                    p32 += 8;
                    if (numMats > (chunkSize - 8) / sizeof(GLMaterialEntry)) return NULL;
                    GLMaterialEntry* hostMats = (GLMaterialEntry*)nlMalloc(
                        (numMats ? numMats : 1) * sizeof(GLMaterialEntry), 8, false);
                    if (hostMats == NULL) return NULL;
                    for (unsigned long m = 0; m < numMats; ++m)
                    {
                        u32* words = (u32*)&hostMats[m];
                        words[0] = port_be32(p32 + m * 12 + 0);
                        words[1] = port_be32(p32 + m * 12 + 4);
                        words[2] = port_be32(p32 + m * 12 + 8);
                    }
                    GLMaterialList* pList = new (nlMalloc(sizeof(GLMaterialList), 8, false)) GLMaterialList();
                    pList->m_uHashID = modelID;
                    pList->SetMaterials(numMats, hostMats);
                    nlFree(hostMats);
                    glInventory.AddMaterialList(modelID, pList);
                    break;
                }
                case BMD_CHUNK_SKIN:
                {
                    if (pModels == NULL || numModels == 0)
                        return NULL;
                    const u32 skinSize = chunkView.size + 8;
                    nlChunk* pSkinChunk = (nlChunk*)NLVIRTUALALLOC(skinSize);
                    if (pSkinChunk == NULL) return NULL;
                    memcpy(pSkinChunk, chunkView.raw, skinSize);
                    // Convert only this retained private copy: the source BMD remains immutable.
                    if (port_skin_swap(pSkinChunk) == 0)
                    {
                        OSReport("Error: SKIN chunk of model %lu is not well-formed\n",
                                 pModels ? (unsigned long)pModels->id : 0ul);
                        return NULL;
                    }
                    // PORT: see rw_glinventory_skinprobe.
                    if (getenv("STRIKERS_PROBE_SKIN"))
                        OSReport("[skin] ADD id=%lu pModels=%p numModels=%lu\n",
                                 (unsigned long)pModels->id, (void*)pModels,
                                 (unsigned long)numModels);
                    glInventory.AddSkinData(pModels->id, pSkinChunk);
                    hasSkinData = true;
                    break;
                }
                default:
                    break;
                }


        }

        if (pModels == NULL || pPackets == NULL || pStreamData == NULL ||
            pDisplayListData == NULL || pIndexData == NULL || numModels == 0)
        {
            OSReport("Error: BMD container is missing mandatory model/packet/stream/index data\n");
            nlFree(data);
            return NULL;
        }

        {
            int count = numModels;
            glModel* pM = pModels;
            while (count > 0)
            {
                pM->packets = (glModelPacket*)((uintptr_t)pM->packets + (uintptr_t)pPackets);
                pM++;
                count--;
            }
        }

        {
            glModelPacket* pPkt = pPackets;
            for (int i = 0; i < numPacketEntries; i++)
            {
                if (glGetRasterState(pPkt->state.raster, (eGLState)5) == 0 &&
                    glTextureLoad(pPkt->state.texture[0]))
                {
                    glUnHandleizeRasterState(pPkt->state.raster);
                    int bits = glTextureGetNumBits(3);
                    if (bits == 1)
                    {
                        glSetRasterState((eGLState)5, 0);
                        glSetRasterState((eGLState)3, 1);
                        glSetRasterState((eGLState)4, 0x40);
                    }
                    else if (bits > 1)
                    {
                        glSetRasterState((eGLState)5, 1);
                        glSetRasterState((eGLState)3, 1);
                        glSetRasterState((eGLState)4, 0);
                        glSetRasterState((eGLState)1, 0);
                    }
                    pPkt->state.raster = glHandleizeRasterState();
                }
                pPkt->streams = (glModelStream*)((uintptr_t)pPkt->streams + (uintptr_t)pStreamData);
                pPkt->indexBuffer += (uintptr_t)pIndexData;
                pPkt->state.matrix += refDataPtr;
                pPkt = (glModelPacket*)((u8*)pPkt + sizeof(glModelPacket));
            }
        }

        {
            glModelStream* pStream = (glModelStream*)pStreamData;
            port_bmd_stream_sizes(pStream, numStreamEntries, vertexDataSize);
            for (int count = 0; count < numStreamEntries; count++)
                pStream[count].address += (uintptr_t)pDisplayListData;
        }

        {
            glModel* pModel = pModels;
            glModel* pModelEnd = pModels + numModels;
            while (pModel < pModelEnd)
            {
                glModelPacket* pPacket = pModel->packets;
                while (pPacket < pModel->packets + pModel->numPackets)
                {
                    if (hasSkinData && glGetRasterState(pPacket->state.raster, (eGLState)8) == 1)
                    {
                        int oldNumStreams = pPacket->numStreams;
                        int newNum = oldNumStreams + 1;
                        glModelStream* streams = (glModelStream*)glResourceAlloc(newNum * sizeof(glModelStream), GLM_Header);
                        memcpy(streams, pPacket->streams, oldNumStreams * sizeof(glModelStream));
                        streams[oldNumStreams].id = 12;
                        streams[oldNumStreams].address = 0;
                        streams[oldNumStreams].stride = (u8)gl_stream_stride[12];
                        pPacket->numStreams = (u8)(pPacket->numStreams + 1);
                        pPacket->streams = streams;
                    }
                    if (pPacket->indexBuffer != 0)
                        pPacket->indexBuffer = (uintptr_t)dlMakeDisplayList(pPacket, true);
                    if (bLoadTextures)
                    {
                        for (int stage = 0; stage < 6; stage++)
                        {
                            if (pPacket->state.texconfig & (1 << stage))
                            {
                                uintptr_t texHandle = pPacket->state.texture[stage];
                                static const bool bProbe = getenv("STRIKERS_PROBE_TEX") != NULL;
                                if (bProbe && !glTextureLoad(texHandle))
                                {
                                    static u32 seen[64];
                                    static int nSeen = 0;
                                    bool bNew = true;
                                    for (int q = 0; q < nSeen; q++) if (seen[q] == texHandle) bNew = false;
                                    if (bNew && nSeen < 64)
                                    {
                                        seen[nSeen++] = texHandle;
                                        OSReport("[texload] model id 0x%08x wants texture 0x%08x (stage %d), not loaded\n",
                                                 (unsigned)pModel->id, (unsigned)texHandle, stage);
                                    }
                                }
                                if (glInventory.GetTextureAnim(texHandle) == NULL && glTextureLoad(texHandle))
                                    pPacket->state.texture[stage] = (uintptr_t)glx_GetTex(texHandle, true, true);
                            }
                        }
                    }
                    pPacket = (glModelPacket*)((u8*)pPacket + sizeof(glModelPacket));
                }
                pModel++;
            }
            nlFree(pIndexData);
        }
        lastModels = pModels;
    }

    nlFree(data);
    return lastModels;
}

static glModel* glxLoadModelFromDisk(const char* filename, unsigned long* pNumModels)
{
    unsigned int alignSize;
    unsigned int fileSize;
    char fullname[256];
    glModel* retval;
    nlFile* f;

    glx_FreeMemory0();
    nlStrNCat(fullname, "art/", filename, 256);

    f = nlOpen(fullname);
    if (f == NULL)
    {
        retval = NULL;
    }
    else
    {
        fileSize = nlFileSize(f, &alignSize);
        nlClose(f);
        if (fileSize == 0)
        {
            retval = NULL;
        }
        else
        {
            char* data = (char*)nlLoadEntireFileToVirtualMemory(fullname, (int*)&fileSize, 0x80000, NULL, AllocateEnd);
            if (data == NULL)
            {
                retval = NULL;
            }
            else
            {
                retval = glxLoadModelFromMemory(data, fileSize, pNumModels, true);
            }
        }
    }

    glx_FreeMemory1(filename);
    return retval;
}

/**
 * Offset/Address/Size: 0xCC | 0x801BFCEC | size: 0x104
 */
glModel* glplatLoadModel(const char* filename, unsigned long* pNumModels)
{
    unsigned int alignSize;
    unsigned int fileSize;
    char fullname[256];
    char lowerName[256];
    glModel* retval;
    nlFile* f;
    bool bSkinned;

    glx_FreeMemory0();
    nlStrNCat(fullname, "art/", filename, 256);
    nlStrNCpy(lowerName, fullname, 256);
    nlToLower(lowerName);

    bSkinned = (strstr(lowerName, "characters") == NULL);

    f = nlOpen(fullname);
    if (f == NULL)
    {
        retval = NULL;
    }
    else
    {
        fileSize = nlFileSize(f, &alignSize);
        nlClose(f);
        if (fileSize == 0)
        {
            retval = NULL;
        }
        else
        {
            char* data = (char*)nlLoadEntireFileToVirtualMemory(fullname, (int*)&fileSize, 0x80000, NULL, AllocateEnd);
            retval = glxLoadModelFromMemory(data, fileSize, pNumModels, bSkinned);
        }
    }

    glx_FreeMemory1(filename);
    return retval;
}

/**
 * Offset/Address/Size: 0x24 | 0x801BFC44 | size: 0xA8
 */
bool glplatBeginLoadModel(const char* filename, void (*callback)(void*, unsigned long, void*), void* userData)
{
    char fullname[256];
    nlStrNCat(fullname, "art/", filename, 256);

    if (userData == NULL)
    {
        if (!nlLoadEntireFileAsync(fullname, callback, userData, 32, AllocateEnd))
        {
            return false;
        }
    }
    else
    {
        if (!nlLoadEntireFileAsync(fullname, callback, userData, 32, (eAllocType)23))
        {
            return false;
        }
    }
    return true;
}

/**
 * Offset/Address/Size: 0x0 | 0x801BFC20 | size: 0x24
 */
glModel* glplatEndLoadModel(void* data, unsigned long size, unsigned long* pNumModels)
{
    return glxLoadModelFromMemory((char*)data, size, pNumModels, false);
}
