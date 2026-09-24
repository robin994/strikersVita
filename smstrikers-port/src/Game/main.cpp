#include "port/prompts.h"
#include "port/region.h"  // PORT: one binary, three discs
#include "port/host.h"
#include "types.h"
#include "NL/nlBind.h"
#include "NL/nlFunction.h"
#include "Game/main.h"

#if defined(PORT_USE_AURORA)
#if defined(PORT_VITA)
#include <aurora_vita_backend.hpp>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <psp2/power.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <SDL3/SDL_events.h>
#else
#include <aurora/aurora.h>
#include <aurora/gfx.h>   // aurora_present_waits_for_vblank
#endif
#include <dolphin/gx/GXAurora.h>   // AuroraSetViewportPolicy
#include <dolphin/vi.h>                // VILockAspectRatio
#include "port/aspect.h"
#include "port/framerate.h"
#if !defined(PORT_VITA)
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#endif
#include "port/overlay.h"
extern "C" void PortDebugFrame(void);   // PORT: defined in Game.cpp
#include "port/launch.h"
#if !defined(PORT_VITA)
#include "port/discord.h"
#include "port/shaders.h"   // PORT: the shader stage, held on the memory card screen
#include <aurora/main.h>   // #define main aurora_main
#include <aurora/event.h>
#endif
#include <stdio.h>   // PORT: snprintf
#endif
#include "port/benchmark.h"
#include "port/audio.h"
#include "port/vita_profiler.h"
#include "port/determinism.h"
#include "port/config.h"
#include "port/texture_packs.h"
#include "Game/Audio/AudioStream.h"
#include "Game/Sys/audio.h"
#include "Game/Sys/clock.h"
#include "NL/gl/glView.h"
#include "NL/gl/glTexture.h"
#include "NL/gl/glState.h"
#include "NL/gl/gl.h"
#include "NL/gl/glAppAttach.h"
#include "NL/gl/glMemory.h"
#include "NL/glx/glxMemory.h"
#include "Game/Effects/ParticleSystem.h"
#include "Game/Transitions/ModelTransition.h"
#include "NL/nlConfig.h"
#include "NL/nlMain.h"
#include "NL/nlFileGC.h"
#include "NL/nlLocalization.h"
#include "NL/nlString.h"
#include "NL/platpad.h"
#include "Game/Game.h"
#include "Game/ProfileTask.h"
#include "Game/Sys/FloatingPointExceptions.h"
#include "Game/Sys/CallStackDumper.h"
#include "Game/Sys/eventman.h"
#include "Game/Sys/gcmemcard.h"
#include "Game/Sys/debug.h"
#include "Game/Audio/AudioLoader.h"
#include "Game/Audio/AudioEventHandler.h"
#include "Game/Audio/CrowdMood.h"
#include "Game/FE/LidOpenMessage.h"
#include "Game/FE/FEAudio.h"
#include "Game/FE/feInput.h"
#include "Game/FE/feManager.h"
#include "Game/BeginFrameTask.h"
#include "Game/DispatchEventsTask.h"
#include "Game/PlatPadUpdateTask.h"
#include "Game/FixedUpdateTask.h"
#include "Game/WorldUpdateTask.h"
#include "Game/GameRenderTask.h"
#include "Game/FrontEndTask.h"
#include "Game/ParticleUpdateTask.h"
#include "Game/TweakerTask.h"
#include "Game/EndFrameTask.h"
#include "Game/TransitionTask.h"
#include "Game/ComUpdateTask.h"
#include "Game/TestTask.h"
#include "Game/Loader/LoadingManager.h"
#include "Game/GL/GLInventory.h"
#include "Game/Debug/ShapeRender.h"
#include "Game/Debug/FrameCounter.h"
#include "Game/GameInfo.h"
#include "Game/DB/StatsTracker.h"
#include "Game/DB/SaveLoad.h"
#include "Game/Render/Wiper.h"
#include "Game/Render/depthoffield.h"
#include "Game/Render/FlareHandler.h"
#include "Game/AI/AIPad.h"
#include "Game/OverlayManager.h"
#include "Game/PadActions.h"
#include "Game/Pad/FlickDetection.h"
#include "Game/Replay.h"
#include "Game/ReplayManager.h"
#include "Game/ReplayChoreo.h"
#include "Game/NisPlayer.h"
#include "Game/Render/Presentation.h"
#include "Game/Render/CrowdManager.h"
#include "Game/ResetTask.h"
#include "NL/nlDebug.h"
#include "NL/nlTask.h"
#include "dolphin/os/OSThread.h"
#include "dolphin/si.h"
#include "dolphin/card.h"

class AudioUpdateTask : public nlTask
{
public:
    /**
     * Offset/Address/Size: 0x1BB4 | 0x801750C4 | size: 0x8
     */
    virtual const char* GetName() { return "Audio"; }
    /**
     * Offset/Address/Size: 0x1BBC | 0x801750CC | size: 0x20
     */
    virtual void Run(float dt)
    {
        Audio::Update(dt);
    }
};

class ClockUpdateTask : public nlTask
{
public:
    /**
     * Offset/Address/Size: 0x1BDC | 0x801750EC | size: 0x8
     */
    virtual const char* GetName() { return "Clock"; }
    /**
     * Offset/Address/Size: 0x1BE4 | 0x801750F4 | size: 0x20
     */
    virtual void Run(float dt)
    {
        ClockManager::Update(dt);
    }
};

#if defined(PORT_VITA)
extern "C" {
// Reserve a large newlib heap now that ATTRIBUTE2 grants the title the
// extended Vita user-memory budget. This is consumed by newlib malloc/free;
// the game's own 96 MiB memblock allocator remains independently bounded.
// Keep this at VitaSDK's normal 128 MiB: the application also reserves a
// separate 96 MiB USER_RW memblock for the GameCube VM/standard allocators.
// A 256 MiB newlib heap starves that memblock before main() even starts.
unsigned int _newlib_heap_size_user = 128u * 1024u * 1024u;
}

// One opt-in diagnostic snapshot, after the selected completed 3D frame. The
// display queue wait and disk write are intentionally not part of normal play.
static void VitaMaybeCaptureFrame()
{
    static bool initialized = false;
    static unsigned long target = 0;
    static unsigned long heavyFrames = 0;
    if (!initialized)
    {
        initialized = true;
        const char* value = getenv("STRIKERS_VITA_SNAPSHOT_3D_FRAME");
        if (value != NULL)
            target = strtoul(value, NULL, 10);
    }
    if (target == 0)
        return;
#if defined(STRIKERS_VITA_GX_THREAD)
    // Async-GX builds need an explicit fence before reading renderer state.
    aurora::vita::wait_for_render_idle();
#endif
    if (aurora::vita::telemetry().frame().counters.triangles < 10000)
        return;
    if (++heavyFrames != target)
        return;
    sceGxmDisplayQueueFinish();
    SceDisplayFrameBuf fb = {};
    fb.size = sizeof(fb);
    const int result = sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_IMMEDIATE);
    if (result < 0 || fb.base == NULL || fb.width == 0 || fb.width > 4096
        || fb.height == 0 || fb.height > 4096 || fb.pitch < fb.width || fb.pixelformat != 0)
    {
        OSReport("[vita] snapshot failed: framebuffer rc=%d format=%u\n", result, fb.pixelformat);
        return;
    }
    unsigned char* row = (unsigned char*)malloc(fb.width * 3u);
    if (row == NULL)
        return;
    char path[128];
    snprintf(path, sizeof(path), "ux0:data/strikersVita/debug_frame_3d_%lu.ppm", target);
    FILE* output = fopen(path, "wb");
    bool ok = output != NULL;
    if (output != NULL)
    {
        fprintf(output, "P6\n%u %u\n255\n", fb.width, fb.height);
        const unsigned char* pixels = (const unsigned char*)fb.base;
        for (unsigned int y = 0; y < fb.height && ok; ++y)
        {
            const unsigned char* source = pixels + y * fb.pitch * 4u;
            for (unsigned int x = 0; x < fb.width; ++x)
            {
                row[x*3u] = source[x*4u];
                row[x*3u+1u] = source[x*4u+1u];
                row[x*3u+2u] = source[x*4u+2u];
            }
            ok = fwrite(row, 3u, fb.width, output) == fb.width;
        }
        if (fclose(output) != 0)
            ok = false;
    }
    free(row);
    OSReport("[vita] snapshot 3d_frame=%lu aurora_frame=%lu ok=%d path=%s\n",
             heavyFrames, (unsigned long)aurora::vita::telemetry().frame().frame, ok ? 1 : 0, path);
}
#endif

