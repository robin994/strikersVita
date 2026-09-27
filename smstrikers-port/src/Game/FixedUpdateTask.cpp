#include "Game/FixedUpdateTask.h"

#include "Game/Ball.h"
#include "Game/BasicStadium.h"
#include "Game/CharacterTemplate.h"
#include "Game/Field.h"
#include "Game/FlickDetection.h"
#include "Game/Game.h"
#include "Game/Physics/Physics.h"
#include "Game/Physics/PhysicsAIBall.h"
#include "Game/Render/NetMesh.h"
#include "Game/Render/SidelineExplodable.h"
#include "Game/ReplayManager.h"
#include "Game/Team.h"
#include "Game/Player.h"
#include "NL/platpad.h"
#include "NL/gl/gl.h"
#include "port/determinism.h"
#include "port/framerate.h"
#include "port/host.h"

#include <algorithm>
#include <cstdlib>
#if defined(PORT_VITA)
#include <aurora_vita_backend.hpp>
#endif

float g_fFixedUpdateTick = 0.02f;
extern PhysicsWorld* g_PhysicsWorld;

bool g_bRunSimAndRenderInLockStep;
float g_fSimulationTick = g_fFixedUpdateTick;

float FixedUpdateTask::mAccumulatedDeltaT;
float FixedUpdateTask::mSimulationTime;
float FixedUpdateTask::mfFrameLockTime;
float FixedUpdateTask::mTimeScale = 1.0f;

namespace
{
#if defined(PORT_VITA)
struct PrePhysicsPoseJob
{
    bool poseLocal[10];
};

bool PreparePlayerPoses(void* opaque, size_t begin, size_t end, uint32_t) noexcept
{
    PrePhysicsPoseJob& job = *static_cast<PrePhysicsPoseJob*>(opaque);
    for (size_t i = begin; i < end; ++i)
        job.poseLocal[i] = static_cast<cPlayer*>(g_pCharacters[i])->PreparePrePhysicsPose(g_fSimulationTick);
    return true;
}

bool PortGameParallelEnabled()
{
    static const bool enabled = [] {
        const char* value = getenv("STRIKERS_VITA_GAME_PARALLEL");
        return value == NULL || value[0] != '0';
    }();
    return enabled;
}

bool PortGameplayFrameskipEnabled()
{
    static int initialized;
    static bool enabled;
    if (!initialized)
    {
        const char* value = getenv("STRIKERS_VITA_FRAMESKIP");
        enabled = value != NULL && value[0] != '\0' && value[0] != '0';
        initialized = 1;
    }
    return enabled;
}

// Rendering can temporarily fall far below the 60 Hz target while the Vita GX
// backend is being optimized. Do not turn that into slow motion: measure the
// real wall time between gameplay updates, let the existing fixed-update loop
// consume that elapsed time, and use the engine's native glDiscardFrame path to
// drop a few presentations while the simulation catches up.
float PortGameplayWallDelta(float fallbackDt)
{
    static unsigned long long s_lastNs;
    const unsigned long long now = port_monotonic_ns();
    float result = fallbackDt;

    if (s_lastNs != 0 && now > s_lastNs)
    {
        const double seconds = (double)(now - s_lastNs) / 1000000000.0;
        // Bound pathological pauses/loading hitches so one stalled frame cannot
        // request hundreds of physics steps. 500 ms still covers the current
        // 3-5 FPS debugging regime without forcing slow motion.
        result = (float)std::min(seconds, 0.5);
    }
    s_lastNs = now;
    return result;
}

void PortScheduleGameplayFrameSkip(float wallDt)
{
    double limitHz = 60.0;
    PortFrameLimitInfo(&limitHz, nullptr, nullptr, nullptr);
    if (limitHz < 20.0 || limitHz > 240.0)
        limitHz = 60.0;

    const double frameSeconds = 1.0 / limitHz;
    if ((double)wallDt <= frameSeconds * 1.5)
        return;

    int missed = (int)((double)wallDt / frameSeconds) - 1;
    if (missed < 1)
        return;
    if (missed > 3)
        missed = 3;
    glDiscardFrame(missed);
}
#endif
} // namespace

extern "C" float* PortSimTimeScalePtr(void)
{
    return &FixedUpdateTask::mTimeScale;
}

/**
 * Offset/Address/Size: 0x2D8 | 0x8016E608 | size: 0x30
 */
FixedUpdateTask::FixedUpdateTask()
{
    mAccumulatedDeltaT = g_fFixedUpdateTick;
    mSimulationTime = 0.f;
    mfFrameLockTime = 0.f;
}

/**
 * Offset/Address/Size: 0x2CC | 0x8016E5FC | size: 0xC
 */
const char* FixedUpdateTask::GetName()
{
    return "Game Fixed Update";
}

/**
 * Offset/Address/Size: 0x2C4 | 0x8016E5F4 | size: 0x8
 */
