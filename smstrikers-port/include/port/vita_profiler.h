#ifndef PORT_VITA_PROFILER_H
#define PORT_VITA_PROFILER_H

#ifdef __cplusplus
extern "C" {
#endif

// Runtime opt-in wrapper around VitaDebugger's allocation-free user-mode profiler.
// STRIKERS_VITA_PROFILE=1 enables capture when the profiler build option is present.
int PortProfilerStart(void);
void PortProfilerStop(void);

void PortProfilerFrameMark(void);
void PortProfilerTasksBegin(void);
void PortProfilerTasksEnd(void);

// Per-nlTask profiling. Tasks present when PortProfilerStart() runs are
// registered in the trace dictionary; later tasks fall back to a generic zone.
void PortProfilerTaskRunBegin(const void* task);
void PortProfilerTaskRunEnd(void);
void PortProfilerTaskTransitionBegin(const void* task);
void PortProfilerTaskTransitionEnd(void);

// EndFrame/glplatSendFrame sub-phases. `phase` is one of:
// 0 swap_pre, 1 send_frame, 2 send_views, 3 swap_post, 4 frame_alloc.
void PortProfilerRenderPhaseBegin(unsigned int phase);
void PortProfilerRenderPhaseEnd(void);
void PortProfilerRenderViewBegin(unsigned int view);
void PortProfilerRenderViewEnd(void);

void PortProfilerAudioThreadStarted(unsigned int threadId);
void PortProfilerGxThreadStarted(unsigned int threadId);
void PortProfilerGxPublishBegin(void);
void PortProfilerGxPublishEnd(void);
void PortProfilerGxConsumeBegin(void);
void PortProfilerGxConsumeEnd(void);
void PortProfilerAudioFillBegin(void);
void PortProfilerAudioFillEnd(void);
void PortProfilerAudioTickBegin(void);
void PortProfilerAudioTickEnd(void);
void PortProfilerAudioQueue(int queuedBytes, int producedBuffers);

#ifdef __cplusplus
}
#endif

#endif // PORT_VITA_PROFILER_H