bool g_bProfiling = false;
bool g_bTweaking = false;
bool g_e3_Build = false;
bool g_Europe = false;
bool g_bFranticPausing = false;
nlLocalization::nlLanguage g_Language = nlLocalization::LangEnglish;
LoadingManager* g_pTheLoadingManagerTask = nullptr;

FrameCounter g_FrameCounter("frame", "send");

static ComUpdateTask comUpdateTask;
static TransitionTask transitionTask;
static DispatchEventsTask dispatchEventsTask;
static PlatPadUpdateTask platPadUpdateTask;
static FrontEndTask frontEndTask;
static WorldUpdateTask worldUpdateTask;
static GameRenderTask gameRenderTask;
static ParticleUpdateTask particleUpdateTask;
static ClockUpdateTask clockUpdateTask;
static BeginFrameTask beginFrameTask;
static AudioUpdateTask audioUpdateTask;
static EndFrameTask endFrameTask;
static TweakerTask tweakerTask;
static FixedUpdateTask fixedUpdateTask;

static TestTask testTask;
static ResetTask resetTask;

static void Initialize();
static void SetupViews();
static void AddTasks();
static void PreInitFS();
static void DoMemCheck();

/**
 * Offset/Address/Size: 0x1BAC | 0x801750BC | size: 0x8
 */
const int* GetRegion()
{
    // PORT: read from the disc header at DVDInit rather than compiled in, one binary runs all three discs.
    static int g_Region = -1;
    g_Region = port_region();
    return &g_Region;
}

static void PreInitFS()
{
}

/**
 * Offset/Address/Size: 0x354 | 0x80173864 | size: 0x1858
 */
static void Initialize()
{
    nlRegHandleDVDMessageCB(Function<void(int)>(DisplayDVDMessageSebring));
    nlRegHandleDVDAllClearCB(Function<void(int)>(DVDAllClearSebring));
    nlRegCheckForResetFromFSCB(Function<FnVoidVoid>(
        Bind<void>(MemFun<ResetTask, void>(&ResetTask::FSCheckForReset), &resetTask)));

    nlInit();
    bool glStartupSuccessful = glStartup();
    if (!glStartupSuccessful)
    {
        nlBreak();
    }
    // PORT: STRIKERS_SEED replaces the clock, so that a headless run can be repeated; unset.
    {
        unsigned int fixedSeed;
        if (PortFixedSeed(&fixedSeed))
            nlSetRandomSeed(fixedSeed, &nlDefaultSeed);
        else
            nlSetRandomSeed(OSGetTick(), &nlDefaultSeed);
    }
    bool globalTexturesLoaded = glLoadTextureBundle("global.glt");
    if (!globalTexturesLoaded)
    {
        nlBreak();
    }

    Config::Global().LoadFromFile("common.ini");
    Config::Global().LoadFromFile("platform.ini");
    Config::Global().LoadFromFile("locale.ini");
    Config::Global().LoadFromFile("user.ini");

    {
        static const char* const kTeamVars[4][2] = {
            {"STRIKERS_TEAM1", "team1"}, {"STRIKERS_TEAM2", "team2"},
            {"STRIKERS_SIDEKICK1", "sidekick1"}, {"STRIKERS_SIDEKICK2", "sidekick2"}};
        for (int i = 0; i < 4; ++i)
        {
            const char* v = getenv(kTeamVars[i][0]);
            if (v != NULL && *v != '\0')
                Config::Global().Set(kTeamVars[i][1], v);
        }
    }

    GetConfigBool(Config::Global(), "DiskAccess", false);
    if (GetConfigBool(Config::Global(), "e3_build", false))
    {
        g_e3_Build = true;
    }
    if (GetConfigBool(Config::Global(), "frantic_pausing", false))
    {
        g_bFranticPausing = true;
    }

    g_bFrameSmiler = GetConfigBool(Config::Global(), "Frame_Smiler", false);
    g_bFrameStatsOnScreen = GetConfigBool(Config::Global(), "Frame_Stats_On_Screen", false);
    g_bFrameStatsOnDisk = GetConfigBool(Config::Global(), "Frame_Stats_On_Disk", false);
    g_bRunSimAndRenderInLockStep = GetConfigBool(Config::Global(), "lockstep", false);

    // PORT: 64 bytes fits a console Event and its payload; a host Event is larger.
    {
        unsigned long uEventPool = 4096;
        const char* poolEnv = getenv("STRIKERS_EVENT_POOL");
        if (poolEnv != NULL && *poolEnv != '\0')
        {
            // Smaller as well as larger: shrinking the pool is how a burst that is too rare to catch at 128 gets caught at all.
            unsigned long v = strtoul(poolEnv, NULL, 10);
            if (v >= 16)
                uEventPool = v;
        }
        EventManager::Create(uEventPool, sizeof(Event) + 3 * (64 - 0x14));
    }
    g_pEventManager->AddEventHandler(FrontEnd::FEEventHandler, NULL, 2);
    g_pEventManager->AddEventHandler(OverlayManager::FEEventHandler, NULL, (u32)-1);
    g_pEventManager->AddEventHandler(FEAudioEventHandler, NULL, 2);
    g_pEventManager->AddEventHandler(Audio::AudioEventHandler, NULL, 0x16);

    nlTaskManager::Startup(0x10000);
    tDebugPrintManager::Initialize();
    ClockManager::Initialize();

    // PORT: the default is the game's, sound on; STRIKERS_AUDIO is checked after the config file so that it wins, because user.ini lives in the disc data.
    AudioLoader::gbDisableAudio = GetConfigBool(Config::Global(), "no_audio", false);
    {
        const char* portAudioEnv = getenv("STRIKERS_AUDIO");
        if (portAudioEnv != NULL && *portAudioEnv != '\0')
            AudioLoader::gbDisableAudio = (*portAudioEnv == '0');
    }
    AudioLoader::gbStream = !GetConfigBool(Config::Global(), "no_stream", false);
    AudioLoader::g_BGM_Off = GetConfigBool(Config::Global(), "no_bgm", false);
    AudioLoader::gbDisableCrowd = GetConfigBool(Config::Global(), "no_crowd", false);
    AudioLoader::gbDisableReverb = GetConfigBool(Config::Global(), "no_reverb", false);

    CrowdMood::ReadConfig();

    if (!AudioLoader::gbDisableAudio)
    {
        AudioLoader::Initialize();
        AudioLoader::SetupSoundBuffers();
        AudioLoader::LoadFEButtonSoundGroup();
        Audio::InitStreaming();
    }

    g_pEventManager->AddEventHandler(ReplayManager::EventHandler, ReplayManager::Instance(), (u32)-1);
    g_pEventManager->AddEventHandler(ReplayChoreo::EventHandler, &ReplayChoreo::Instance(), (u32)-1);
    g_pEventManager->AddEventHandler(NisPlayer::EventHandler, NisPlayer::Instance(), (u32)-1);
    g_pEventManager->AddEventHandler(Presentation::EventHandler, &Presentation::Instance(), (u32)-1);
    g_pEventManager->AddEventHandler(CrowdManager::EventHandler, &CrowdManager::instance, (u32)-1);

    glResourceMark();
    glInventory.Create();
    InitPads();
    SISetSamplingRate(0);
    PADSetSamplingCallback(VBlankPadUpdate);
    AIPadManager::Startup();
    FEInput::Initialize();
    FlickDetection::Initialize();
#if defined(PORT_USE_AURORA)
    // game code / maker code, read from the disc that is loaded
    CARDInit(port_disc_game_code(), port_disc_maker_code());
#else
    CARDInit();
#endif
    MemCard::s_InitDone = true;

    g_pTheLoadingManagerTask = new (nlMalloc(sizeof(LoadingManager), 8, false)) LoadingManager(0x14);

    if (!GetConfigBool(Config::Global(), "DisableComListener", false))
    {
        comUpdateTask.Initialize();
    }

    TransitionTask::sm_pGlobalTask = &transitionTask;
    transitionTask.Initialize(*g_pTheLoadingManagerTask);

    testTask.Initialize();
    nlLocalization::Initialize();

    DVDDiskID* diskid = DVDGetCurrentDiskID();
    if (diskid->gameName[0] == 'G' && diskid->gameName[1] == '4' && diskid->gameName[2] == 'Q' && diskid->gameName[3] == 'P')
    {
        switch (OSGetLanguage())
        {
        case 1:
            g_Language = nlLocalization::LangGerman;
            break;
        case 2:
            g_Language = nlLocalization::LangFrench;
            break;
        case 3:
            g_Language = nlLocalization::LangSpanish;
            break;
        case 4:
            g_Language = nlLocalization::LangItalian;
            break;
        default:
            g_Language = nlLocalization::LangUKEnglish;
            break;
        }
        g_Europe = true;
    }
    else if (diskid->gameName[0] == 'G' && diskid->gameName[1] == '4' && diskid->gameName[2] == 'Q' && diskid->gameName[3] == 'J')
    {
        // PORT: the Japanese disc carries every language's strings, fonts and menus, so `language` picks one; unset is Japanese.
        static const nlLocalization::nlLanguage kJapaneseDisc[] = {
            nlLocalization::LangEnglish, nlLocalization::LangGerman,  nlLocalization::LangFrench,
            nlLocalization::LangSpanish, nlLocalization::LangItalian, nlLocalization::LangJapanese,
        };
        const int language = port_language();
        g_Language = language >= 0 ? kJapaneseDisc[language] : nlLocalization::LangJapanese;
    }
    else if (diskid->gameName[0] == 'G' && diskid->gameName[1] == '4' && diskid->gameName[2] == 'Q' && diskid->gameName[3] == 'E')
    {
        g_Language = nlLocalization::LangEnglish;
        // PORT: the American disc has English only, so a `language` line is reported as ignored.
        if (port_language() >= 0)
            OSReport("[port] language: ignored, the American disc plays in English\n");
    }
    else
    {
        BasicString<char, Detail::TempStringAllocator> userlanguage = Config::Global().Get<BasicString<char, Detail::TempStringAllocator> >(
            "Language", BasicString<char, Detail::TempStringAllocator>("eng"));

        if (nlStrICmp(userlanguage.c_str(), "eng") == 0)
        {
            g_Language = nlLocalization::LangEnglish;
        }
        else if (nlStrICmp(userlanguage.c_str(), "jpn") == 0)
        {
            g_Language = nlLocalization::LangJapanese;
        }
        else if (nlStrICmp(userlanguage.c_str(), "deu") == 0)
        {
            g_Language = nlLocalization::LangGerman;
        }
        else if (nlStrICmp(userlanguage.c_str(), "fre") == 0)
        {
            g_Language = nlLocalization::LangFrench;
        }
        else if (nlStrICmp(userlanguage.c_str(), "ita") == 0)
        {
            g_Language = nlLocalization::LangItalian;
        }
        else if (nlStrICmp(userlanguage.c_str(), "spa") == 0)
        {
            g_Language = nlLocalization::LangSpanish;
        }
        else if (nlStrICmp(userlanguage.c_str(), "uke") == 0)
        {
            g_Language = nlLocalization::LangUKEnglish;
        }
        else if (nlStrICmp(userlanguage.c_str(), "longest") == 0)
        {
            g_Language = nlLocalization::LangLongestStrings;
        }
    }
    OSReport("[port] language: %s\n", nlLocalization::LanguageName[g_Language]);   // PORT: which table the disc and `language` chose

    LoadMemoryCardIconData();

    if (nlSingleton<GameInfoManager>::s_pInstance == NULL)
    {
        nlSingleton<GameInfoManager>::s_pInstance = new (nlMalloc(sizeof(GameInfoManager), 8, false)) GameInfoManager();
    }
    if (nlSingleton<StatsTracker>::s_pInstance == NULL)
    {
        nlSingleton<StatsTracker>::s_pInstance = new (nlMalloc(sizeof(StatsTracker), 8, false)) StatsTracker();
    }

    AddTasks();

    SetupViews();
    g_ShapeRenderer.Initialize();
    Wiper::Instance().Initialize();
    DepthOfFieldManager::instance.Initialize();
    FlareHandler::instance.Initialize();
    glAppStartup();

    BasicString<char, Detail::TempStringAllocator> skinString = Config::Global().Get<BasicString<char, Detail::TempStringAllocator> >("Skinning", BasicString<char, Detail::TempStringAllocator>("both"));
    BasicString<char, Detail::TempStringAllocator> replaySkin = Config::Global().Get<BasicString<char, Detail::TempStringAllocator> >("UserReplaySkinning", BasicString<char, Detail::TempStringAllocator>("blend"));
    if (skinString == "both")
    {
        BeginFrameTask::s_GameplaySkin = eModelSkin_Both;
    }
    else if (skinString == "rigid")
    {
        BeginFrameTask::s_GameplaySkin = eModelSkin_Rigid;
    }
    else
    {
        BeginFrameTask::s_GameplaySkin = eModelSkin_Blend;
    }
    if (replaySkin == "both")
    {
        BeginFrameTask::s_ReplaySkin = eModelSkin_Both;
    }
    else if (replaySkin == "rigid")
    {
        BeginFrameTask::s_ReplaySkin = eModelSkin_Rigid;
    }
    else
    {
        BeginFrameTask::s_ReplaySkin = eModelSkin_Blend;
    }
}

