#include "Game/Camera/animcam.h"
#include "NL/nlWare.h"
#include "dolphin/os.h"
#include "port/endian.h"

#include "Game/AI/AiUtil.h"
#include "NL/nlFile.h"
#include "NL/gl/glMatrix.h"
#include "NL/nlMemory.h"
#include "Game/Render/depthoffield.h"
#include "Game/FixedUpdateTask.h"

static float dofBehindTarget = 2.0f;

cCameraData* cAnimCamera::m_cameraDataList;

/**
 * Offset/Address/Size: 0x113C | 0x801A5D30 | size: 0x2C
 */
static void EnableDofDebug()
{
    if (DepthOfFieldManager::instance.m_bDebugView)
    {
        DepthOfFieldManager::instance.m_bDebugView = 0;
    }
    else
    {
        DepthOfFieldManager::instance.m_bDebugView = 1;
    }
}

struct DofDebugFlag
{
    DofDebugFlag()
    {
        m_active = false;
        m_callback = EnableDofDebug;
    }
    bool m_active;
    void (*m_callback)();
};

static DofDebugFlag g_EnableDofDebug;

static void InitCameraData(cCameraData* data)
{
    data->next = NULL;
    data->m_uHashID = 0;
    data->m_uKeyCount = 0;
    data->cameraPos = NULL;
    data->targetPos = NULL;
    data->cameraRot = NULL;
    data->fFOV = NULL;
    data->fFocalLength = NULL;
    data->ownsKeyData = true;
}

static void FreeCameraKeys(cCameraData* data)
{
    if (data == NULL)
        return;

    operator delete[](data->cameraPos);
    operator delete[](data->targetPos);
    operator delete[](data->cameraRot);
    operator delete[](data->fFOV);
    operator delete[](data->fFocalLength);
    data->cameraPos = NULL;
    data->targetPos = NULL;
    data->cameraRot = NULL;
    data->fFOV = NULL;
    data->fFocalLength = NULL;
}

struct CameraArrayView
{
    const u8* data;
    unsigned long size;
};

static bool CameraArrayFits(const CameraArrayView& view, u32 count, u32 stride)
{
    return view.data != NULL && stride != 0 && count <= view.size / stride;
}

/**
 * Offset/Address/Size: 0xCA4 | 0x801A5898 | size: 0x498
 */
