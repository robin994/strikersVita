#include "port/overlay.h"

#if defined(PORT_VITA)

#include "port/benchmark.h"
#include "Game/Sys/tweak.h"
#include "Game/SH/SHPause.h"
#include "NL/gl/glFont.h"
#include "NL/gl/glState.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <psp2/ctrl.h>

extern bool g_bRenderWorld;
extern bool g_bFrameStatsOnScreen;
extern bool g_bCoPlanarPerObject;
extern bool g_bDrawSafeFrame;
extern unsigned char g_bShadowBlobs;
extern unsigned char g_bClipToFrustum;
extern unsigned char g_bWhiteDiffuse;
extern unsigned char g_TexDetail;
extern unsigned char g_TexShadow;
extern unsigned char g_TexSelfIllum;
extern unsigned char g_TexGloss;
extern bool g_bShadowVolumes;
extern bool g_bEnableDrawableModel;
extern bool g_bDrawPlanarShadows;
extern bool g_bBallGlow;
extern bool g_bEnableDrawableSkinModel;
extern unsigned char g_hudVisible;
extern bool g_bAllowLighting;
extern bool g_bAllowSpecular;

extern "C" uint32_t aurora_vita_debug_runtime_flags(void) noexcept;
extern "C" uint32_t aurora_vita_debug_runtime_capabilities(void) noexcept;
extern "C" void aurora_vita_debug_set_runtime_flags(uint32_t flags) noexcept;
extern "C" int aurora_vita_debug_shader_runtime_compile(void) noexcept;
extern "C" void aurora_vita_debug_set_shader_runtime_compile(int enabled) noexcept;
extern "C" uint32_t aurora_vita_debug_build_flags(void) noexcept;

