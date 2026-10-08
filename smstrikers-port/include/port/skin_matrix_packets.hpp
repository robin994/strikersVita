#pragma once

#include "dolphin/mtx.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace port
{
// Matches the GLUD_Skin payload. Workers only see owned copies of these bytes.
struct SkinMatrixSource
{
    int reg;
    float mat[12];
};

struct SkinMatrixResult
{
    Mtx position;
    Mtx normal;
    bool valid;
};

class SkinMatrixPackets
{
public:
    static constexpr size_t MaxPackets = 512;
    static constexpr size_t MaxMatrices = 4096;
    static constexpr size_t MinimumMatrices = 128;
    static constexpr uint32_t ExecutionLanes = 4; // Lane3 is admitted only by the quota scheduler.
    using RangeTask = bool (*)(void*, size_t, size_t, uint32_t) noexcept;
    using Dispatch = bool (*)(size_t, RangeTask, void*) noexcept;

    void begin(const Mtx view, bool inverse)
    {
        clear();
        std::memcpy(view_, view, sizeof(Mtx));
        inverse_ = inverse;
        // Allocate once, before publishing any job. Capacity is retained across views.
        records_.reserve(MaxPackets);
        sources_.reserve(MaxMatrices);
        results_.reserve(MaxMatrices);
    }

    void clear() noexcept
    {
        ready_ = false;
        blocked_ = false;
        records_.clear();
        sources_.clear();
        results_.clear();
        for (size_t& items : laneItems_)
            items = 0;
    }

    bool capture(uintptr_t packet, const void* source, size_t count)
    {
        if (ready_ || blocked_)
            return false;
        if (packet == 0 || source == nullptr || count == 0 ||
            records_.size() == MaxPackets || count > MaxMatrices - sources_.size())
        {
            blocked_ = true;
            return false;
        }
        const size_t offset = sources_.size();
        sources_.resize(offset + count);
        std::memcpy(sources_.data() + offset, source, count * sizeof(SkinMatrixSource));
        records_.push_back({packet, source, offset, count});
        return true;
    }

    bool prepare(Dispatch dispatch) noexcept
    {
        ready_ = false;
        if (blocked_ || sources_.size() < MinimumMatrices || dispatch == nullptr)
            return false;
        std::sort(records_.begin(), records_.end(), [](const Record& a, const Record& b) {
            return a.packet < b.packet;
        });
        results_.resize(sources_.size()); // No allocations: begin() reserved the bound.
        Job job{sources_.data(), results_.data(), view_, inverse_, laneItems_};
        ready_ = dispatch(sources_.size(), calculate, &job);
        return ready_;
    }

    const SkinMatrixResult* find(uintptr_t packet, const void* source, size_t count,
                                 const Mtx currentView, bool inverse) const noexcept
    {
        if (!ready_ || inverse != inverse_ || std::memcmp(view_, currentView, sizeof(Mtx)) != 0)
            return nullptr;
        const auto record = std::lower_bound(records_.begin(), records_.end(), packet,
            [](const Record& a, uintptr_t key) { return a.packet < key; });
        if (record == records_.end() || record->packet != packet ||
            record->source != source || record->count != count ||
            std::memcmp(sources_.data() + record->offset, source,
                        count * sizeof(SkinMatrixSource)) != 0)
            return nullptr;
        // Validate the entire packet before its first GX load. A singular normal
        // matrix must not leave a partially consumed packet on the prepared path.
        const SkinMatrixResult* result = results_.data() + record->offset;
        for (size_t i = 0; i < count; ++i)
            if (!result[i].valid)
                return nullptr;
        return result;
    }

    size_t matrices() const noexcept { return sources_.size(); }
    bool blocked() const noexcept { return blocked_; }
    size_t laneItems(uint32_t lane) const noexcept
    {
        return lane < ExecutionLanes ? laneItems_[lane] : 0;
    }

private:
    struct Record
    {
        uintptr_t packet;
        const void* source; // Owner-only lookup; never dereferenced by workers.
        size_t offset;
        size_t count;
    };
    struct Job
    {
        const SkinMatrixSource* sources;
        SkinMatrixResult* results;
        const float (*view)[4];
        bool inverse;
        size_t* laneItems;
    };

    static bool calculate(void* context, size_t begin, size_t end, uint32_t lane) noexcept
    {
        if (lane >= ExecutionLanes)
            return false;
        const Job& job = *static_cast<const Job*>(context);
        for (size_t i = begin; i < end; ++i)
        {
            SkinMatrixResult& result = job.results[i];
            PSMTXConcat(job.view, *reinterpret_cast<const Mtx*>(job.sources[i].mat), result.position);
            result.valid = !job.inverse || PSMTXInvXpose(result.position, result.normal) != 0;
        }
        // The pool gives each lane disjoint ranges and one active callback.
        job.laneItems[lane] += end - begin;
        return true;
    }

    Mtx view_{};
    bool inverse_ = false;
    bool ready_ = false;
    bool blocked_ = false;
    size_t laneItems_[ExecutionLanes]{};
    std::vector<Record> records_;
    std::vector<SkinMatrixSource> sources_;
    std::vector<SkinMatrixResult> results_;
};
} // namespace port
