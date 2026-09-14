#ifndef _SANIM_H_
#define _SANIM_H_

#include "types.h"
#include <stdint.h>
#include <string.h>

#include "NL/nlMath.h"

class cPoseAccumulator;

struct PackedScale
{
    signed short x; // offset 0x0, size 0x2
    signed short y; // offset 0x2, size 0x2
    signed short z; // offset 0x4, size 0x2
}; // total size: 0x6

struct PackedTrans
{
    float x; // offset 0x0, size 0x4
    float y; // offset 0x4, size 0x4
    float z; // offset 0x8, size 0x4
}; // total size: 0xC

enum ePlayMode
{
    PM_CYCLIC = 0,
    PM_HOLD = 1,
};

class cSAnimCallback
{
public:
    /* 0x0 */ float m_fTime;
    // PORT: this holds a pointer as often as an id.
    /* 0x4 */ uintptr_t m_nParam1;
    /* 0x8 */ void (*m_funcCallback)(uintptr_t);
    /* 0xC */ cSAnimCallback* next;
}; // total size: 0x10

// PORT: nlChunk is an on-disc 8-byte header. Child chunks are byte-packed and
// are not guaranteed to start at a 4-byte boundary (real SKIN/retarget assets
// do contain odd-sized siblings). Keep the type packed so even legacy direct
// m_ID/m_Size reads cannot make GCC assume word alignment on ARM, and make the
// accessors memcpy-based so they are safe independently of compiler choices.
class __attribute__((packed)) nlChunk
{
public:
    nlChunk* GetNextChunk();
    u32 GetSize();
    u32 GetID();
    nlChunk* GetLastChunk();
    nlChunk* GetFirstChunk();
    u8 IsNestedChunk();
    void* GetData();
    void* GetUnalignedData();
    void* GetAlignedData();
    u32 GetChunkAlignment();
    bool IsAlignedChunk();

    /* 0x00 */ u32 m_ID;
    /* 0x04 */ u32 m_Size;
}; // size: 0x8

inline nlChunk* nlChunk::GetNextChunk()
{
    return (nlChunk*)((u8*)this + GetSize() + sizeof(nlChunk));
}

inline u32 nlChunk::GetSize()
{
    u32 value;
    memcpy(&value, &m_Size, sizeof(value));
    return value;
}

inline u32 nlChunk::GetID()
{
    u32 value;
    memcpy(&value, &m_ID, sizeof(value));
    return value & 0x80FFFFFF;
}

inline nlChunk* nlChunk::GetLastChunk()
{
    return (nlChunk*)((u8*)this + GetSize() + sizeof(nlChunk));
}

inline nlChunk* nlChunk::GetFirstChunk()
{
    return (nlChunk*)((u8*)this + sizeof(nlChunk));
}

inline u8 nlChunk::IsNestedChunk()
{
    u32 value;
    memcpy(&value, &m_ID, sizeof(value));
    return (value & 0x80000000) != 0;
}

inline void* nlChunk::GetData()
{
    bool isAligned = IsAlignedChunk();
    if (isAligned)
    {
        return GetAlignedData();
    }
    return GetUnalignedData();
}

inline void* nlChunk::GetUnalignedData()
{
    return this + 1;
}

inline void* nlChunk::GetAlignedData()
{
    const u32 shift = GetChunkAlignment() >> 24;
    if (shift == 0 || shift > 16)
        return shift == 0 ? GetUnalignedData() : NULL;
    uintptr_t alignment = (uintptr_t)1 << shift;
    uintptr_t addr = (uintptr_t)GetUnalignedData();
    return (void*)((addr + alignment - 1) & ~(alignment - 1));
}

inline u32 nlChunk::GetChunkAlignment()
{
    u32 value;
    memcpy(&value, &m_ID, sizeof(value));
    return value & 0x7F000000;
}

inline bool nlChunk::IsAlignedChunk()
{
    return GetChunkAlignment();
}

class cIdentifier
{
public:
    unsigned int GetHashID() const
    {
        return m_uHashID;
    }

    void Destroy()
    {
    }

protected:
    /* 0x0 */ const char* m_szName;
    /* 0x4 */ unsigned int m_uHashID;
}; // total size: 0x8

class cSAnim : public cIdentifier
{
public:
    typedef char* MemType;

    static cSAnim* Initialize(nlChunk* pChunk);
    static u8 IsValidChunkID(u32 id)
    {
        return (id & 0x80FFFFFF) == 0x80017000;
    }

    void BlendRot(int nodeIndex, int remappedNodeIndex, float tNorm, float weight, cPoseAccumulator* acc, bool additive) const;
    void BlendScale(int nodeIndex, int remappedNodeIndex, float tNorm, float weight, cPoseAccumulator* acc, bool additive) const;
    void BlendTrans(int nAccumulatorNode, int nSAnimNode, float fTime, float fWeight, cPoseAccumulator* pAccumulator, bool bMirror) const;
    void Destroy();
    void GetRootRot(float fTime, unsigned short* pRootRot) const;
    void GetRootTrans(float t, nlVector3* out) const;
    void CreateCallback(float fTime, uintptr_t nParam1, void (*funcCallback)(uintptr_t));
    float GetMorphWeight(int channel, float fTime) const;

    cSAnimCallback* GetCallbackList() const
    {
        return m_pCallbackList;
    }

    float GetDuration() const
    {
        return (float)m_nNumKeys / 30.0f;
    }

    /* 0x08 */ unsigned int m_nNumKeys;
    /* 0x0C */ unsigned int m_nNumNodes;
    /* 0x10 */ unsigned int m_nNumMorphChannels;
    /* 0x14 */ const unsigned int* m_pNodeProperties;
    /* 0x18 */ void* m_pRotKeys;
    /* 0x1C */ PackedScale** m_pScaleKeys;
    /* 0x20 */ PackedTrans** m_pTransKeys;
    /* 0x24 */ unsigned int m_nNumRootKeys;
    /* 0x28 */ unsigned short* m_pRootRot;
    /* 0x2C */ nlVector3* m_pRootTrans;
    // PORT: 4-byte on-disc records.
    /* 0x30 */ const unsigned int* m_nMorphIds;
    /* 0x34 */ const unsigned int* m_pNumMorphKeys;
    /* 0x38 */ unsigned char* m_pMorphKeys;
    /* 0x3C */ cSAnimCallback* m_pCallbackList;
    /* 0x40 */ float m_fLinearSpeed;
    /* 0x44 */ unsigned long m_nHierarchySignature;
}; // total size: 0x48

#endif // _SANIM_H_