static void AddTasks()
{
    nlTaskManager::AddTask(&resetTask, 0, -1);
    nlTaskManager::AddTask(&beginFrameTask, 2, -1);
    nlTaskManager::AddTask(&dispatchEventsTask, 0x14, -1);
    nlTaskManager::AddTask(&platPadUpdateTask, 3, -1);
    nlTaskManager::AddTask(&clockUpdateTask, 5, -1);
    nlTaskManager::AddTask(&fixedUpdateTask, 7, -1);
    nlTaskManager::AddTask(&worldUpdateTask, 8, 0x20013);
    nlTaskManager::AddTask(&gameRenderTask, 0xa, 0x20113);
    nlTaskManager::AddTask(&frontEndTask, 0xc, 0x117);
    nlTaskManager::AddTask(&particleUpdateTask, 0xb, 0x20113);
    nlTaskManager::AddTask(&tweakerTask, 0xd, -1);
    nlTaskManager::AddTask(&audioUpdateTask, 0xe, -1);
    nlTaskManager::AddTask(&endFrameTask, 0xf, -1);
    nlTaskManager::AddTask(&transitionTask, 1, -1);
    nlTaskManager::AddTask(g_pTheLoadingManagerTask, 6, -1);

    if (!GetConfigBool(Config::Global(), "DisableComListener", false))
    {
        nlTaskManager::AddTask(&comUpdateTask, 0x10, -1);
    }
    if (GetConfigBool(Config::Global(), "test/enable", false))
    {
        nlTaskManager::AddTask(&testTask, 0x12, -1);
    }
}

/**
 * Offset/Address/Size: 0x224 | 0x80173734 | size: 0x130
 */
static void SetupViews()
{
    static eGLView sort_none[] = {
        GLV_Shadow0, GLV_Shadow1, GLV_UnsortedPerspective, GLV_InvisiblePlane, GLV_ElectricFence, GLV_UnsortedOrtho, GLV_ShadowBlend0, GLV_ShadowBlend1, GLV_Debug, GLV_Transitions, GLV_CoPlanar0, GLV_CoPlanar
    };

    static eGLView disabled_views[] = {
        GLV_ShadowBlend0, GLV_ShadowBlend1, GLV_ScreenBlur, GLV_ScreenBlur2
    };

    for (int iview = 0; iview < GLV_Num; iview++)
    {
        glViewSetTarget((eGLView)iview, GLTG_Main);
    }

    glViewSetSortMode(GLV_FrontEnd, GLVSort_TransformedDepth);
    glViewSetSortMode(GLV_Anark, GLVSort_Reverse);

    for (int iview = 0; iview < sizeof(sort_none) / sizeof(eGLView); iview++)
    {
        glViewSetSortMode(sort_none[iview], GLVSort_None);
    }

    for (int iview = 0; iview < sizeof(disabled_views) / sizeof(eGLView); iview++)
    {
        glViewSetEnable(disabled_views[iview], false);
    }

    u32 uWarbleTexture = glGetTexture("target/warble");
    bool bWarbleLoaded = glTextureLoad(uWarbleTexture);
    if (!bWarbleLoaded)
    {
        glViewSetEnable(GLV_Warble, false);
        glViewSetEnable(GLV_WarbleBlend, false);
    }

    glViewSetDepthClear(GLV_CameraSpace, true);
    glViewSetDepthClear(GLV_Transitions, true);
    glViewSetDepthClear(GLV_Transitions3D, true);
    glViewSetDepthClear(GLV_Anark3D_BG, true);
    glViewSetDepthClear(GLV_Anark3D_FG, true);

    ParticleSystem::ClearViews();
    ParticleSystem::AddView(GLV_Particles);

    ModeledScreenTransition::s_3DView = GLV_Transitions3D;
}

