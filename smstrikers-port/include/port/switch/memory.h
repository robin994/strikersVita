// Process heap figures on Horizon.

#ifndef PORT_SWITCH_MEMORY_H
#define PORT_SWITCH_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

void PortSwitchHeapInfo(unsigned long long* inUse, unsigned long long* available);

#ifdef __cplusplus
}
#endif

#endif   // PORT_SWITCH_MEMORY_H
