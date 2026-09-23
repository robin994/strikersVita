// Aurora skips any draw whose pipeline is still compiling, so the boot memory card screen waits for the queue instead.

#ifndef PORT_SHADERS_H
#define PORT_SHADERS_H

#ifdef __cplusplus
extern "C" {
#endif

// Logs the queue; once, after aurora_initialize.
void PortShaderStageBegin(void);

// Non-zero while pipelines are still compiling, for at most two minutes from the first call.
int PortShaderStagePending(void);

// Compiled pipelines as a share of every one known, 0 to 100.
int PortShaderStagePercent(void);

// Report the wait, once, when the screen moves on.
void PortShaderStageEnd(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_SHADERS_H
