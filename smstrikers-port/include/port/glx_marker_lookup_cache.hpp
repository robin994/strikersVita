#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace port::glx
{
// Fixed-capacity CPU-only cache for stable marker-tree texture handles.
// A change of marker level, texture registration or texture-resource lifetime
// increments the generation. No pointers survive an epoch transition as hits.
//
// The GLX registry and this cache belong exclusively to the frontend thread:
// do not use this type to make GLX registry queries asynchronous.
template <typename Value, size_t Sets = 128>
class MarkerLookupCache final
{
    static_assert(Sets > 0 && (Sets & (Sets - 1)) == 0, "Power-of-two set count required");
    struct Entry
    {
        uintptr_t key = 0;
        Value value{};
        uint64_t epoch = 0;
    };
    std::array<Entry, Sets * 2> entries{};
    std::array<uint8_t, Sets> nextWay{};
    uint64_t epoch = 1;

    static size_t set_for(uintptr_t key) noexcept
    {
        // Hash aligned 32-bit game asset IDs and pointer-width keys alike.
        uint32_t v = uint32_t(key ^ (key >> (sizeof(uintptr_t) * 4)));
        v ^= v >> 16;
        v *= 0x7feb352du;
        v ^= v >> 15;
        return v & (Sets - 1);
    }

  public:
    static constexpr size_t Capacity = Sets * 2;

    bool find(uintptr_t key, Value& result) const noexcept
    {
        const auto base = set_for(key) * 2;
        for (size_t way = 0; way != 2; ++way)
        {
            const Entry& entry = entries[base + way];
            if (entry.epoch == epoch && entry.key == key)
            {
                result = entry.value;
                return true;
            }
        }
        return false;
    }

    void remember(uintptr_t key, Value value) noexcept
    {
        const size_t set = set_for(key);
        const size_t base = set * 2;
        for (size_t way = 0; way != 2; ++way)
        {
            Entry& entry = entries[base + way];
            if (entry.epoch == epoch && entry.key == key)
            {
                entry.value = value;
                return;
            }
        }
        const auto way = size_t(nextWay[set] & 1);
        nextWay[set] ^= 1;
        entries[base + way] = Entry{ key, value, epoch };
    }

    void invalidate() noexcept
    {
        if (++epoch != 0)
            return;
        // The only wrap case also wipes all previously stamped entries.
        for (auto& entry : entries)
            entry = {};
        epoch = 1;
    }

    uint64_t generation() const noexcept { return epoch; }
};
} // namespace port::glx