namespace {
char s_scene[64] = "-";
PortDebugMatch s_match{};
PortDebugSession s_session{};
PortDebugCommand s_commands[32]{};
unsigned s_head;
unsigned s_count;
int s_forcedStadium = -1;
int s_quit;

bool s_menuOpen;
bool s_perfHud;
bool s_nativeMenuRequest;
uint32_t s_prevButtons;
int s_page;
int s_selection[5]{};
bool s_stadiumVisible = true;
bool s_skyboxVisible = true;
bool s_characterShadowsVisible = true;

enum QuickPage {
    QP_RENDER = 0,
    QP_SHADOWS,
    QP_SHADERS,
    QP_EXPERIMENTS,
    QP_METRICS,
    QP_COUNT,
};

enum AuroraRuntimeBits : uint32_t {
    AR_STATIC_GEOMETRY = 1u << 0,
    AR_STREAMED_VERTEX = 1u << 1,
    AR_LIT_VERTEX = 1u << 2,
    AR_DYNAMIC_TEX_MTX = 1u << 3,
    AR_BUMP_VERTEX = 1u << 4,
    AR_PRIMITIVE_EXPAND = 1u << 5,
    AR_STATIC_STABLE_ONLY = 1u << 6,
};

struct QuickDefaults {
    bool captured;
    bool renderWorld;
    bool frameStats;
    bool coPlanar;
    bool drawSafeFrame;
    unsigned char shadowBlobs;
    unsigned char clipFrustum;
    unsigned char whiteDiffuse;
    unsigned char texDetail, texShadow, texSelfIllum, texGloss;
    bool shadowVolumes;
    bool drawableModel;
    bool planarShadows;
    bool ballGlow;
    bool drawableSkin;
    unsigned char hud;
    bool lighting;
    bool specular;
    uint32_t auroraFlags;
    int shaderRuntimeCompile;
} s_defaults;

const char* const kPageNames[QP_COUNT] = {
    "RENDER", "SHADOWS", "SHADERS", "GXM EXPERIMENTS", "METRICS"
};

const nlColour kPanel = { 0x00, 0x00, 0x00, 0xC8 };
const nlColour kGreen = { 0x20, 0xFF, 0x40, 0xFF };
const nlColour kYellow = { 0xFF, 0xF0, 0x20, 0xFF };
const nlColour kWhite = { 0xF0, 0xF0, 0xF0, 0xFF };
const nlColour kGrey = { 0xA0, 0xA0, 0xA0, 0xFF };

const char* onoff(bool value) { return value ? "ON" : "OFF"; }

void push_class_flag(int which, bool off)
{
    PortDebugCommand command{};
    command.op = PDBG_SET_CLASS_FLAG;
    command.a = which;
    command.b = off ? 1 : 0;
    PortDebugPushCommand(&command);
}

void capture_defaults()
{
    if (s_defaults.captured)
        return;
    s_defaults.captured = true;
    s_defaults.renderWorld = g_bRenderWorld;
    s_defaults.frameStats = g_bFrameStatsOnScreen;
    s_defaults.coPlanar = g_bCoPlanarPerObject;
    s_defaults.drawSafeFrame = g_bDrawSafeFrame;
    s_defaults.shadowBlobs = g_bShadowBlobs;
    s_defaults.clipFrustum = g_bClipToFrustum;
    s_defaults.whiteDiffuse = g_bWhiteDiffuse;
    s_defaults.texDetail = g_TexDetail;
    s_defaults.texShadow = g_TexShadow;
    s_defaults.texSelfIllum = g_TexSelfIllum;
    s_defaults.texGloss = g_TexGloss;
    s_defaults.shadowVolumes = g_bShadowVolumes;
    s_defaults.drawableModel = g_bEnableDrawableModel;
    s_defaults.planarShadows = g_bDrawPlanarShadows;
    s_defaults.ballGlow = g_bBallGlow;
    s_defaults.drawableSkin = g_bEnableDrawableSkinModel;
    s_defaults.hud = g_hudVisible;
    s_defaults.lighting = g_bAllowLighting;
    s_defaults.specular = g_bAllowSpecular;
    s_defaults.auroraFlags = aurora_vita_debug_runtime_flags();
    s_defaults.shaderRuntimeCompile = aurora_vita_debug_shader_runtime_compile();
}

void restore_defaults()
{
    if (!s_defaults.captured)
        return;
    g_bRenderWorld = s_defaults.renderWorld;
    g_bFrameStatsOnScreen = s_defaults.frameStats;
    g_bCoPlanarPerObject = s_defaults.coPlanar;
    g_bDrawSafeFrame = s_defaults.drawSafeFrame;
    g_bShadowBlobs = s_defaults.shadowBlobs;
    g_bClipToFrustum = s_defaults.clipFrustum;
    g_bWhiteDiffuse = s_defaults.whiteDiffuse;
    g_TexDetail = s_defaults.texDetail;
    g_TexShadow = s_defaults.texShadow;
    g_TexSelfIllum = s_defaults.texSelfIllum;
    g_TexGloss = s_defaults.texGloss;
    g_bShadowVolumes = s_defaults.shadowVolumes;
    g_bEnableDrawableModel = s_defaults.drawableModel;
    g_bDrawPlanarShadows = s_defaults.planarShadows;
    g_bBallGlow = s_defaults.ballGlow;
    g_bEnableDrawableSkinModel = s_defaults.drawableSkin;
    g_hudVisible = s_defaults.hud;
    g_bAllowLighting = s_defaults.lighting;
    g_bAllowSpecular = s_defaults.specular;
    aurora_vita_debug_set_runtime_flags(s_defaults.auroraFlags);
    aurora_vita_debug_set_shader_runtime_compile(s_defaults.shaderRuntimeCompile);
    s_stadiumVisible = true;
    s_skyboxVisible = true;
    s_characterShadowsVisible = true;
    push_class_flag(PDBG_CF_STADIUM_OFF, false);
    push_class_flag(PDBG_CF_SKYBOX_OFF, false);
    push_class_flag(PDBG_CF_CHAR_SHADOWS_OFF, false);
}

int page_item_count(int page)
{
    switch (page)
    {
    case QP_RENDER: return 10;
    case QP_SHADOWS: return 7;
    case QP_SHADERS: return 8;
    case QP_EXPERIMENTS: return 9;
    case QP_METRICS: return 2;
    default: return 0;
    }
}

void toggle_aurora(uint32_t bit)
{
    aurora_vita_debug_set_runtime_flags(aurora_vita_debug_runtime_flags() ^ bit);
}

void activate_item(int page, int item)
{
    capture_defaults();
    switch (page)
    {
    case QP_RENDER:
        switch (item)
        {
        case 0: g_bRenderWorld = !g_bRenderWorld; break;
        case 1: g_hudVisible = g_hudVisible ? 0 : 1; break;
        case 2: g_bEnableDrawableModel = !g_bEnableDrawableModel; break;
        case 3: g_bEnableDrawableSkinModel = !g_bEnableDrawableSkinModel; break;
        case 4:
            s_stadiumVisible = !s_stadiumVisible;
            push_class_flag(PDBG_CF_STADIUM_OFF, !s_stadiumVisible);
            break;
        case 5:
            s_skyboxVisible = !s_skyboxVisible;
            push_class_flag(PDBG_CF_SKYBOX_OFF, !s_skyboxVisible);
            break;
        case 6:
            s_characterShadowsVisible = !s_characterShadowsVisible;
            push_class_flag(PDBG_CF_CHAR_SHADOWS_OFF, !s_characterShadowsVisible);
            break;
        case 7: g_bBallGlow = !g_bBallGlow; break;
        case 8: g_bClipToFrustum = g_bClipToFrustum ? 0 : 1; break;
        case 9: g_bFrameStatsOnScreen = !g_bFrameStatsOnScreen; break;
        }
        break;
    case QP_SHADOWS:
        switch (item)
        {
        case 0: g_bShadowBlobs = g_bShadowBlobs ? 0 : 1; break;
        case 1: g_bDrawPlanarShadows = !g_bDrawPlanarShadows; break;
        case 2: g_bShadowVolumes = !g_bShadowVolumes; break;
        case 3: g_bCoPlanarPerObject = !g_bCoPlanarPerObject; break;
        case 4: g_TexShadow = g_TexShadow ? 0 : 1; break;
        case 5: g_bBallGlow = !g_bBallGlow; break;
        case 6:
            s_characterShadowsVisible = !s_characterShadowsVisible;
            push_class_flag(PDBG_CF_CHAR_SHADOWS_OFF, !s_characterShadowsVisible);
            break;
        }
        break;
    case QP_SHADERS:
        switch (item)
        {
        case 0: g_bAllowLighting = !g_bAllowLighting; break;
        case 1: g_bAllowSpecular = !g_bAllowSpecular; break;
        case 2: g_TexDetail = g_TexDetail ? 0 : 1; break;
        case 3: g_TexShadow = g_TexShadow ? 0 : 1; break;
        case 4: g_TexSelfIllum = g_TexSelfIllum ? 0 : 1; break;
        case 5: g_TexGloss = g_TexGloss ? 0 : 1; break;
        case 6: g_bWhiteDiffuse = g_bWhiteDiffuse ? 0 : 1; break;
        case 7:
            aurora_vita_debug_set_shader_runtime_compile(!aurora_vita_debug_shader_runtime_compile());
            break;
        }
        break;
    case QP_EXPERIMENTS:
        switch (item)
        {
        case 0: toggle_aurora(AR_STATIC_GEOMETRY); break;
        case 1: toggle_aurora(AR_STREAMED_VERTEX); break;
        case 2: toggle_aurora(AR_LIT_VERTEX); break;
        case 3: toggle_aurora(AR_DYNAMIC_TEX_MTX); break;
        case 4: toggle_aurora(AR_BUMP_VERTEX); break;
        case 5: toggle_aurora(AR_PRIMITIVE_EXPAND); break;
        case 6: toggle_aurora(AR_STATIC_STABLE_ONLY); break;
        case 7:
        {
            const uint32_t caps = aurora_vita_debug_runtime_capabilities();
            const uint32_t flags = aurora_vita_debug_runtime_flags();
            aurora_vita_debug_set_runtime_flags((flags & caps) == caps ? 0 : caps);
            break;
        }
        case 8: restore_defaults(); break;
        }
        break;
    case QP_METRICS:
        if (item == 0)
            s_perfHud = !s_perfHud;
        else if (item == 1)
            restore_defaults();
        break;
    }
}

void handle_buttons(uint32_t buttons)
{
    const uint32_t combo = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT;
    const bool comboNow = (buttons & combo) == combo;
    const bool comboBefore = (s_prevButtons & combo) == combo;
    if (comboNow && !comboBefore)
    {
        s_nativeMenuRequest = true;
    }

    s_prevButtons = buttons;
}

void draw_line(int y, bool selected, const char* label, const char* value)
{
    glFontPrintf(GLV_Debug, 1, y, selected ? kYellow : kWhite,
                 "%c %-27s %s", selected ? '>' : ' ', label,
                 value != nullptr ? value : "");
}

void draw_perf_box()
{
    PortBenchLive live{};
    PortBenchGetLive(&live);
    PortBenchRendererStats rs{};
    PortBenchGetRendererStats(&rs);

    DrawTextRectangle(GLV_Debug, 50.0f, 0.0f, 79.0f, 7.0f, 0.0f, kPanel, true);
    glFontBegin(false);
    glFontPrintf(GLV_Debug, 51, 0, kGreen, "RENDER %5.1f FPS", live.fps);
    glFontPrintf(GLV_Debug, 51, 1, kWhite, "FRAME %6.1f ms", live.frameMs);
    glFontPrintf(GLV_Debug, 51, 2, kWhite, "BUSY  %6.1f ms", live.busyMs);
    if (rs.valid)
    {
        glFontPrintf(GLV_Debug, 51, 3, kWhite, "XFORM %6.1f ms", rs.vertexTransformUs / 1000.0);
        glFontPrintf(GLV_Debug, 51, 4, kWhite, "DECODE%6.1f ms", rs.vertexDecodeUs / 1000.0);
        glFontPrintf(GLV_Debug, 51, 5, kWhite, "SUBMIT%6.1f ms", rs.submitUs / 1000.0);
        glFontPrintf(GLV_Debug, 51, 6, kWhite, "DRAWS %6llu", rs.draws);
    }
    glFontEnd();
}

void draw_menu()
{
    char value[48];
    const uint32_t runtime = aurora_vita_debug_runtime_flags();
    const uint32_t caps = aurora_vita_debug_runtime_capabilities();
    const uint32_t build = aurora_vita_debug_build_flags();
    const int selected = s_selection[s_page];

    DrawTextRectangle(GLV_Debug, 0.0f, 0.0f, 47.0f, 20.0f, 0.0f, kPanel, true);
    glFontBegin(false);
    glFontPrintf(GLV_Debug, 1, 0, kGreen, "OPTIONS  [%s]", kPageNames[s_page]);
    glFontPrintf(GLV_Debug, 1, 1, kGrey, "L/R PAGE  X TOGGLE  O CLOSE  TRIANGLE RESET");

    int y = 3;
    switch (s_page)
    {
    case QP_RENDER:
        draw_line(y++, selected == 0, "WORLD", onoff(g_bRenderWorld));
        draw_line(y++, selected == 1, "HUD", onoff(g_hudVisible != 0));
        draw_line(y++, selected == 2, "STATIC MODELS", onoff(g_bEnableDrawableModel));
        draw_line(y++, selected == 3, "SKINNED MODELS", onoff(g_bEnableDrawableSkinModel));
        draw_line(y++, selected == 4, "STADIUM", onoff(s_stadiumVisible));
        draw_line(y++, selected == 5, "SKYBOX", onoff(s_skyboxVisible));
        draw_line(y++, selected == 6, "CHARACTER SHADOWS", onoff(s_characterShadowsVisible));
        draw_line(y++, selected == 7, "BALL GLOW", onoff(g_bBallGlow));
        draw_line(y++, selected == 8, "FRUSTUM CULLING", onoff(g_bClipToFrustum != 0));
        draw_line(y++, selected == 9, "GAME FRAME STATS", onoff(g_bFrameStatsOnScreen));
        break;
    case QP_SHADOWS:
        draw_line(y++, selected == 0, "BLOB SHADOWS", onoff(g_bShadowBlobs != 0));
        draw_line(y++, selected == 1, "PLANAR SHADOWS", onoff(g_bDrawPlanarShadows));
        draw_line(y++, selected == 2, "SHADOW VOLUMES", onoff(g_bShadowVolumes));
        draw_line(y++, selected == 3, "CO-PLANAR PER OBJECT", onoff(g_bCoPlanarPerObject));
        draw_line(y++, selected == 4, "SHADOW TEXTURE STAGE", onoff(g_TexShadow != 0));
        draw_line(y++, selected == 5, "BALL GLOW", onoff(g_bBallGlow));
        draw_line(y++, selected == 6, "CHARACTER SHADOWS", onoff(s_characterShadowsVisible));
        break;
    case QP_SHADERS:
        draw_line(y++, selected == 0, "LIGHTING", onoff(g_bAllowLighting));
        draw_line(y++, selected == 1, "SPECULAR", onoff(g_bAllowSpecular));
        draw_line(y++, selected == 2, "DETAIL STAGE", onoff(g_TexDetail != 0));
        draw_line(y++, selected == 3, "SHADOW STAGE", onoff(g_TexShadow != 0));
        draw_line(y++, selected == 4, "SELF ILLUMINATION", onoff(g_TexSelfIllum != 0));
        draw_line(y++, selected == 5, "GLOSS STAGE", onoff(g_TexGloss != 0));
        draw_line(y++, selected == 6, "WHITE DIFFUSE", onoff(g_bWhiteDiffuse != 0));
        draw_line(y++, selected == 7, "RUNTIME SHADER COMPILE",
                  onoff(aurora_vita_debug_shader_runtime_compile() != 0));
        break;
    case QP_EXPERIMENTS:
        draw_line(y++, selected == 0, "STATIC GEOMETRY GPU", onoff((runtime & AR_STATIC_GEOMETRY) != 0));
        draw_line(y++, selected == 1, "STREAMED GPU VERTEX", onoff((runtime & AR_STREAMED_VERTEX) != 0));
        draw_line(y++, selected == 2, "LIT FIXED VERTEX", onoff((runtime & AR_LIT_VERTEX) != 0));
        draw_line(y++, selected == 3, "DYNAMIC TEX MATRIX", onoff((runtime & AR_DYNAMIC_TEX_MTX) != 0));
        draw_line(y++, selected == 4, "BUMP FIXED VERTEX", onoff((runtime & AR_BUMP_VERTEX) != 0));
        draw_line(y++, selected == 5, "PRIMITIVE EXPANSION", onoff((runtime & AR_PRIMITIVE_EXPAND) != 0));
        draw_line(y++, selected == 6, "STATIC STABLE ONLY", onoff((runtime & AR_STATIC_STABLE_ONLY) != 0));
        std::snprintf(value, sizeof value, "%s", ((runtime & caps) == caps) ? "ALL ON" : "MIXED/OFF");
        draw_line(y++, selected == 7, "MASTER EXPERIMENTS", value);
        draw_line(y++, selected == 8, "RESTORE BOOT DEFAULTS", "ACTION");
        glFontPrintf(GLV_Debug, 1, y + 1, kGrey, "GXM %s  DIRECT STREAM %s  DIRECT DRAW %s",
                     (build & 1u) ? "ON" : "OFF", (build & 2u) ? "ON" : "OFF", (build & 4u) ? "ON" : "OFF");
        break;
    case QP_METRICS:
    {
        PortBenchLive live{};
        PortBenchGetLive(&live);
        PortBenchRendererStats rs{};
        PortBenchGetRendererStats(&rs);
        draw_line(y++, selected == 0, "PERFORMANCE HUD", onoff(s_perfHud));
        draw_line(y++, selected == 1, "RESTORE BOOT DEFAULTS", "ACTION");
        glFontPrintf(GLV_Debug, 1, y++, kWhite, "FPS %.1f  FRAME %.2fms  BUSY %.2fms  P95 %.2fms",
                     live.fps, live.frameMs, live.busyMs, live.busyP95Ms);
        if (rs.valid)
        {
            glFontPrintf(GLV_Debug, 1, y++, kWhite, "FRONT %.2f  XFORM %.2f  DECODE %.2f ms",
                         rs.drawFrontendUs / 1000.0, rs.vertexTransformUs / 1000.0, rs.vertexDecodeUs / 1000.0);
            glFontPrintf(GLV_Debug, 1, y++, kWhite, "TEX %.2f  PIPE %.2f  SUBMIT %.2f ms",
                         rs.textureResolveUs / 1000.0, rs.pipelineResolveUs / 1000.0, rs.submitUs / 1000.0);
            glFontPrintf(GLV_Debug, 1, y++, kWhite, "DRAWS %llu  VERT %llu  TRI %llu",
                         rs.draws, rs.vertices, rs.triangles);
            glFontPrintf(GLV_Debug, 1, y++, kWhite, "STATIC HIT %llu MISS %llu  SCENES %u",
                         rs.staticGeometryHits, rs.staticGeometryMisses, rs.nativeSceneCount);
            glFontPrintf(GLV_Debug, 1, y++, rs.gpuBackpressureLikely ? kYellow : kWhite,
                         "GPU BACKPRESSURE %s  DQ BLOCK %u%%",
                         rs.gpuBackpressureLikely ? "YES" : "NO", rs.displayQueueBlockedPercent);
        }
        break;
    }
    }
    glFontEnd();
}
}

