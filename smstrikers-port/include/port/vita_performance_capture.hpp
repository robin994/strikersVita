#pragma once
#include "aurora_vita_backend.hpp"
#include "port/config.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace port {
inline void capture_completed_performance(bool livePlay) {
  static const char* path=std::getenv("STRIKERS_PERFORMANCE_CAPTURE");
  if(!path||!*path||!PortDiagnosticsEnabled()||!livePlay)return;
  static std::vector<aurora::vita::PerformanceSnapshot> rows;
  static bool written=false;
  static size_t seen=0;
  if(written)return;
  const auto number=[](const char* name,unsigned fallback,unsigned maximum) {
    const char* text=std::getenv(name);if(!text||!*text)return fallback;
    char* end=nullptr;unsigned long value=std::strtoul(text,&end,10);
    return end!=text&&!*end&&value<=maximum?unsigned(value):fallback;
  };
  static const size_t limit=number("STRIKERS_PERFORMANCE_CAPTURE_FRAMES",600,3600);
  static const size_t warmup=number("STRIKERS_PERFORMANCE_CAPTURE_SKIP",600,36000);
  if(!limit){written=true;return;}
  if(rows.empty())rows.reserve(limit);
  const auto sample=aurora::vita::completed_performance_snapshot();
  if(!sample.completedFrame)return;
  if(seen++<warmup)return;
  rows.push_back(sample);
  if(rows.size()<limit)return;
  written=true;
  FILE* file=std::fopen(path,"w");if(!file)return;
  // Vita's minimal printf does not support the size_t length modifier.
  std::fprintf(file,"# strikers-consumer-capture-v1 diagnostics=1 warmup=%u samples=%u; nested phases and overlapping threads must not be summed\n",unsigned(warmup),unsigned(rows.size()));
  std::fprintf(file,"frame,frame_us,gx_total_us,xf_pos_inspected,xf_pos_unchanged,xf_pos_changed,producer_wait_us,consumer_wait_us,draws,vertices,gpu_vertices,texture_uploads,texture_upload_bytes,geometry_hits,geometry_misses,geometry_bytes,prepared_hits,prepared_misses,prepared_rejected,pool_busy_fallbacks,core3_total_pct_x100,finish_calls,finish_wait_us,display_queue_us,native_texture_hits,native_geometry_hits,native_rejected,scene0_wait_us,scene1_wait_us,scene2_wait_us,scene3_wait_us,core3_telemetry_valid,core3_chunks,core3_denied,core3_telemetry_failures,core3_overruns,native_misses,vertex_delta_copies,vertex_delta_saved_calls,vertex_delta_fallbacks,vertex_delta_copied_bytes,vertex_delta_saved_bytes");
  using namespace aurora::vita::gfx;
  for(size_t i=0;i<size_t(TelemetryPhase::Count);++i)std::fprintf(file,",%s_us",telemetry_phase_name(TelemetryPhase(i)));
  std::fprintf(file,"\n");
  for(const auto& s:rows) {
    const auto& c=s.frontend.counters;uint64_t calls=0,wait=0;
    for(size_t i=0;i<FinishReasonCount;++i){calls+=s.nativeFinishReasonCalls[i];wait+=s.nativeFinishReasonWaitUs[i];}
    const uint64_t values[]={s.frameIndex,s.frameUs,s.gxProcessTotalUs,s.xfPositionWritesInspected,s.xfPositionWritesUnchanged,s.xfPositionWritesChanged,s.producerWaitUs,s.consumerWaitUs,c.draws,c.vertices,c.gpuVertices,c.textureUploads,c.textureUploadBytes,s.staticGeometryHits,s.staticGeometryMisses,s.staticGeometryBytes,s.preparedListHits,s.preparedListMisses,s.preparedListRejected,s.poolBusyFallbacks,s.core3LastTotalPercentX100,calls,wait,s.displayQueueLastUs,s.nativeAssets.textureHits,s.nativeAssets.geometryHits,s.nativeAssets.rejected,s.diagSceneGpuUs[0],s.diagSceneGpuUs[1],s.diagSceneGpuUs[2],s.diagSceneGpuUs[3],s.core3TelemetryValid,s.core3Chunks,s.core3Denied,s.core3TelemetryFailures,s.core3Overruns,s.nativeAssets.misses,s.nativeVertexDeltaCopies,s.nativeVertexDeltaSavedCalls,s.nativeVertexDeltaFallbacks,s.nativeVertexDeltaCopiedBytes,s.nativeVertexDeltaSavedBytes};
    for(size_t i=0;i<sizeof(values)/sizeof(values[0]);++i)std::fprintf(file,"%s%llu",i?",":"",(unsigned long long)values[i]);
    for(uint64_t us:s.frontend.phaseUs)std::fprintf(file,",%llu",(unsigned long long)us);
    std::fprintf(file,"\n");
  }
  std::fclose(file);rows.clear();rows.shrink_to_fit();
}
} // namespace port
