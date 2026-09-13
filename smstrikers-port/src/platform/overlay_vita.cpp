#include "port/overlay.h"

#if defined(PORT_VITA)

#include <cstring>

namespace {
char s_scene[64] = "-";
PortDebugMatch s_match{};
PortDebugSession s_session{};
PortDebugCommand s_commands[32]{};
unsigned s_head;
unsigned s_count;
int s_forcedStadium = -1;
int s_quit;
}

extern "C" void PortOverlayInit(void) {}
extern "C" int PortOverlayEnabled(void) { return 0; }
extern "C" void PortOverlayDraw(void) {}

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
extern "C" void PortOverlayToggleMenu(void) {}
extern "C" int PortOverlayMenuOpen(void) { return 0; }
extern "C" void PortOverlayHandleKey(int scancode, int down) { (void)scancode; (void)down; }
extern "C" int PortQuitRequested(void) { return s_quit; }
extern "C" void PortRequestQuit(void) { s_quit = 1; }
extern "C" int PortDebugStateWanted(void) { return 0; }
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