extern "C" void PortOverlayInit(void) { capture_defaults(); }
extern "C" int PortOverlayEnabled(void) { return 0; }
extern "C" void PortOverlayDraw(void)
{
    if (!s_nativeMenuRequest)
        return;
    s_nativeMenuRequest = false;
    PauseMenuScene::OpenDebugMenu();
}

extern "C" void PortOverlaySetScene(const char* name)
{
    if (name == nullptr)
        name = "-";
    std::strncpy(s_scene, name, sizeof(s_scene) - 1);
    s_scene[sizeof(s_scene) - 1] = '\0';
}

extern "C" void PortOverlaySetStadium(const char* name) { (void)name; }
extern "C" void PortOverlaySetMatch(float clock, int scoreHome, int scoreAway)
{
    (void)clock;
    (void)scoreHome;
    (void)scoreAway;
}
extern "C" const char* PortOverlaySceneName(void) { return s_scene; }
extern "C" void PortOverlayToggleMenu(void)
{
    s_nativeMenuRequest = true;
}
extern "C" int PortOverlayMenuOpen(void) { return 0; }
extern "C" void PortOverlayVitaInput(unsigned int buttons) { handle_buttons(buttons); }
extern "C" void PortOverlayHandleKey(int scancode, int down) { (void)scancode; (void)down; }
extern "C" int PortQuitRequested(void) { return s_quit; }
extern "C" void PortRequestQuit(void) { s_quit = 1; }
extern "C" int PortDebugForcedStadium(void) { return s_forcedStadium; }
extern "C" void PortDebugSetForcedStadium(int index) { s_forcedStadium = index; }
extern "C" void PortDebugSetPad(int port, int err, unsigned int buttons, int stickX, int stickY,
                                  int substickX, int substickY, int triggerL, int triggerR)
{
    (void)port; (void)err; (void)buttons; (void)stickX; (void)stickY;
    (void)substickX; (void)substickY; (void)triggerL; (void)triggerR;
}

extern "C" void PortDebugSetMatch(const PortDebugMatch* match)
{
    if (match != nullptr)
        s_match = *match;
}
extern "C" const PortDebugMatch* PortDebugGetMatch(void) { return &s_match; }
extern "C" void PortDebugSetSession(const PortDebugSession* session)
{
    if (session != nullptr)
        s_session = *session;
}
extern "C" const PortDebugSession* PortDebugGetSession(void) { return &s_session; }

extern "C" int PortDebugPushCommand(const PortDebugCommand* command)
{
    if (command == nullptr || s_count == 32)
        return 0;
    s_commands[(s_head + s_count) & 31u] = *command;
    ++s_count;
    return 1;
}

extern "C" int PortDebugPopCommand(PortDebugCommand* out)
{
    if (out == nullptr || s_count == 0)
        return 0;
    *out = s_commands[s_head];
    s_head = (s_head + 1) & 31u;
    --s_count;
    return 1;
}

#endif
