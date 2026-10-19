#if defined(STRIKERS_VITA_NATIVE_RUNTIME) || defined(STRIKERS_PACKET_PROFILE_TEST)
#include "port/packet_profile.hpp"
#include "port/host.h"
#include <cerrno>
#include <cstdio>
#include <numeric>

namespace port::render
{
void PacketProfile::Stats::add(const Stats& other) noexcept
{
    for (size_t i = 0; i < ns.size(); ++i) ns[i] += other.ns[i];
    for (size_t i = 0; i < counts.size(); ++i) counts[i] += other.counts[i];
}
PacketProfile::Scope::Scope(PacketProfile& profile, unsigned v, uint32_t references, Wait w) noexcept
{
    if (!profile.recording()) return;
    owner = &profile;
    view = v < Views - 1 ? v : Views - 1;
    wait = w;
    initialWait = wait ? wait() : 0;
    stats.emplace();
    stats->counts[Attempts] = 1;
    stats->counts[InputReferences] = references;
    previous = port_monotonic_ns();
}
void PacketProfile::Scope::next(Phase nextPhase) noexcept
{
    if (!owner) return;
    const auto now = port_monotonic_ns();
    stats->ns[phase] += now - previous;
    previous = now;
    phase = nextPhase;
}
void PacketProfile::Scope::submitted(uint32_t model, size_t vertices, size_t indices) noexcept
{
    if (!owner) return;
    stats->counts[Accepted] = 1;
    ++stats->counts[model ? (vertices ? ResidentBuilds : ResidentHits) : DynamicDraws];
    stats->counts[DecodedVertices] = vertices;
    stats->counts[DecodedIndices] = indices;
    if (!model)
    {
        stats->counts[VertexBytes] = vertices * sizeof(ModelVertex);
        stats->counts[IndexBytes] = indices * sizeof(uint16_t);
    }
}
PacketProfile::Scope::~Scope()
{
    if (!owner) return;
    next(Cleanup);
    stats->counts[Rejected] = !stats->counts[Accepted];
    const auto finalWait = wait ? wait() : initialWait;
    stats->counts[PacketWaitNs] = (finalWait - initialWait) * 1000;
    owner->current.add(*stats);
    owner->currentViews[view].add(*stats);
}
bool PacketProfile::configure(const char* capturePath, size_t requested) noexcept
{
    if (enabled()) return false;
    if (!capturePath || !*capturePath || !requested || requested > 8192) return false;
    try
    {
        path = capturePath;
        frames.reserve(requested);
        goal = requested;
        return true;
    }
    catch (...) { error = ENOMEM; return false; }
}
void PacketProfile::begin_frame(const PortBenchCaptureStatus& status) noexcept
{
    armed = enabled() && !finished && status.state == PORT_CAPTURE_COLLECTING && !status.warmupRemaining;
    if (!armed) return;
    before = status.samples;
    current = {};
    currentViews = {};
}
void PacketProfile::end_frame(const PortBenchCaptureStatus& status, uint32_t engineFrame,
                              bool discarded, const Renderer::FrameTiming& timing) noexcept
{
    const bool selected = armed && status.samples == before + 1;
    armed = false;
    if (!selected || finished) return;
    // The benchmark owns eligibility and logical-frame identity. A missed
    // sample is an error, never silently replaced by an intro/replay frame.
    if (before != frames.size() || frames.size() >= goal)
    {
        error = EINVAL;
        finished = true;
        return;
    }
    frames.push_back(Frame{before, status.lastFrame, engineFrame, discarded, current, timing});
    for (size_t i = 0; i < Views; ++i) totals[i].add(currentViews[i]);
    if (frames.size() == goal)
    {
        finished = true;
        write();
    }
}
namespace
{
constexpr const char* columns =
    "packet_ns,invalidation_ns,arrays_ns,skin_ns,material_ns,geometry_ns,submit_ns,cleanup_ns,"
    "attempts,accepted,rejected,resident_hits,resident_builds,dynamic_draws,input_references,"
    "decoded_vertices,decoded_indices,vertex_bytes,index_bytes,packet_wait_ns";
bool stats(FILE* f, const PacketProfile::Stats& s)
{
    if (std::fprintf(f, "%llu", (unsigned long long)std::accumulate(s.ns.begin(), s.ns.end(), uint64_t{})) < 0) return false;
    for (auto n : s.ns) if (std::fprintf(f, ",%llu", (unsigned long long)n) < 0) return false;
    for (auto n : s.counts) if (std::fprintf(f, ",%llu", (unsigned long long)n) < 0) return false;
    return true;
}
template <class Writer> bool output(const std::string& path, Writer writer)
{
    const auto temp = path + ".tmp";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    bool ok = writer(f);
    if (std::fclose(f)) ok = false;
    if (!ok) { std::remove(temp.c_str()); return false; }
    if (std::rename(temp.c_str(), path.c_str())) { std::remove(temp.c_str()); return false; }
    return true;
}
}
bool PacketProfile::write() noexcept
{
    if (!enabled() || frames.size() != goal) return false;
    try
    {
        errno = 0;
        const bool uniforms = frames.front().renderer.uniforms.valid;
        for (const auto& row : frames)
            if (row.renderer.uniforms.valid != uniforms)
            {
                error = EINVAL;
                return false;
            }
        bool ok = output(path + ".packets.csv", [&](FILE* f) {
            if (std::fprintf(f, "# strikers-packet-profile-v1 attribution_only=1 waits_are_subsets=1\n"
                "sample,live_frame,engine_frame,discarded,%s,owner_draw_us,owner_present_us,frontend_wait_us,"
                "command_bytes,command_copy_ns,command_vertex_bytes,command_index_bytes,slot_waits,renderer_draws\n", columns) < 0) return false;
            for (const auto& row : frames)
            {
                // Vita's formatter lacks %z. Capture and view indices are
                // bounded by configure's 8192 samples and the 35 view slots.
                if (std::fprintf(f, "%u,%lu,%u,%u,", unsigned(row.sample), row.liveFrame, row.engineFrame, unsigned(row.discarded)) < 0 || !stats(f, row.producer)) return false;
                const auto& r = row.renderer;
                if (std::fprintf(f, ",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u\n",
                    (unsigned long long)r.drawUs, (unsigned long long)r.queueUs,
                    (unsigned long long)r.frontendWaitUs, (unsigned long long)r.commandBytes,
                    (unsigned long long)r.commandCopyNs, (unsigned long long)r.commandVertexBytes,
                    (unsigned long long)r.commandIndexBytes, r.slotWaits, r.draws) < 0) return false;
            }
            return true;
        });
        if (ok) ok = output(path + ".views.csv", [&](FILE* f) {
            if (std::fprintf(f, "# strikers-packet-profile-views-v1 samples=%u\nview,%s\n", unsigned(frames.size()), columns) < 0) return false;
            for (size_t i = 0; i < Views; ++i)
                if (totals[i].counts[Attempts] && (std::fprintf(f, "%u,", unsigned(i)) < 0 || !stats(f, totals[i]) || std::fputc('\n', f) == EOF)) return false;
            return true;
        });
        if (ok && uniforms) ok = output(path + ".uniforms.csv", [&](FILE* f) {
            if (std::fprintf(f, "# strikers-uniform-profile-v1 quantities_only=1\n"
                "sample,live_frame,engine_frame,discarded,cache_enabled,"
                "vs_pack_calls,vs_pack_bytes,vs_reservations,vs_reserved_bytes,vs_cache_copies,vs_cache_copy_bytes,vs_reused_values,vs_reused_bytes,"
                "fs_pack_calls,fs_pack_bytes,fs_reservations,fs_reserved_bytes,fs_cache_copies,fs_cache_copy_bytes,fs_reused_values,fs_reused_bytes,"
                "fallback_uploads,errors,cache_bytes,cache_peak_bytes\n") < 0) return false;
            for (const auto& row : frames)
            {
                const auto& u = row.renderer.uniforms;
                if (std::fprintf(f, "%u,%lu,%u,%u,%u", unsigned(row.sample), row.liveFrame,
                    row.engineFrame, unsigned(row.discarded), unsigned(u.enabled)) < 0) return false;
                for (const auto& stage : u.stage)
                    for (auto n : stage)
                        if (std::fprintf(f, ",%llu", (unsigned long long)n) < 0) return false;
                if (std::fprintf(f, ",%llu,%llu,%llu,%llu\n", (unsigned long long)u.fallbackUploads,
                    (unsigned long long)u.errors, (unsigned long long)u.cacheBytes,
                    (unsigned long long)u.cachePeakBytes) < 0) return false;
            }
            return true;
        });
        error = ok ? 0 : (errno ? errno : EIO);
    }
    catch (...) { error = ENOMEM; }
    std::fprintf(stderr, "[packet-profile] samples=%u output_error=%d path=%s\n", unsigned(frames.size()), error, path.c_str());
    return !error;
}
} // namespace port::render
#endif
