#pragma once

#include <array>
#include <cstdio>

namespace port
{
// Each profiling stream owns its stdio buffer until the stream closes. Configure
// it only after fopen, before any I/O; repeated get() must never call setvbuf.
class ProfileOutput
{
public:
    static const size_t BufferBytes = 64u * 1024u;

    // The path and stream name must outlive this object (the port uses literals).
    ProfileOutput(const char* path, const char* stream, bool buffered)
        : path_(path), stream_(stream), requestedBuffered_(buffered)
    {
    }

    ~ProfileOutput()
    {
        if (file_ != nullptr)
            std::fclose(file_);
    }

    ProfileOutput(const ProfileOutput&) = delete;
    ProfileOutput& operator=(const ProfileOutput&) = delete;

    FILE* get()
    {
        if (file_ != nullptr)
            return file_;
        file_ = std::fopen(path_, "a");
        if (file_ == nullptr)
            return stderr;

        if (requestedBuffered_)
            buffered_ = std::setvbuf(file_, buffer_.data(), _IOFBF, buffer_.size()) == 0;
        if (!buffered_)
            std::setvbuf(file_, nullptr, _IOLBF, 0);
        else
            std::fprintf(file_, "[profile-output] stream=%s buffered=1 buffer_bytes=%u\n",
                         stream_, static_cast<unsigned int>(BufferBytes));
        return file_;
    }

    // Flush completed reports for FTP capture and abrupt app stops. The OFF
    // path retains line buffering and already emits each newline.
    void flush_report()
    {
        if (buffered_ && file_ != nullptr)
            std::fflush(file_);
    }

    bool buffered() const { return buffered_; }

private:
    const char* path_;
    const char* stream_;
    bool requestedBuffered_;
    bool buffered_ = false;
    FILE* file_ = nullptr;
    std::array<char, BufferBytes> buffer_;
};
} // namespace port