static bool LoadAnimCameraDataBE(const u8* begin, const u8* end, cCameraData* out)
{
    if (begin == NULL || end == NULL || begin > end || out == NULL)
        return false;

    CameraArrayView cameraPos = { NULL, 0 };
    CameraArrayView targetPos = { NULL, 0 };
    CameraArrayView cameraRot = { NULL, 0 };
    CameraArrayView fov = { NULL, 0 };
    CameraArrayView focal = { NULL, 0 };
    bool haveCount = false;
    u32 keyCount = 0;

    const u8* cursor = begin;
    while (cursor < end)
    {
        PortBEChunkView chunk;
        if (!port_be_chunk_read(cursor, end, &chunk))
            return false;
        cursor = chunk.next;

        switch (chunk.id & 0x80FFFFFFu)
        {
        case 0x15508:
            if (chunk.payload_len < 4)
                return false;
            keyCount = port_be32(chunk.payload);
            haveCount = true;
            break;
        case 0x15509: cameraPos = { chunk.payload, chunk.payload_len }; break;
        case 0x1550C: targetPos = { chunk.payload, chunk.payload_len }; break;
        case 0x15511: cameraRot = { chunk.payload, chunk.payload_len }; break;
        case 0x1550F: fov = { chunk.payload, chunk.payload_len }; break;
        case 0x15510: focal = { chunk.payload, chunk.payload_len }; break;
        default: break;
        }
    }

    if (!haveCount || keyCount == 0 ||
        !CameraArrayFits(cameraPos, keyCount, 12) ||
        !CameraArrayFits(targetPos, keyCount, 12) ||
        !CameraArrayFits(cameraRot, keyCount, 16) ||
        !CameraArrayFits(fov, keyCount, 4))
        return false;

    out->m_uKeyCount = keyCount;
    out->ownsKeyData = true;
    out->cameraPos = (nlVector3*)nlMalloc(keyCount * sizeof(nlVector3), 8, false);
    out->targetPos = (nlVector3*)nlMalloc(keyCount * sizeof(nlVector3), 8, false);
    out->cameraRot = (nlQuaternion*)nlMalloc(keyCount * sizeof(nlQuaternion), 8, false);
    out->fFOV = (float*)nlMalloc(keyCount * sizeof(float), 8, false);
    if (focal.data != NULL)
    {
        if (!CameraArrayFits(focal, keyCount, 4))
        {
            FreeCameraKeys(out);
            return false;
        }
        out->fFocalLength = (float*)nlMalloc(keyCount * sizeof(float), 8, false);
    }

    if (out->cameraPos == NULL || out->targetPos == NULL ||
        out->cameraRot == NULL || out->fFOV == NULL ||
        (focal.data != NULL && out->fFocalLength == NULL))
    {
        FreeCameraKeys(out);
        return false;
    }

    for (u32 i = 0; i < keyCount; ++i)
    {
        for (u32 component = 0; component < 3; ++component)
        {
            out->cameraPos[i].e[component] = port_bef32(cameraPos.data + i * 12 + component * 4);
            out->targetPos[i].e[component] = port_bef32(targetPos.data + i * 12 + component * 4);
        }
        for (u32 component = 0; component < 4; ++component)
            out->cameraRot[i].e[component] = port_bef32(cameraRot.data + i * 16 + component * 4);
        out->fFOV[i] = port_bef32(fov.data + i * 4);
        if (out->fFocalLength != NULL)
            out->fFocalLength[i] = port_bef32(focal.data + i * 4);
    }

    return true;
}

/**
 * Offset/Address/Size: 0xBF8 | 0x801A57EC | size: 0xAC
 */
bool cAnimCamera::LoadCameraAnimation(nlChunk* begin, nlChunk* end, const char* cameraName, bool ownsKeyData)
{
    // Immutable BE input cannot be exposed as host float/vector arrays. Always
    // materialize host-owned keys, including NIS cameras that used to alias mData.
    (void)ownsKeyData;
    cCameraData* data = (cCameraData*)nlMalloc(sizeof(cCameraData), 8, false);
    if (data == NULL)
        return false;

    InitCameraData(data);
    data->m_uHashID = nlStringLowerHash(cameraName);
    if (!LoadAnimCameraDataBE((const u8*)begin, (const u8*)end, data))
    {
        FreeCameraKeys(data);
        operator delete(data);
        return false;
    }

    nlListAddStart(&m_cameraDataList, data, (cCameraData**)NULL);
    return true;
}

/**
 * Offset/Address/Size: 0xB14 | 0x801A5708 | size: 0xE4
 */
bool cAnimCamera::LoadCameraAnimation(const char* szFilename, const char* szCameraName, bool ownsKeyData)
{
    unsigned long size = 0;
    void* fileData = nlLoadEntireFile(szFilename, &size, 0x20, (eAllocType)0);
    if (fileData == NULL)
        return false;

    const u8* begin = (const u8*)fileData;
    const u8* end = begin + size;
    PortBEChunkView root;
    const bool valid = port_be_chunk_read(begin, end, &root) &&
                       (root.id & 0x00FFFFFFu) == 0x15501u;
    bool result = false;
    if (valid)
    {
        result = LoadCameraAnimation((nlChunk*)(root.raw + 8), (nlChunk*)root.next,
                                     szCameraName, ownsKeyData);
    }
    else
    {
        OSReport("Error: '%s' is not a well-formed camera animation\n", szFilename);
    }

    operator delete(fileData);
    return result;
}

/**
 * Offset/Address/Size: 0xAE8 | 0x801A56DC | size: 0x2C
 */
