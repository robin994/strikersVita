#pragma once

#include <aurora_vita_view_draw_capture.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace port {

// Raw artifact contains the complete ordered event stream but intentionally
// does NOT invent SELF, INI or shader-cache hashes. Seal it offline with the
// actual artifacts before allowing the analysis to report a complete capture.
inline void capture_view_draw(bool livePlay) {
    static bool enabled = [] {
        const char* setting = std::getenv("STRIKERS_VITA_VIEW_DRAW_CAPTURE");
        return setting && std::strcmp(setting, "1") == 0;
    }();
    if (!enabled) return;
    static bool finished = false;
    static bool recording = false;
    static unsigned gameplayFrames = 0;
    static unsigned collectedFrames = 0;
    const auto setting = [](const char* name, unsigned fallback, unsigned cap) {
        const char* value = std::getenv(name);
        if (!value || !*value) return fallback;
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        return end != value && !*end && parsed <= cap ? static_cast<unsigned>(parsed) : fallback;
    };
    static const unsigned skip = setting("STRIKERS_VITA_VIEW_DRAW_SKIP", 600, 36000);
    static const unsigned frames = setting("STRIKERS_VITA_VIEW_DRAW_FRAMES", 12, 300);
    static const unsigned capacity = setting("STRIKERS_VITA_VIEW_DRAW_CAPACITY", 8192, 32768);
    static const bool capturePayloads = setting("STRIKERS_VITA_VIEW_DRAW_PAYLOADS", 0, 1) != 0;
    if (finished || !livePlay) return;
    if (!recording) {
        if (gameplayFrames++ < skip) return;
        recording = aurora_vita_view_draw_start_ex(capacity, capturePayloads ? 1 : 0) != 0;
        if (!recording) finished = true;
        return;
    }
    if (++collectedFrames < frames) return;
    aurora_vita_view_draw_stop();
    finished = true;
    const char* selected = std::getenv("STRIKERS_VITA_VIEW_DRAW_PATH");
    const char* path = selected && *selected ? selected : "ux0:data/strikersVita/view-draw-raw.jsonl";
    FILE* file = std::fopen(path, "wb");
    if (!file) return;
    std::fprintf(file, "# aurora-vita-view-draw-raw-v2\n");
    const size_t count = aurora_vita_view_draw_count();
    AuroraViewDrawRecord rows[16]{};
    for (size_t offset = 0; offset < count;) {
        const size_t fetched = aurora_vita_view_draw_read(offset, rows, 16);
        if (!fetched) break;
        for (size_t i = 0; i < fetched; ++i) {
            const AuroraViewDrawRecord& row = rows[i];
            if (row.type == AURORA_VIEW_DRAW_MARKER)
                std::fprintf(file, "{\"type\":\"view\",\"sequence\":%llu,\"producer_frame\":%llu,\"view\":%u}\n",
                    static_cast<unsigned long long>(row.sequence), static_cast<unsigned long long>(row.producer_frame), unsigned(row.view));
            else if (row.type == AURORA_VIEW_DRAW_FRAME_COMPLETE)
                std::fprintf(file, "{\"type\":\"frame_complete\",\"sequence\":%llu,\"producer_frame\":%llu,\"consumer_frame\":%llu}\n",
                    static_cast<unsigned long long>(row.sequence), static_cast<unsigned long long>(row.producer_frame),
                    static_cast<unsigned long long>(row.consumer_frame));
            else if (row.type == AURORA_VIEW_DRAW_DRAW)
                std::fprintf(file,
                    "{\"type\":\"draw\",\"sequence\":%llu,\"producer_frame\":%llu,\"consumer_frame\":%llu,"
                    "\"view\":%u,\"logical_draw\":%u,\"target\":%u,\"tev_stages\":%u,"
                    "\"texture_mask\":%u,\"texgen_mask\":%u,\"vertex_count\":%u,\"index_count\":%u,"
                    "\"submit_cpu_us\":%llu,\"uniform_bytes\":%llu,\"program_native\":%u,"
                    "\"indexed_pn\":%u,\"blend\":%u,\"depth\":%u,\"alpha\":%u,\"alpha_ref0\":%u,\"alpha_ref1\":%u,\"alpha_op\":%u,\"scissor\":%u,"
                    "\"pipeline_requested\":\"%016llx\",\"pipeline_active\":\"%016llx\","
                    "\"vertex_hash\":\"%016llx\",\"fragment_hash\":\"%016llx\","
                    "\"payload_hashes_present\":%u,\"vertex_payload_hash\":\"%016llx\","
                    "\"index_payload_hash\":\"%016llx\",\"uniform_payload_hash\":\"%016llx\","
                    "\"draw_state_hash\":\"%016llx\"}\n",
                    static_cast<unsigned long long>(row.sequence),static_cast<unsigned long long>(row.producer_frame),
                    static_cast<unsigned long long>(row.consumer_frame),unsigned(row.view),unsigned(row.logical_draw),
                    unsigned(row.target),unsigned(row.tev_stages),unsigned(row.texture_mask),unsigned(row.texgen_mask),
                    unsigned(row.vertex_count),unsigned(row.index_count),static_cast<unsigned long long>(row.submit_cpu_us),
                    static_cast<unsigned long long>(row.uniform_bytes),unsigned(row.program_native),unsigned(row.indexed_pn),
                    unsigned(row.blend),unsigned(row.depth),unsigned(row.alpha),unsigned(row.alpha_ref0),
                    unsigned(row.alpha_ref1),unsigned(row.alpha_op),unsigned(row.scissor),
                    static_cast<unsigned long long>(row.pipeline_requested),static_cast<unsigned long long>(row.pipeline_active),
                    static_cast<unsigned long long>(row.vertex_hash),static_cast<unsigned long long>(row.fragment_hash),
                    unsigned(row.payload_hashes_present),
                    static_cast<unsigned long long>(row.vertex_payload_hash),
                    static_cast<unsigned long long>(row.index_payload_hash),
                    static_cast<unsigned long long>(row.uniform_payload_hash),
                    static_cast<unsigned long long>(row.draw_state_hash));
        }
        offset += fetched;
    }
    std::fprintf(file, "# end records=%llu dropped=%llu capacity=%llu\n",
                 static_cast<unsigned long long>(count),static_cast<unsigned long long>(aurora_vita_view_draw_lost()),
                 static_cast<unsigned long long>(aurora_vita_view_draw_capacity()));
    std::fclose(file);
}

} // namespace port
