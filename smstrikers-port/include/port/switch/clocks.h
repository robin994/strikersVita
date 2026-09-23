// CPU boost on Horizon.

#ifndef PORT_SWITCH_CLOCKS_H
#define PORT_SWITCH_CLOCKS_H

#ifdef __cplusplus
extern "C" {
#endif

void PortSwitchCpuBoost(int on);
void PortSwitchLoadingIndicator(int on);

#ifdef __cplusplus
}
#endif

#endif   // PORT_SWITCH_CLOCKS_H