void cAnimCamera::FreeCameraAnimations()
{
    nlDeleteList(&m_cameraDataList);
    m_cameraDataList = NULL;
}

/**
 * Offset/Address/Size: 0xA54 | 0x801A5648 | size: 0x94
 */
cAnimCamera::cAnimCamera()
{
    m_bUseSimulationTime = false;
    m_LetManagerDoUpdate = true;
    m_bUnusedPad = false;
    m_fAnimationTime = 0.0f;
    m_fAnimationSpeed = 1.0f;
    m_fLastSimulationTime = -1.0f;
    m_pActiveCameraData = NULL;
    mFacingAngle = 0;
    m_EndOfAnimationCallback = NULL;
    nlVec3Set(m_vecTarget, 0.0f, 0.0f, 0.0f);
    nlVec3Set(m_OffsetPos, 0.0f, 0.0f, 0.0f);
    nlVec3Set(m_Mirror, 1.0f, 1.0f, 1.0f);
    m_pActiveCameraData = m_cameraDataList;
    m_bCyclic = true;
}

/**
 * Offset/Address/Size: 0x9F8 | 0x801A55EC | size: 0x5C
 */
cAnimCamera::~cAnimCamera()
{
}

/**
 * Offset/Address/Size: 0x41C | 0x801A5010 | size: 0x5DC
 */
void cAnimCamera::BuildAnimViewMatrix(nlMatrix4& mView)
{
    float fRealIndex = m_fAnimationTime * (float)(m_pActiveCameraData->m_uKeyCount - 1);
    int nIndex = (int)fRealIndex;
    float fWeightB = fRealIndex - (float)nIndex;
    float fWeightA = 1.0f - fWeightB;
    nlVector3 cameraPos = { };
    nlVector3 targetPos = { };
    nlQuaternion cameraRot = { 0.0f, 0.0f, 0.0f, 1.0f };
    nlMatrix4 viewMatrix;
    nlMatrix4 facingAngleMatrix;
    if (m_fAnimationTime >= 1.0f)
    {
        m_Fov = m_pActiveCameraData->fFOV[nIndex];
        cameraPos = m_pActiveCameraData->cameraPos[nIndex];
        targetPos = m_pActiveCameraData->targetPos[nIndex];
        cameraRot = m_pActiveCameraData->cameraRot[nIndex];
    }
    else
    {
        nlVector3& cpN = m_pActiveCameraData->cameraPos[nIndex + 1];
        nlVector3& cpK = m_pActiveCameraData->cameraPos[nIndex];
        nlVector3 delta;
        nlVec3Sub(delta, cpK, cpN);
        float distSq = delta.GetLengthSq3D();
        if (distSq > 16.0f)
        {
            if (fWeightB < 0.5f)
            {
                cameraPos = m_pActiveCameraData->cameraPos[nIndex];
                targetPos = m_pActiveCameraData->targetPos[nIndex];
                cameraRot = m_pActiveCameraData->cameraRot[nIndex];
                m_Fov = m_pActiveCameraData->fFOV[nIndex];
            }
            else
            {
                cameraPos = m_pActiveCameraData->cameraPos[nIndex + 1];
                targetPos = m_pActiveCameraData->targetPos[nIndex + 1];
                cameraRot = m_pActiveCameraData->cameraRot[nIndex + 1];
                m_Fov = m_pActiveCameraData->fFOV[nIndex + 1];
            }
        }
        else
        {
            m_Fov = fWeightA * m_pActiveCameraData->fFOV[nIndex] + fWeightB * m_pActiveCameraData->fFOV[nIndex + 1];
            nlVec3WeightedSum(cameraPos, fWeightA, m_pActiveCameraData->cameraPos[nIndex], fWeightB, m_pActiveCameraData->cameraPos[nIndex + 1]);
            nlVec3WeightedSum(targetPos, fWeightA, m_pActiveCameraData->targetPos[nIndex], fWeightB, m_pActiveCameraData->targetPos[nIndex + 1]);
            nlQuatSlerp(cameraRot, m_pActiveCameraData->cameraRot[nIndex], m_pActiveCameraData->cameraRot[nIndex + 1], fWeightB);
        }
    }

    nlVector3 dofDelta;
    nlVec3Sub(dofDelta, m_vecCamera, m_vecTarget);
    float dist = nlSqrt(dofDelta.GetLengthSq3D(), true);
    DepthOfFieldManager::instance.m_fDistanceFromCamera = dofBehindTarget + dist;
    cameraPos.x *= m_Mirror.x;
    cameraPos.y *= m_Mirror.y;
    cameraPos.z *= m_Mirror.z;
    targetPos.x *= m_Mirror.x;
    targetPos.y *= m_Mirror.y;
    targetPos.z *= m_Mirror.z;
    if (m_Mirror.x != 1.0f || m_Mirror.y != 1.0f || m_Mirror.z != 1.0f)
    {
        cameraRot.x *= -m_Mirror.x;
        cameraRot.y *= -m_Mirror.y;
        cameraRot.z *= -m_Mirror.z;
    }
    nlVec3Add(m_vecCamera, cameraPos, m_OffsetPos);
    nlVec3Add(m_vecTarget, targetPos, m_OffsetPos);
    GetLocalPoint(m_vecCamera, m_vecCamera, m_OffsetPos, 0);
    GetWorldPoint(m_vecCamera, m_vecCamera, m_OffsetPos, mFacingAngle);
    if (m_bUnusedPad)
    {
        nlVector3 up = { 0.0f, 0.0f, 1.0f };
        glMatrixLookAt(mView, m_vecCamera, m_vecTarget, up);
    }
    else
    {
        nlQuatToMatrix(viewMatrix, cameraRot);
        float facingAngleRadians = (float)mFacingAngle * 0.0000958738f;
        nlMakeRotationMatrixZ(facingAngleMatrix, facingAngleRadians);
        nlMultMatrices(viewMatrix, viewMatrix, facingAngleMatrix);
        viewMatrix.m41 = m_vecCamera.x;
        viewMatrix.m42 = m_vecCamera.y;
        viewMatrix.m43 = m_vecCamera.z;
        viewMatrix.m44 = 1.0f;
        nlInvertMatrix(mView, viewMatrix);
    }
}

