// The game's rumble presets as HD rumble effects.

#ifndef PORT_SWITCH_RUMBLE_H
#define PORT_SWITCH_RUMBLE_H

#ifdef __cplusplus
extern "C" {
#endif

// `preset` is an eRumbleActionPreset. False leaves the rumble to PADControlMotor.
int PortSwitchRumblePlay(unsigned int pad, int preset);

void PortSwitchRumbleStop(unsigned int pad);

#ifdef __cplusplus
}
#endif

#endif   // PORT_SWITCH_RUMBLE_H