float FixedUpdateTask::GetPhysicsUpdateTick()
{
    return g_fSimulationTick;
}

/**
 * Offset/Address/Size: 0x280 | 0x8016E5B0 | size: 0x44
 */
void FixedUpdateTask::DecrementFrameLock(float fDeltaT)
{
    mfFrameLockTime -= fDeltaT;
    if (mfFrameLockTime < 0.f)
    {
        nlTaskManager::SetNextState(2);
        mfFrameLockTime = 0.f;
    }
}

/**
 * Offset/Address/Size: 0x0 | 0x8016E330 | size: 0x280
 */
void FixedUpdateTask::Run(float dt)
{
    if (nlTaskManager::m_pInstance->m_CurrState == 2)
    {
        float simulationTick;

#if defined(PORT_VITA)
        float gameplayDt = dt;
        if (!PortFixedTimestep() && PortGameplayFrameskipEnabled())
        {
            gameplayDt = PortGameplayWallDelta(dt);
            PortScheduleGameplayFrameSkip(gameplayDt);
        }
#else
        const float gameplayDt = dt;
#endif

        mAccumulatedDeltaT += gameplayDt * mTimeScale;

        while (g_bRunSimAndRenderInLockStep || mAccumulatedDeltaT >= g_fFixedUpdateTick)
        {
            UseFixedUpdatePad();

            UpdatePlatPad(simulationTick = g_fFixedUpdateTick);
            cPadManager::Update(simulationTick);
            FlickDetection::Update();

            mAccumulatedDeltaT -= g_fFixedUpdateTick;
            if (g_bRunSimAndRenderInLockStep)
            {
                mAccumulatedDeltaT = 0.0f;
            }

            CallFixedUpdateTasks();

            if (g_bRunSimAndRenderInLockStep)
            {
                break;
            }
        }
    }

    UseDefaultPad();
    UpdatePlatPad(dt);
    cPadManager::Update(dt);
    FlickDetection::Update();
}

void FixedUpdateTask::AIUpdateTask(float fDeltaT)
{
    g_pGame->PreUpdate(fDeltaT);
    g_pGame->Update(fDeltaT);
}

void FixedUpdateTask::PrePhysicsAITask(float fDeltaT)
{
#if defined(PORT_VITA)
    if (PortGameParallelEnabled() && aurora::vita::worker_threads() != 0)
    {
        PrePhysicsPoseJob job = {};
        (void)aurora::vita::parallel_for(10, 3, PreparePlayerPoses, &job);

        // Physics objects and the held ball are deliberately committed in the
        // original deterministic character order on CPU0.
        for (int i = 0; i < 10; ++i)
            static_cast<cPlayer*>(g_pCharacters[i])->CommitPrePhysicsUpdate(job.poseLocal[i]);
        return;
    }
#endif
    int i;
    for (i = 0; i < 10; i++)
    {
        g_pCharacters[i]->PrePhysicsUpdate(fDeltaT);
    }
}

void FixedUpdateTask::PostPhysicsAITask(float fDeltaT)
{
    int i;
    for (i = 0; i < 10; i++)
    {
        g_pCharacters[i]->PostPhysicsUpdate();
    }
    g_pBall->PostPhysicsUpdate(fDeltaT);
}

void FixedUpdateTask::CallFixedUpdateTasks()
{
    mSimulationTime += g_fSimulationTick;
    AIUpdateTask(g_fSimulationTick);
    BasicStadium::GetCurrentStadium()->mpNPCManager->UpdateAINPCs(g_fSimulationTick);
    PrePhysicsAITask(g_fSimulationTick);
    PhysicsUpdate(g_PhysicsWorld, g_fSimulationTick);
    PostPhysicsAITask(g_fSimulationTick);

    if (NetMesh::s_bAnimatedNetMeshEnabled)
    {
        bool i = true;
        float goalieX = (float)fabs(g_pTeams[0]->GetGoalie()->m_v3Position.x);
        if (goalieX > cField::GetGoalLineX(1U))
        {
        }
        else
        {
            goalieX = (float)fabs(g_pTeams[1]->GetGoalie()->m_v3Position.x);
            if (goalieX > cField::GetGoalLineX(1U))
            {
            }
            else
            {
                i = false;
            }
        }

        cBall* pBall = g_pBall;
        PhysicsAIBall* pPhysicsBall = pBall->m_pPhysicsBall;
        NetMesh::spPositiveXNetMesh->Update(g_fSimulationTick, pBall->m_v3Position, pBall->m_v3PrevPosition, i, pPhysicsBall);
        pPhysicsBall = (pBall = g_pBall)->m_pPhysicsBall;
        NetMesh::spNegativeXNetMesh->Update(g_fSimulationTick, pBall->m_v3Position, pBall->m_v3PrevPosition, i, pPhysicsBall);
    }

    SidelineExplodableManager::Update(g_fSimulationTick);
    ReplayManager::Instance()->GrabSnapshot();
}
