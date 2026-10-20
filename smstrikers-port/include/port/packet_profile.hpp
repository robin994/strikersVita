#pragma once
#include "port/benchmark.h"
#include "port/renderer.hpp"
#include <array>
#include <string>
#include <optional>
#include <vector>

namespace port::render
{
// Diagnostic producer wall time. Phases partition a packet callback; waits
// and renderer-owner time are subsets/overlapping work, never extra phases.
class PacketProfile
{
  public:
    enum Phase { Invalidation, Arrays, Skin, Material, Geometry, Submit, Cleanup, PhaseCount };
    enum Counter
    {
        Attempts, Accepted, Rejected, ResidentHits, ResidentBuilds, DynamicDraws,
        InputReferences, DecodedVertices, DecodedIndices, VertexBytes, IndexBytes,
        PacketWaitNs, CounterCount
    };
    static constexpr unsigned Views = 35; // GLV 0..33, then invalid/unknown.
    struct Stats
    {
        std::array<uint64_t, PhaseCount> ns{};
        std::array<uint64_t, CounterCount> counts{};
        void add(const Stats&) noexcept;
    };
    struct Frame
    {
        size_t sample = 0;
        unsigned long liveFrame = 0;
        uint32_t engineFrame = 0;
        bool discarded = false;
        Stats producer{};
        Renderer::FrameTiming renderer{};
    };
    class Scope
    {
      public:
        using Wait = uint64_t (*)();
        Scope(PacketProfile&, unsigned view, uint32_t references, Wait) noexcept;
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        void next(Phase) noexcept;
        void submitted(uint32_t model, size_t vertices, size_t indices) noexcept;
      private:
        PacketProfile* owner = nullptr;
        Wait wait = nullptr;
        uint64_t previous = 0, initialWait = 0;
        unsigned view = 0;
        Phase phase = Invalidation;
        std::optional<Stats> stats;
    };
    bool configure(const char* capturePath, size_t requested) noexcept;
    bool enabled() const noexcept { return goal != 0; }
    bool recording() const noexcept { return armed; }
    void begin_frame(const PortBenchCaptureStatus&) noexcept;
    void end_frame(const PortBenchCaptureStatus&, uint32_t engineFrame, bool discarded,
                   const Renderer::FrameTiming&) noexcept;
    bool write() noexcept;
    int output_error() const noexcept { return error; }
    const std::vector<Frame>& samples() const noexcept { return frames; }
    const auto& views() const noexcept { return totals; }
  private:
    std::string path;
    size_t goal = 0, before = 0;
    bool armed = false, finished = false;
    int error = 0;
    Stats current{};
    std::array<Stats, Views> currentViews{}, totals{};
    std::vector<Frame> frames;
};
} // namespace port::render