static void DoMemCheck()
{
}

/**
 * Offset/Address/Size: 0x0 | 0x80173510 | size: 0x224
 */
#if defined(PORT_USE_AURORA)
// Set by main() so the frame loop can end cleanly when the window is closed.
static bool s_portRunning = true;
static const char* s_portExitReason = NULL;

// src/platform/input.cpp, default keyboard mapping for port 0.
extern "C" void PortInstallKeyboardBindings(void);
extern "C" void PortUpdateSyntheticInput(unsigned long frame);
// src/platform/aurora_compat.c, stands in for the VI retrace interrupt that drove controller sampling on console.
extern "C" void PortInvokePadSamplingCallback(void);

static unsigned long s_portFrame = 0;

#if defined(PORT_VITA)
enum VitaShaderProfile
{
    VITA_SHADER_CONTROL = 0,
    VITA_SHADER_WARM,
    VITA_SHADER_SEALED,
};

#ifndef STRIKERS_VITA_SHADER_PROFILE_DEFAULT
#define STRIKERS_VITA_SHADER_PROFILE_DEFAULT "SEALED"
#endif

static VitaShaderProfile s_vitaShaderProfile = VITA_SHADER_SEALED;
static bool s_vitaShaderGameplayActive = false;

static bool PortAsciiEqualIgnoreCase(const char* a, const char* b)
{
    if (a == NULL || b == NULL)
        return false;
    while (*a != '\0' && *b != '\0')
    {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb)
            return false;
    }
    return *a == '\0' && *b == '\0';
}

static VitaShaderProfile PortVitaReadShaderProfile()
{
    const char* value = getenv("STRIKERS_GXM_SHADER_PROFILE");
    if (value == NULL || *value == '\0')
        value = STRIKERS_VITA_SHADER_PROFILE_DEFAULT;

    if (PortAsciiEqualIgnoreCase(value, "CONTROL"))
        return VITA_SHADER_CONTROL;
    if (PortAsciiEqualIgnoreCase(value, "WARM"))
        return VITA_SHADER_WARM;
    return VITA_SHADER_SEALED;
}

static const char* PortVitaShaderProfileName(VitaShaderProfile profile)
{
    switch (profile)
    {
    case VITA_SHADER_CONTROL: return "CONTROL";
    case VITA_SHADER_WARM: return "WARM";
    default: return "SEALED";
    }
}

static void PortVitaSampleRendererStats()
{
    const aurora::vita::PerformanceSnapshot perf = aurora::vita::performance_snapshot();
    PortBenchRendererStats stats = {};
    stats.valid = 1;
    stats.shaderRuntimeCompilationEnabled = perf.shaderRuntimeCompilationEnabled ? 1 : 0;
    stats.shaderRuntimeCompiles = perf.shaderRuntimeCompiles;
    stats.shaderRuntimeCompileUs = perf.shaderRuntimeCompileUs;
    stats.shaderCompileBlockedMisses = perf.shaderCompileBlockedMisses;
    stats.shaderDiskCacheHits = perf.shaderDiskCacheHits;
    stats.shaderDiskCacheMisses = perf.shaderDiskCacheMisses;
    stats.frameUs = perf.frameUs;
    stats.rendererCpuFrameUs = perf.rendererCpuFrameUs;
    stats.displayQueueAverageUs = perf.displayQueueAverageUs;
    stats.displayQueueSamples = perf.displayQueueSamples;
    stats.displayQueueBlockedPercent = perf.displayQueueBlockedPercent;
    stats.nativeSceneCount = perf.nativeSceneCount;
    stats.nativeEfbCopies = perf.nativeEfbCopies;
    stats.staticGeometryHits = perf.staticGeometryHits;
    stats.staticGeometryMisses = perf.staticGeometryMisses;
    stats.staticGeometryLookupFallbacks = perf.staticGeometryLookupFallbacks;
    stats.staticGeometryBytes = perf.staticGeometryBytes;
    stats.staticGeometryEntries = perf.staticGeometryEntries;
    PortBenchSetRendererStats(&stats);
}

void PortVitaShaderCacheBeginLoading()
{
    if (s_vitaShaderProfile != VITA_SHADER_SEALED)
        return;
    if (!aurora::vita::runtime_shader_compilation_enabled())
        aurora::vita::set_runtime_shader_compilation_enabled(true);
}

void PortVitaShaderCacheEndLoading(bool enteringGameplay)
{
    if (s_vitaShaderProfile == VITA_SHADER_CONTROL)
        return;

    // The stable Aurora backend performs manifest/program-cache prewarm during
    // renderer initialization. It has no post-loading prewarm entry point.
    (void)enteringGameplay;

    if (s_vitaShaderProfile == VITA_SHADER_SEALED)
        aurora::vita::set_runtime_shader_compilation_enabled(false);
}

void PortVitaShaderCacheEnterGameplay()
{
    if (s_vitaShaderGameplayActive)
        return;

    PortVitaSampleRendererStats();
    PortBenchRendererGameplayStart();
    s_vitaShaderGameplayActive = true;
}

void PortVitaShaderCacheLeaveGameplay()
{
    if (!s_vitaShaderGameplayActive)
        return;

    // Snapshot before reopening the compiler so the gameplay delta cannot be
    // polluted by front-end variants compiled immediately after the transition.
    PortVitaSampleRendererStats();
    PortBenchRendererGameplayEnd();
    s_vitaShaderGameplayActive = false;
}
#endif

// Numeric environment overrides for the graphics configuration.
static unsigned long PortEnvU32(const char* name, unsigned long fallback)
{
    const char* v = getenv(name);
    if (v == NULL || *v == '\0')
        return fallback;
    return strtoul(v, NULL, 10);
}

static float PortEnvFloat(const char* name, float fallback)
{
    const char* v = getenv(name);
    if (v == NULL || *v == '\0')
        return fallback;
    return (float)atof(v);
}

// STRIKERS_CAPTURE writes a frame as a binary PPM; Aurora records the readback on the frame's own command encoder.
static void PortMaybeRequestCapture()
{
#if defined(PORT_VITA)
    // Vita capture is intentionally disabled during bring-up. The renderer has
    // its own frame tracing/telemetry and no desktop readback path is present.
    return;
#else
    const char* capturePath = getenv("STRIKERS_CAPTURE");
    if (capturePath == NULL)
        return;

    const char* whichEnv = getenv("STRIKERS_CAPTURE_FRAME");
    unsigned long which = whichEnv != NULL ? strtoul(whichEnv, NULL, 10) : 120;
    if (s_portFrame + 1 != which)
        return;

    OSReport("requesting capture of frame %lu to %s\n", which, capturePath);
    aurora_capture_frame(capturePath);
#endif
}

static void PortRequestManualShot()
{
#if defined(PORT_VITA)
    return;
#else
    static unsigned long s_shotIndex;
    static char s_shotPath[1024];

    const char* dir = getenv("STRIKERS_SHOT_DIR");
    // PORT: /tmp is not a directory on Windows.
#if defined(_WIN32)
    if (dir == NULL || *dir == '\0')
        dir = getenv("TEMP");
    if (dir == NULL || *dir == '\0')
        dir = ".";
#else
    if (dir == NULL || *dir == '\0')
        dir = "/tmp";
#endif

    snprintf(s_shotPath, sizeof s_shotPath, "%s/strikers-shot-%03lu.ppm",
             dir, s_shotIndex++);
    OSReport("[shot] frame %lu -> %s\n", s_portFrame, s_shotPath);
    aurora_capture_frame(s_shotPath);
#endif
}

