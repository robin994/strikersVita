// Native pad for Vita; neutral placeholder on other non-Aurora hosts.

#include <string.h>

#include "dolphin/pad.h"

#if defined(STRIKERS_VITA)
#include <psp2/ctrl.h>
#endif

static PADSamplingCallback s_sampling_cb;

BOOL PADInit(void)
{
#if defined(STRIKERS_VITA)
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
#endif
    return TRUE;
}

int PADReset(u32 mask)
{
    (void)mask;
    return 1;
}

u32 PADRead(PADStatus* status)
{
    if (status == NULL)
        return 0;
    memset(status, 0, sizeof(PADStatus) * PAD_MAX_CONTROLLERS);

    status[0].err = PAD_ERR_NONE;
    for (int i = 1; i < PAD_MAX_CONTROLLERS; i++)
        status[i].err = PAD_ERR_NO_CONTROLLER;

#if defined(STRIKERS_VITA)
    SceCtrlData pad;
    memset(&pad, 0, sizeof pad);
    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0)
    {
        u16 b = 0;
        if (pad.buttons & SCE_CTRL_LEFT) b |= PAD_BUTTON_LEFT;
        if (pad.buttons & SCE_CTRL_RIGHT) b |= PAD_BUTTON_RIGHT;
        if (pad.buttons & SCE_CTRL_DOWN) b |= PAD_BUTTON_DOWN;
        if (pad.buttons & SCE_CTRL_UP) b |= PAD_BUTTON_UP;
        if (pad.buttons & SCE_CTRL_CROSS) b |= PAD_BUTTON_A;
        if (pad.buttons & SCE_CTRL_CIRCLE) b |= PAD_BUTTON_B;
        if (pad.buttons & SCE_CTRL_SQUARE) b |= PAD_BUTTON_X;
        if (pad.buttons & SCE_CTRL_TRIANGLE) b |= PAD_BUTTON_Y;
        if (pad.buttons & SCE_CTRL_LTRIGGER) b |= PAD_TRIGGER_L;
        if (pad.buttons & SCE_CTRL_RTRIGGER) b |= PAD_TRIGGER_R;
        if (pad.buttons & SCE_CTRL_SELECT) b |= PAD_TRIGGER_Z;
        if (pad.buttons & SCE_CTRL_START) b |= PAD_BUTTON_START;

        status[0].button = b;
        status[0].stickX = (s8)((int)pad.lx - 128);
        status[0].stickY = (s8)(127 - (int)pad.ly);
        status[0].substickX = (s8)((int)pad.rx - 128);
        status[0].substickY = (s8)(127 - (int)pad.ry);
        status[0].triggerLeft = (pad.buttons & SCE_CTRL_LTRIGGER) ? 255 : 0;
        status[0].triggerRight = (pad.buttons & SCE_CTRL_RTRIGGER) ? 255 : 0;
        status[0].analogA = (pad.buttons & SCE_CTRL_CROSS) ? 255 : 0;
        status[0].analogB = (pad.buttons & SCE_CTRL_CIRCLE) ? 255 : 0;
    }
#endif

    // Bitmask of ports whose read failed.
    return 0;
}

void PADClampCircle(PADStatus* status)
{
    // The Vita analog range already fits the signed GameCube range.
    (void)status;
}

void PADControlMotor(u32 chan, u32 command)
{
    (void)chan;
    (void)command;
}

PADSamplingCallback PADSetSamplingCallback(PADSamplingCallback callback)
{
    PADSamplingCallback prev = s_sampling_cb;
    s_sampling_cb = callback;
    return prev;
}

void PortInvokePadSamplingCallback(void)
{
    if (s_sampling_cb)
        s_sampling_cb();
}
