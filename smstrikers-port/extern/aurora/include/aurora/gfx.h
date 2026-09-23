#ifndef AURORA_GFX_H
#define AURORA_GFX_H

#ifdef __cplusplus
#include <cstdint>

extern "C" {
#else
#include "stdint.h"
#endif

#if !defined(NDEBUG) && !defined(AURORA_GFX_DEBUG_GROUPS)
#define AURORA_GFX_DEBUG_GROUPS
#endif

void push_debug_group(const char* label);
void pop_debug_group();

typedef struct {
  uint32_t queuedPipelines;
  uint32_t createdPipelines;
  uint32_t drawCallCount;
  uint32_t mergedDrawCallCount;
  uint32_t lastVertSize;
  uint32_t lastUniformSize;
  uint32_t lastIndexSize;
  uint32_t lastStorageSize;
  uint32_t lastTextureUploadSize;
} AuroraStats;

const AuroraStats* aurora_get_stats();
/* smstrikers-port: the pipeline counters from AuroraStats, read atomically; the struct's own fields race the compile threads. */
void aurora_get_pipeline_counts(uint32_t* queued, uint32_t* created);
float aurora_get_fps();

void aurora_enable_vsync(bool enabled);
/* smstrikers-port: whether the selected present mode waits for the vblank; vsync off still gets Fifo on a surface without Mailbox or Immediate. */
bool aurora_present_waits_for_vblank();

#ifdef __cplusplus
}
#endif

#endif