// PORT: keep the picture the shape of the window; idempotent, because everything below the generation test is skipped unless the shape actually moved.
static void PortFollowWindowShape()
{
#if defined(PORT_VITA)
    const unsigned int gen = PortAspectGeneration();
    PortFollowRenderScale(544);
    PortSetWindowAspect(960, 544);
    if (PortAspectGeneration() != gen)
        PortApplyAspectChange();
#else
    const AuroraWindowSize ws = aurora_window_size();
    const unsigned int gen = PortAspectGeneration();

    // PORT: the automatic render scale follows the window height.
    PortFollowRenderScale(ws.height);
    PortSetWindowAspect(ws.width, ws.height);
    if (PortAspectGeneration() == gen)
        return;

    PortApplyAspectChange();
    aurora_apply_frame_buffer_resize();
#endif
}

#if !defined(PORT_VITA)
static SDL_Window* s_portWindow;
#endif

static void PortFollowDisplayRefresh()
{
#if defined(PORT_VITA)
    PortSetDisplayRefresh(60.0, 1);
#else
    if (s_portWindow == NULL)
        return;
    const SDL_DisplayMode* mode =
        SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_portWindow));
    PortSetDisplayRefresh(mode != NULL ? (double)mode->refresh_rate : 0.0,
                          aurora_present_waits_for_vblank() ? 1 : 0);
#endif
}

static void PortPumpAuroraEvents()
{
#if defined(PORT_VITA)
    SDL_PumpEvents();
    PortFollowWindowShape();
    SceCtrlData pad = {};
    static unsigned int s_exitComboFrames;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0)
    {
        const bool exitCombo =
            (pad.buttons & (SCE_CTRL_START | SCE_CTRL_SELECT))
                == (SCE_CTRL_START | SCE_CTRL_SELECT);
        if (exitCombo)
        {
            // PORT: START+SELECT used to quit on a single poll.  During FE
            // bring-up that is far too easy to trigger while testing buttons,
            // and looks exactly like an unexplained clean process exit.  Require
            // roughly two seconds at 60 Hz and name the reason in the log.
            if (++s_exitComboFrames >= 120 && s_portRunning)
            {
                s_portExitReason = "START+SELECT held for 2 seconds";
                OSReport("[port] exit requested: %s\n", s_portExitReason);
                s_portRunning = false;
            }
        }
        else
        {
            s_exitComboFrames = 0;
        }
    }
#else
    // PORT: polled rather than waited for; the event below is only the fast path.
    PortFollowWindowShape();

    for (const AuroraEvent* ev = aurora_update(); ev && ev->type != AURORA_NONE; ++ev)
    {
        if (ev->type == AURORA_EXIT)
        {
            s_portExitReason = "window close";
            s_portRunning = false;
        }

        // PORT: the picture follows the window rather than the shape the window had at startup, so maximising it does not put black bars down the sides.
        if (ev->type == AURORA_WINDOW_RESIZED
            || ev->type == AURORA_DISPLAY_SCALE_CHANGED)
        {
            PortFollowWindowShape();
        }

        if (ev->type == AURORA_SDL_EVENT
            && ev->sdl.type == SDL_EVENT_KEY_DOWN
            && !ev->sdl.key.repeat
            && (ev->sdl.key.scancode == SDL_SCANCODE_F12
                || ev->sdl.key.scancode == SDL_SCANCODE_P))
        {
            PortRequestManualShot();
        }

        // PORT: every key goes to the overlay, which owns the hotkeys, F1 for the menu among them.
        if (ev->type == AURORA_SDL_EVENT
            && (ev->sdl.type == SDL_EVENT_KEY_DOWN || ev->sdl.type == SDL_EVENT_KEY_UP)
            && !ev->sdl.key.repeat)
        {
            PortOverlayHandleKey((int)ev->sdl.key.scancode,
                                 ev->sdl.type == SDL_EVENT_KEY_DOWN ? 1 : 0);
        }

        if (ev->type == AURORA_SDL_EVENT)
            PortPromptsEvent(&ev->sdl); // PORT: button prompts

        // PORT: the limiter follows the display the window is on, which can move or change mode.
        if (ev->type == AURORA_SDL_EVENT
            && (ev->sdl.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED
                || ev->sdl.type == SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED))
        {
            PortFollowDisplayRefresh();
        }
    }
#endif
}
#endif

