#pragma once
#include <cstdint>
#include <cstddef>

namespace port {
// Resolve ARAM/stream addresses once per voice render tick. Never retain this
// view between ticks: stream storage can be replaced or refilled.
struct AudioSampleView {
    const uint8_t* data;
    const void* extra;
    uint32_t length;
    uint8_t format;
    bool stream;

    AudioSampleView(const uint8_t* bytes, const void* info, uint32_t count, uint8_t type)
        : data(bytes), extra(info), length(count), format(type)
        , stream(type == 4 || type == 5 || type == 6) {}

    uint32_t bounded_index(uint32_t index) const
    {
        return !stream && length && index >= length ? length - 1 : index;
    }
    bool pcm16() const { return format == 2 || format == 6; }
    int32_t read_pcm(uint32_t index) const
    {
        if (data == nullptr)
            return 0;
        index = bounded_index(index);
        if (pcm16())
        {
            const auto* p = data + static_cast<std::size_t>(index) * 2;
            return static_cast<int16_t>((uint16_t(p[0]) << 8) | p[1]);
        }
        return static_cast<int32_t>(static_cast<int8_t>(data[index])) * 256;
    }
};
} // namespace port