/**
 * Offset/Address/Size: 0x410 | 0x801A5004 | size: 0xC
 */
void cAnimCamera::UnselectCameraAnimation()
{
    m_pActiveCameraData = 0;
}

/**
 * Offset/Address/Size: 0x39C | 0x801A4F90 | size: 0x74
 */
void cAnimCamera::SelectCameraAnimation(const char* name)
{
    m_fLastSimulationTime = -1.0f;
    u32 hash = nlStringLowerHash(name);

    cCameraData* pData = m_cameraDataList;
    while (pData != nullptr)
    {
        if (pData->m_uHashID == hash)
        {
            m_pActiveCameraData = pData;
            return;
        }
        pData = pData->next;
    }

    m_fAnimationTime = 0.0f;
    BuildAnimViewMatrix(m_matView);
}

/**
 * Offset/Address/Size: 0x34C | 0x801A4F40 | size: 0x50
 */
bool cAnimCamera::CameraAnimationExists(const char* name) const
{
    u32 hash = nlStringLowerHash(name);
    cCameraData* data = m_cameraDataList;
    while (data != NULL)
    {
        if (data->m_uHashID == hash)
        {
            return true;
        }
        data = data->next;
    }
    return false;
}

/**
 * Offset/Address/Size: 0x2AC | 0x801A4EA0 | size: 0xA0
 */