int main(int argc, char* argv[])
{
#if defined(PORT_VITA)
#if !defined(STRIKERS_VITA_NO_LOGS)
    // Keep the Vita diagnostics on the memory card. stderr is deliberately
    // unbuffered so the last useful line survives a crash or forced exit.
    char vitaLogDir[64];
    (void)port_executable_dir(vitaLogDir, sizeof vitaLogDir);
    if (freopen("ux0:data/strikersVita/runtime.log", "w", stderr) != NULL)
    {
        setvbuf(stderr, NULL, _IONBF, 0);
        fprintf(stderr, "[vita] runtime log started\n");
    }
#endif
#endif

    // PORT: strikers.ini -> environment, before anything reads one.
    {
        const int applied = PortConfigLoad();
        if (applied > 0)
            OSReport("[port] %s: %d setting(s)\n",
                     PortConfigPath(), applied);
    }
    PortBenchInit();

#if defined(PORT_USE_AURORA)
#if defined(PORT_VITA)
    {
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
        // Keep the producer/game thread on core 0. With async GX enabled, core
        // 2 is the sole GX/GXM owner while core 1 is the helper core used by
        // Aurora's single CPU worker and the low-duty audio thread.
        (void)sceKernelChangeThreadCpuAffinityMask(
            sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);
        OSReport("[vita] clocks at boot: cpu=%d bus=%d gpu=%d xbar=%d MHz\n",
                 scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
                 scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
        const char* cpuMHz = getenv("STRIKERS_CPU_MHZ");
        if (cpuMHz != NULL && strcmp(cpuMHz, "444") == 0)
        {
            const int result = scePowerSetArmClockFrequency(444);
            OSReport("[vita] requested cpu=444 MHz rc=%d actual=%d\n", result, scePowerGetArmClockFrequency());
        }
        const char* gpuMHz = getenv("STRIKERS_GPU_MHZ");
        if (gpuMHz != NULL && strcmp(gpuMHz, "222") == 0)
        {
            const int gpuResult = scePowerSetGpuClockFrequency(222);
            const int busResult = scePowerSetBusClockFrequency(222);
            const int xbarResult = scePowerSetGpuXbarClockFrequency(166);
            OSReport("[vita] requested gpu=222 bus=222 xbar=166 rc=%d/%d/%d reported=%d/%d/%d\n",
                     gpuResult, busResult, xbarResult,
                     scePowerGetGpuClockFrequency(), scePowerGetBusClockFrequency(),
                     scePowerGetGpuXbarClockFrequency());
        }
        SceKernelFreeMemorySizeInfo memInfo = {};
        memInfo.size = sizeof(memInfo);
        if (sceKernelGetFreeMemorySize(&memInfo) >= 0)
        {
            OSReport("[vita] pre-Aurora free memory: user=%u cdram=%u phycont=%u\n",
                     (unsigned int)memInfo.size_user,
                     (unsigned int)memInfo.size_cdram,
                     (unsigned int)memInfo.size_phycont);
        }
        if (!glxVitaReserveResourceArena())
            OSReport("[vita] warning: could not pre-reserve the full GLX resource arena\n");
        if (sceKernelGetFreeMemorySize(&memInfo) >= 0)
        {
            OSReport("[vita] post-GLX-reserve free memory: user=%u cdram=%u phycont=%u\n",
                     (unsigned int)memInfo.size_user,
                     (unsigned int)memInfo.size_cdram,
                     (unsigned int)memInfo.size_phycont);
        }
        aurora::vita::BackendConfig cfg = {};
#if defined(STRIKERS_VITA_GC_NATIVE_RES)
        // Experimental performance mode: retain the Vita 960x544 scanout while
        // rasterizing the GameCube scene at its conventional 640x480 XFB size.
        // Aurora scales the final image to the display extent.
        cfg.render_width = 640;
        cfg.render_height = 480;
#endif
        cfg.vgl_legacy_pool_size = 0;
        // Do not use vglInitExtended's threshold mode here: with zero CDRAM
        // and PHYCONT thresholds it turns almost every currently-free page into
        // a vitaGL heap. Strikers already owns a long-lived GLX texture arena,
        // so give the renderer explicit bounded pools and preserve OS/newlib /
        // shader-compiler headroom.
        if (sceKernelGetFreeMemorySize(&memInfo) >= 0)
        {
            const u32 mb = 1024u * 1024u;
            // The circular pool and dynamic Aurora streaming buffers are CPU
            // mapped, so vitaGL allocates them from RAM before PHYCONT/CDRAM.
            // A 4 MiB RAM pool forced most streaming pages into scarce CDRAM,
            // directly competing with the gameplay texture cache.
            cfg.vgl_ram_pool_size = memInfo.size_user > 24u * mb
                ? ((memInfo.size_user - 24u * mb < 20u * mb) ? memInfo.size_user - 24u * mb : 20u * mb)
                : 0;
            cfg.vgl_cdram_pool_size = memInfo.size_cdram > 16u * mb
                ? ((memInfo.size_cdram - 16u * mb < 40u * mb) ? memInfo.size_cdram - 16u * mb : 40u * mb)
                : 0;
            const char* cdramPoolMb = getenv("STRIKERS_VGL_CDRAM_MB");
            if (cdramPoolMb != NULL)
            {
                const unsigned long value = strtoul(cdramPoolMb, NULL, 10);
                if (value >= 32 && value <= 96 && memInfo.size_cdram >= (value + 16u) * mb)
                    cfg.vgl_cdram_pool_size = (unsigned int)value * mb;
            }
            cfg.vgl_phycont_pool_size = memInfo.size_phycont > 8u * mb
                ? ((memInfo.size_phycont - 8u * mb < 8u * mb) ? memInfo.size_phycont - 8u * mb : 8u * mb)
                : 0;
            OSReport("[vita] vitaGL fixed pools: ram=%u KB cdram=%u KB phycont=%u KB\n",
                     cfg.vgl_ram_pool_size >> 10,
                     cfg.vgl_cdram_pool_size >> 10,
                     cfg.vgl_phycont_pool_size >> 10);
        }
        cfg.vgl_circular_pool_size = 12 * 1024 * 1024;
        cfg.vgl_display_buffer_count = 3;
        // Aurora's multi-buffered VBO/IBO pages are long-lived dynamic buffers.
        // Keep them out of vitaGL's circular scratch pool: that same pool stages
        // compressed texture transfers, and a large-frame rollover must not
        // consume the memory needed by the next CMPR/DXT1 upload.
        cfg.vgl_scratch_dynamic = false;
        cfg.vgl_scratch_stream = true;
        // The gameplay scene exceeds 16 MiB of resident GX textures in a
        // single frame. Aurora's cache cannot evict textures already referenced
        // by queued draws, so the smaller budget turns character/stadium
        // textures into fallbacks even though the vitaGL CDRAM pool still has
        // room. Match Aurora-Vita's normal 24 MiB cache budget.
        cfg.texture_cache_budget = 26 * 1024 * 1024;
        const char* textureCacheMb = getenv("STRIKERS_TEXTURE_CACHE_MB");
        if (textureCacheMb != NULL)
        {
            const unsigned long value = strtoul(textureCacheMb, NULL, 10);
            if (value >= 16 && value <= 64)
                cfg.texture_cache_budget = (unsigned int)value * 1024u * 1024u;
        }
        // The arena now rolls over safely inside a frame; use Aurora's normal
        // page size so stadium/crowd batches amortize buffer orphaning while
        // keeping peak transient storage bounded.
        cfg.stream_vertex_bytes = 8 * 1024 * 1024;
        cfg.stream_index_bytes = 512 * 1024;
        cfg.stream_slots = 3;
        // Stable three-core topology. Keep GX/GXM ownership on the proven
        // synchronous game thread for now: core 0 runs the game and renderer
        // control path, core 1 is reserved for audio, and one Aurora helper is
        // pinned to core 2 for parallel vertex/decode work. This preserves the
        // stable renderer lifetime while removing helper contention from CPU0.
        cfg.cpu_worker_threads = 1;
        // The worker scheduler treats this as the minimum useful work per lane.
        // Avoid waking the helper for tiny draws. Common ~200-vertex Strikers
        // packets still split across the GX owner + helper, while smaller UI
        // draws remain entirely on the render owner.
        cfg.cpu_parallel_min_vertices = 96;
        const char* workerCount = getenv("STRIKERS_AURORA_CPU_WORKERS");
        if (workerCount != NULL && workerCount[0] >= '0' && workerCount[0] <= '2' && workerCount[1] == '\0')
        {
            cfg.cpu_worker_threads = (unsigned int)(workerCount[0] - '0');
            if (cfg.cpu_worker_threads > 1)
                cfg.cpu_worker_threads = 1;
        }
        const char* parallelMin = getenv("STRIKERS_AURORA_PARALLEL_MIN");
        if (parallelMin != NULL)
        {
            const unsigned long value = strtoul(parallelMin, NULL, 10);
            if (value >= 64 && value <= 65536)
                cfg.cpu_parallel_min_vertices = (unsigned int)value;
        }
        cfg.wait_vblank = true;
#if defined(STRIKERS_VITA_FORCE_60HZ)
        // NEXTFRAME remains tear-free, but waiting in the display callback can
        // quantize a frame that barely misses vblank straight down to 30 Hz.
        // Pace the producer in VI instead; the callback itself stays nonblocking.
        cfg.wait_vblank = false;
        float displayHz = 60.0f;
        if (sceDisplayGetRefreshRate(&displayHz) < 0 || displayHz < 50.0f || displayHz > 70.0f)
            displayHz = 60.0f;
        PortSetDisplayRefresh((double)displayHz, 0);
        PortSetFrameLimit((double)displayHz);
#else
        const char* vitaVsync = getenv("STRIKERS_VITA_VSYNC");
        if (vitaVsync != NULL)
            cfg.wait_vblank = vitaVsync[0] != '0';
#endif
        OSReport("[vita] GXM callback vblank wait=%u (NEXTFRAME present always enabled)\n",
                 cfg.wait_vblank ? 1u : 0u);
        // Keep lightweight timing telemetry enabled in normal builds, but do
        // not pay for per-draw coverage/trace/geometry diagnostics unless a
        // developer explicitly requests them in strikers.ini/environment.
        const char* auroraDiagnostics = getenv("STRIKERS_AURORA_DIAGNOSTICS");
        const bool fullAuroraDiagnostics = auroraDiagnostics != NULL
            && auroraDiagnostics[0] != '\0' && auroraDiagnostics[0] != '0';
        cfg.diagnostics = fullAuroraDiagnostics;
        const char* splitVertexPhases = getenv("STRIKERS_PROFILE_VERTEX_PHASES");
        cfg.profile_split_vertex_phases = splitVertexPhases != NULL && splitVertexPhases[0] == '1';
        const char* textureDiagnostics = getenv("STRIKERS_VITA_TEXTURE_DIAGNOSTICS");
        cfg.texture_decode_diagnostics = textureDiagnostics != NULL && textureDiagnostics[0] == '1';
#if defined(AURORA_VITA_RENDERER_GXM)
        // Keep all persistent Aurora data under Strikers' own ux0:data folder.
        // Do not rely on TITLE_ID-derived paths: homebrew data roots are chosen
        // by the application and must survive rebuilds/title metadata changes.
        cfg.data_root_path = "ux0:data/strikersVita/aurora-vita";

        // D16 halves native depth storage/bandwidth versus the conservative
        // higher-precision path. Keep a runtime escape hatch for hardware A/B
        // checks on stages that expose precision-sensitive depth ranges.
        cfg.gxm_d16_depth = true;
        const char* gxmD16 = getenv("STRIKERS_GXM_D16");
        if (gxmD16 != NULL)
            cfg.gxm_d16_depth = gxmD16[0] != '0';
        // Native GXM can keep immutable object-space GX geometry resident and
        // perform fixed PN/texgen work in its vertex shader. The separated GX
        // bring-up must start from the CPU vertex path: the experimental static
        // geometry/fixed-vertex path is the current freeze suspect and Aurora's
        // own regression audit recommends budget=0 for the control profile.
#if defined(STRIKERS_VITA_GX_THREAD)
        cfg.static_geometry_budget = 0;
#else
        cfg.static_geometry_budget = 8 * 1024 * 1024;
#endif
        cfg.static_geometry_min_vertices = 16;
        const char* fixedMin = getenv("STRIKERS_GXM_FIXED_MIN");
        if (fixedMin != NULL)
        {
            const unsigned long value = strtoul(fixedMin, NULL, 10);
            if (value >= 3 && value <= 256)
                cfg.static_geometry_min_vertices = (unsigned int)value;
        }
        s_vitaShaderProfile = PortVitaReadShaderProfile();
        PortBenchSetLabel("shader", PortVitaShaderProfileName(s_vitaShaderProfile));
#if defined(STRIKERS_VITA_GX_THREAD)
        PortBenchSetLabel("gx_thread", "ON");
#else
        PortBenchSetLabel("gx_thread", "OFF");
#endif
        // Reuse the persistent GXP cache introduced by the upstream preload
        // path, while keeping CONTROL as a true no-preload baseline.
        cfg.gxm_preload_program_cache = s_vitaShaderProfile != VITA_SHADER_CONTROL;
        cfg.gxm_program_cache_preload_limit = 1024;
        // Stable Aurora performs this prewarm during renderer initialization.
        cfg.pipeline_prewarm_limit = s_vitaShaderProfile == VITA_SHADER_CONTROL ? 0 : 64;
        // Strikers seals manually at the loading -> gameplay boundary. Menus,
        // stage loading and training runs must retain the ability to discover
        // and persist new variants.
        cfg.gxm_seal_shader_cache_after_prewarm = false;
        // The native shader now reproduces GX channel lighting and COLOR0/COLOR1
        // texgen semantics. Keep an environment escape hatch for immediate A/B
        // validation against the graphics-proven CPU vertex path.
#if defined(STRIKERS_VITA_GX_THREAD)
        cfg.gxm_lit_fixed_vertex_gpu = false;
        cfg.gxm_dynamic_tex_matrix_gpu = false;
        cfg.gxm_bump_fixed_vertex_gpu = false;
        cfg.gxm_primitive_expand_gpu = false;
#else
        cfg.gxm_lit_fixed_vertex_gpu = true;
        // Extended persistent-GX path modelled after the successful Melee Vita
        // renderer: keep matrix selection, bump basis and stable point/line
        // expansion on GXM whenever Aurora can prove the draw is cache-safe.
        cfg.gxm_dynamic_tex_matrix_gpu = true;
        cfg.gxm_bump_fixed_vertex_gpu = true;
        cfg.gxm_primitive_expand_gpu = true;
#endif
        const char* litGpu = getenv("STRIKERS_GXM_LIT_GPU");
        if (litGpu != NULL)
            cfg.gxm_lit_fixed_vertex_gpu = litGpu[0] == '1';
        const char* texMtxGpu = getenv("STRIKERS_GXM_TEXMTX_GPU");
        if (texMtxGpu != NULL)
            cfg.gxm_dynamic_tex_matrix_gpu = texMtxGpu[0] == '1';
        const char* bumpGpu = getenv("STRIKERS_GXM_BUMP_GPU");
        if (bumpGpu != NULL)
            cfg.gxm_bump_fixed_vertex_gpu = bumpGpu[0] == '1';
        const char* primitiveGpu = getenv("STRIKERS_GXM_PRIM_GPU");
        if (primitiveGpu != NULL)
            cfg.gxm_primitive_expand_gpu = primitiveGpu[0] == '1';
#endif
        const char* staticGeometryMb = getenv("STRIKERS_STATIC_GEOMETRY_MB");
        if (staticGeometryMb != NULL)
        {
            const unsigned long value = strtoul(staticGeometryMb, NULL, 10);
            if (value <= 32)
                cfg.static_geometry_budget = (unsigned int)value * 1024u * 1024u;
        }
        const char* drawLimit = getenv("STRIKERS_VITA_DRAW_LIMIT");
        if (drawLimit != NULL)
            cfg.diagnostic_draw_limit = (unsigned int)strtoul(drawLimit, NULL, 10);
        OSReport("[vita] static geometry budget=%u KB gpu_fixed_vertex=%d lit_gpu=%d texmtx_gpu=%d bump_gpu=%d prim_gpu=%d split_vertex_phases=%d\n",
                 (unsigned int)(cfg.static_geometry_budget >> 10), cfg.static_geometry_budget != 0,
                 cfg.gxm_lit_fixed_vertex_gpu ? 1 : 0,
                 cfg.gxm_dynamic_tex_matrix_gpu ? 1 : 0,
                 cfg.gxm_bump_fixed_vertex_gpu ? 1 : 0,
                 cfg.gxm_primitive_expand_gpu ? 1 : 0,
                 cfg.profile_split_vertex_phases ? 1 : 0);
        if (cfg.diagnostic_draw_limit != 0)
            OSReport("[vita] diagnostic draw limit=%u\n", (unsigned int)cfg.diagnostic_draw_limit);
        cfg.strict_unsupported = false;
        cfg.diagnostics_period_frames = 10;
#if defined(STRIKERS_VITA_NO_LOGS)
        // Use Aurora Vita's native runtime logging switch rather than compiling
        // logging code out. This keeps renderer behavior identical while making
        // the performance build silent and prevents diagnostic files being opened.
        cfg.log_level = aurora::vita::RuntimeLogLevel::Silent;
        cfg.diagnostics = false;
        cfg.profile_split_vertex_phases = false;
        cfg.texture_decode_diagnostics = false;
        cfg.diagnostic_draw_limit = 0;
        cfg.telemetry_log_path = NULL;
        cfg.coverage_log_path = NULL;
        cfg.trace_log_path = NULL;
#else
        cfg.telemetry_log_path = "ux0:data/strikersVita/aurora_telemetry.log";
        cfg.coverage_log_path = fullAuroraDiagnostics ? "ux0:data/strikersVita/aurora_coverage.log" : NULL;
        cfg.trace_log_path = fullAuroraDiagnostics ? "ux0:data/strikersVita/aurora_trace.log" : NULL;
#endif
        const char* vita3dDiagnostics = getenv("STRIKERS_VITA_3D_DIAGNOSTICS");
        const bool verboseVita3d = vita3dDiagnostics != NULL
            && vita3dDiagnostics[0] != '\0' && vita3dDiagnostics[0] != '0';
        OSReport("[vita] 3D perf profile=cpu-color-v3 stream_v=%uKB stream_i=%uKB parallel_min=%u aurora_diag=%u vita3d_diag=%u\n",
                 (unsigned int)(cfg.stream_vertex_bytes >> 10),
                 (unsigned int)(cfg.stream_index_bytes >> 10),
                 (unsigned int)cfg.cpu_parallel_min_vertices,
                 fullAuroraDiagnostics ? 1u : 0u,
                 verboseVita3d ? 1u : 0u);
        if (!aurora::vita::initialize(cfg))
        {
            OSReport("[vita] Aurora backend init failed: %u %s\n",
                     (unsigned int)aurora::vita::last_init_failure(),
                     aurora::vita::last_init_failure_detail());
            return 1;
        }
        // Boot starts inside a loading window. TransitionTask closes it after
        // the first front-end state is fully materialized.
        aurora::vita::set_runtime_shader_compilation_enabled(true);
        PortVitaSampleRendererStats();
        PortSetWindowAspect(960, 544);
        PortSetDisplayRefresh(60.0, 1);
        VILockAspectRatio((int)(PortTargetAspect() * 10000.0f), 10000);
    }
#else
    {
        AuroraConfig cfg = {};
        cfg.appName = "Super Mario Strikers";
        // PORT: STRIKERS_BACKEND picks the graphics backend. Unset takes Aurora's own preference order.
        AuroraBackend wantBackend = BACKEND_AUTO;
#if defined(_WIN32)
        wantBackend = BACKEND_VULKAN;
        cfg.desiredBackend = wantBackend;
        int backendWasAsked = 0;
#else
        int backendWasAsked = 0;
#endif
        {
            const char* want = getenv("STRIKERS_BACKEND");
            if (want != NULL && *want != '\0')
            {
                backendWasAsked = 1;
                static const struct { const char* name; AuroraBackend id; } kBackends[] = {
                    { "auto", BACKEND_AUTO },         { "d3d11", BACKEND_D3D11 },
                    { "d3d12", BACKEND_D3D12 },       { "metal", BACKEND_METAL },
                    { "vulkan", BACKEND_VULKAN },     { "opengl", BACKEND_OPENGL },
                    { "opengles", BACKEND_OPENGLES }, { "webgpu", BACKEND_WEBGPU },
                    { "null", BACKEND_NULL },
                };
                const size_t nBackends = sizeof(kBackends) / sizeof(kBackends[0]);
                size_t bi = 0;
                for (; bi < nBackends; bi++)
                {
                    // Local, because this is two lines and pulling in a case-insensitive compare from NL for it is not worth the template instantiation.
                    const char* a = want;
                    const char* b = kBackends[bi].name;
                    while (*a != '\0' && *b != '\0')
                    {
                        char ca = *a;
                        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
                        if (ca != *b) break;
                        a++; b++;
                    }
                    if (*a == '\0' && *b == '\0')
                    {
                        wantBackend = kBackends[bi].id;
                        cfg.desiredBackend = wantBackend;
                        break;
                    }
                }
                if (bi == nBackends)
                {
                    OSReport("[port] STRIKERS_BACKEND=%s is not a backend name; "
                             "expected one of auto d3d11 d3d12 metal vulkan "
                             "opengl opengles webgpu null\n", want);
                    return 1;
                }
            }
        }
        // 1080p, 16:9: the logical framebuffer follows the display aspect, so a 16:9 window is filled rather than pillarboxed.
        cfg.windowWidth = 1920;
        cfg.windowHeight = 1080;
        cfg.vsync = PortEnvU32("STRIKERS_VSYNC", 0) != 0;

        // STRIKERS_MSAA sets the sample count and can only be chosen here. 1 by default: 4x costs an Intel N100 half its frame rate.
        cfg.msaa = (uint32_t)PortEnvU32("STRIKERS_MSAA", 1);
        // The port widened pointers rather than emulating the console memory map.
        cfg.mem1Size = 0;
        cfg.mem2Size = 0;
        // PORT: aurora_initialize reads startFullscreen, the window icon and the cache directory once, so they are chosen here.
        PortAuroraConfigure(&cfg);
        AuroraInfo info = aurora_initialize(argc, argv, &cfg);

        // STRIKERS_ASPECT=auto takes the shape of the window.
        PortSetWindowAspect(info.windowSize.width, info.windowSize.height);

        // PORT: cap to the display's refresh rate rather than to a constant.
        {
            const SDL_DisplayMode* mode =
                SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(info.window));
            // 0 for "unknown", refresh_rate is documented as 0.0f when the mode does not report one.
            // PORT: with vsync off, a surface that cannot skip the wait still waits for the display's refresh.
            PortSetDisplayRefresh(mode != NULL ? (double)mode->refresh_rate : 0.0,
                                  aurora_present_waits_for_vblank() ? 1 : 0);
            // PORT: kept for the event pump, which re-derives the rate when the display changes.
            s_portWindow = info.window;
        }

        // PORT: Aurora falls back to its preference order when the requested backend will not start.
        if (wantBackend != BACKEND_AUTO && info.backend != wantBackend)
        {
            if (backendWasAsked)
            {
                OSReport("[port] WARNING: asked for backend %d, got %d; Aurora "
                         "fell back. Any measurement from this run is of the "
                         "backend it got.\n",
                         (int)wantBackend, (int)info.backend);
            }
            else
            {
                OSReport("[port] Vulkan did not start; fell back to backend %d. "
                         "Expect roughly half the frame rate, but it runs.\n",
                         (int)info.backend);
            }
        }

        // Render above the console's framebuffer size.
        {
            float scale = (float)info.windowSize.height / 448.0f;
            if (scale > 3.0f)
                scale = 3.0f;
            if (scale < 1.0f)
                scale = 1.0f;
            aurora_set_frame_buffer_scale(PortEnvFloat("STRIKERS_RES_SCALE", scale));
        }

        // Hold the render target at the VI mode's aspect.
        VILockAspectRatio((int)(PortTargetAspect() * 10000.0f), 10000);
        AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);

        PortTexturesInit(info.userPath); // PORT: texture packs

        // Aurora's PAD reads SDL gamepads and reports PAD_ERR_NO_CONTROLLER when there is neither a gamepad nor a keyboard binding.
        PortInstallKeyboardBindings();

        PortDiscordInit();
    }