void cAnimCamera::FreeCameraAnimation(const char* szCameraName)
{
    unsigned long uHashID = nlStringLowerHash(szCameraName);
    cCameraData* pCameraData = m_cameraDataList;
    while (pCameraData != NULL)
    {
        if (pCameraData->m_uHashID == uHashID)
        {
            nlListRemoveElement(&m_cameraDataList, pCameraData, (cCameraData**)NULL);
            if (pCameraData != NULL)
            {
                if (pCameraData->ownsKeyData)
                {
                    operator delete[](pCameraData->cameraPos);
                    operator delete[](pCameraData->targetPos);
                    operator delete[](pCameraData->cameraRot);
                    operator delete[](pCameraData->fFOV);
                    operator delete[](pCameraData->fFocalLength);
                }
                operator delete(pCameraData);
            }
            return;
        }
        pCameraData = pCameraData->next;
    }
}

static inline float GetSimulationDelta(float lastTime, float& simTime)
{
    simTime = FixedUpdateTask::mSimulationTime;
    return simTime - lastTime;
}

/**
 * Offset/Address/Size: 0x150 | 0x801A4D44 | size: 0x15C
 */
void cAnimCamera::Update(float dt)
{
    if (!m_LetManagerDoUpdate)
    {
        return;
    }

    if (m_bUseSimulationTime)
    {
        float lastTime = m_fLastSimulationTime;
        float duration = 0.0f;
        if (lastTime < duration)
        {
            m_fLastSimulationTime = FixedUpdateTask::mSimulationTime;
        }
        else
        {
            float simTime;
            float delta = GetSimulationDelta(lastTime, simTime);
            if (m_pActiveCameraData != NULL)
            {
                duration = (float)(m_pActiveCameraData->m_uKeyCount - 1) / 30.0f;
            }
            m_fAnimationTime += (delta * m_fAnimationSpeed) / duration;
            m_fLastSimulationTime = simTime;
        }
    }
    else
    {
        float duration;
        if (m_pActiveCameraData != NULL)
        {
            duration = (float)(m_pActiveCameraData->m_uKeyCount - 1) / 30.0f;
        }
        else
        {
            duration = 0.0f;
        }
        m_fAnimationTime += (dt * m_fAnimationSpeed) / duration;
    }

    if (m_fAnimationTime >= 1.0f)
    {
        if (m_bCyclic)
        {
            m_fAnimationTime -= 1.0f;
        }
        else
        {
            m_fAnimationTime = 1.0f;
        }
        if (m_EndOfAnimationCallback != NULL)
        {
            m_EndOfAnimationCallback();
        }
    }

    BuildAnimViewMatrix(m_matView);
}

/**
 * Offset/Address/Size: 0x0 | 0x801A4BF4 | size: 0x150
 */
void cAnimCamera::ManualUpdate(float dt)
{
    if (m_bUseSimulationTime)
    {
        float lastTime = m_fLastSimulationTime;
        float duration = 0.0f;
        if (lastTime < duration)
        {
            m_fLastSimulationTime = FixedUpdateTask::mSimulationTime;
        }
        else
        {
            float simTime = FixedUpdateTask::mSimulationTime;
            float delta = simTime - lastTime;
            if (m_pActiveCameraData != NULL)
            {
                duration = (float)(m_pActiveCameraData->m_uKeyCount - 1) / 30.0f;
            }
            m_fAnimationTime += (delta * m_fAnimationSpeed) / duration;
            m_fLastSimulationTime = simTime;
        }
    }
    else
    {
        float duration;
        if (m_pActiveCameraData != NULL)
        {
            duration = (float)(m_pActiveCameraData->m_uKeyCount - 1) / 30.0f;
        }
        else
        {
            duration = 0.0f;
        }
        m_fAnimationTime += (dt * m_fAnimationSpeed) / duration;
    }

    if (m_fAnimationTime >= 1.0f)
    {
        if (m_bCyclic)
        {
            m_fAnimationTime -= 1.0f;
        }
        else
        {
            m_fAnimationTime = 1.0f;
        }
        if (m_EndOfAnimationCallback != NULL)
        {
            m_EndOfAnimationCallback();
        }
    }

    BuildAnimViewMatrix(m_matView);
}