#endif
#else
    (void)argc;
    (void)argv;
#endif

    if (g_DoStackWatermarkTests)
    {
        OSClearStack(g_StackWatermarkFiller);
    }

    Initialize();
    PortProfilerStart();

    fopen("flushfile.txt", "r");

    nlTaskManager::SetNextState(GetConfigBool(Config::Global(), "skipfe", false) ? 2 : 4);

    if (GetConfigBool(Config::Global(), "enableFloatingPointExceptions", false))
    {
        InstallFloatingPointExceptionHandler();
    }

    if (GetConfigBool(Config::Global(), "callStackDumper", false))
    {
        InstallCallStackDumper();
    }

#if defined(PORT_USE_AURORA)
#if !defined(PORT_VITA)
    PortShaderStageBegin();   // PORT: logs the pipeline queue the boot memory card screen waits for
#endif

    while (s_portRunning && !PortQuitRequested())
    {
        // PORT: the deferred limiter sleep goes before the event pump so the frame reads input after it.
        PortLimiterFlush();
        PortBenchInputPumped();
        PortPumpAuroraEvents();
        PortDebugFrame();
#if defined(PORT_VITA)
        PortBenchFrameBegin();
        PortProfilerFrameMark();
        if (!aurora::vita::begin_frame())
            continue;              // minimised or surface lost; nothing to draw
#else
        // PORT: ReturnToFE leaves the pause flag set, so it is a pause only while a match exists.
        PortDiscordUpdate(FrontEnd::m_bInPauseMenuState && g_pGame != NULL ? 1 : 0);

        // PORT: timed, since the swapchain acquire blocks inside aurora_begin_frame under vsync.
        const unsigned long long acquireStart = port_monotonic_ns();
        if (!aurora_begin_frame())
            continue;              // minimised or surface lost; nothing to draw
        const unsigned long long acquireNs = port_monotonic_ns() - acquireStart;
        PortBenchFrameBegin();
        PortBenchAddAcquire(acquireNs);

        // PORT: SDL's pad state only changes in a pump and the acquire blocks under vsync, so pump again after it.
        {
            static int s_latePump = -1;
            if (s_latePump < 0)
            {
                const char* e = getenv("STRIKERS_LATE_PUMP");
                s_latePump = (e != NULL && *e == '0') ? 0 : 1;
            }
            if (s_latePump)
            {
                PortBenchInputPumped();
                SDL_PumpEvents();
            }
        }

        PortPromptsFrame(); // PORT: button prompts
#endif

        // Sample the pad before the tasks that read it. main() registers VBlankPadUpdate through PADSetSamplingCallback.
        PortInvokePadSamplingCallback();

        PortProfilerTasksBegin();
        nlTaskManager::RunAllTasks();
        PortProfilerTasksEnd();
        UpdateProfile();
        PortBenchAfterTasks();

        // PORT: the audio clock. MusyX runs only inside this call; see include/port/audio.h.
        PortAudioUpdate();
#if defined(PORT_VITA)
        aurora::vita::end_frame();
        VitaMaybeCaptureFrame();
#else
        aurora_end_frame();
#endif
        s_portFrame++;
#if defined(PORT_VITA)
        if ((s_portFrame % 60u) == 0u)
            PortVitaSampleRendererStats();
#endif
        PortBenchFrameEnd();
        if (PortBenchRunSeconds() > 0.0 && PortBenchElapsed() >= PortBenchRunSeconds())
        {
#if defined(PORT_VITA)
            PortVitaSampleRendererStats();
#endif
            PortBenchReport();
            s_portExitReason = "benchmark complete";
            break;
        }
    }
    if (s_portExitReason == NULL && PortQuitRequested())
        s_portExitReason = "PortQuitRequested";
    OSReport("[port] main loop ended at frame %lu: %s\n", s_portFrame,
             s_portExitReason != NULL ? s_portExitReason : "unknown reason");
#if defined(PORT_VITA)
    PortAudioStop();
    PortProfilerStop();
    aurora::vita::shutdown();
    sceKernelExitProcess(0);
#else
    PortDiscordShutdown();
    PortBenchReport();
    aurora_shutdown();
#endif
    return 0;
#else
    for (;;)
    {
        nlTaskManager::RunAllTasks();
        UpdateProfile();
    }
#endif
}
