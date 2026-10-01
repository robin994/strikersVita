#include "port/profile_output.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
unsigned int checks = 0;
unsigned int failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

std::string read_file(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return {};
    std::string out;
    char buffer[4096];
    size_t bytes;
    while ((bytes = std::fread(buffer, 1, sizeof(buffer), file)) != 0)
        out.append(buffer, bytes);
    std::fclose(file);
    return out;
}

long long file_size(const std::string& path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0 ? static_cast<long long>(info.st_size) : -1;
}

std::string marker(const char* stream)
{
    return std::string("[profile-output] stream=") + stream + " buffered=1 buffer_bytes=65536\n";
}

void buffered_report(const std::string& path)
{
    {
        port::ProfileOutput out(path.c_str(), "task", true);
        FILE* file = out.get();
        CHECK(file != stderr);
        CHECK(out.buffered());
        std::fputs("[task-profile] frames=120\n", file);
        // Repeated accesses must not reconfigure stdio or flush a partial report.
        for (unsigned int i = 0; i < 100; ++i)
            CHECK(out.get() == file);
        CHECK(file_size(path) == 0);
        out.flush_report();
        CHECK(read_file(path) == marker("task") + "[task-profile] frames=120\n");
        std::fputs("[geometry-profile] hits_delta=99\n", out.get());
        CHECK(read_file(path) == marker("task") + "[task-profile] frames=120\n");
        out.flush_report();
        CHECK(read_file(path) == marker("task") + "[task-profile] frames=120\n[geometry-profile] hits_delta=99\n");
    }
    std::remove(path.c_str());
}

void line_buffered_fallback(const std::string& path)
{
    {
        port::ProfileOutput out(path.c_str(), "task", false);
        FILE* file = out.get();
        CHECK(file != stderr);
        CHECK(!out.buffered());
        std::fputs("first line\n", file);
        CHECK(read_file(path) == "first line\n");
        for (unsigned int i = 0; i < 20; ++i)
            CHECK(out.get() == file);
        std::fputs("second line\n", out.get());
        CHECK(read_file(path) == "first line\nsecond line\n");
        out.flush_report();
        CHECK(read_file(path) == "first line\nsecond line\n");
    }
    std::remove(path.c_str());
}

void bounded_buffer_overflow_and_close(const std::string& path)
{
    std::string expected = marker("render");
    {
        port::ProfileOutput out(path.c_str(), "render", true);
        for (unsigned int i = 0; i < 400; ++i)
        {
            const std::string line = "[view-profile] row=" + std::to_string(i) + " " + std::string(500, 'x') + "\n";
            expected += line;
            std::fputs(line.c_str(), out.get());
        }
        CHECK(expected.size() > port::ProfileOutput::BufferBytes * 3u);
        CHECK(file_size(path) > 0);
        // The last partial stdio buffer is intentionally left for fclose.
    }
    CHECK(read_file(path) == expected);
    std::remove(path.c_str());
}

void independent_append_streams(const std::string& path)
{
    {
        port::ProfileOutput task(path.c_str(), "task", true);
        port::ProfileOutput render(path.c_str(), "render", true);
        std::fputs("task report 1\n", task.get());
        task.flush_report();
        std::fputs("render report 1\n", render.get());
        render.flush_report();
        std::fputs("task report 2\n", task.get());
        task.flush_report();
        std::fputs("render report 2\n", render.get());
        render.flush_report();
        CHECK(read_file(path) == marker("task") + "task report 1\n" + marker("render") +
              "render report 1\ntask report 2\nrender report 2\n");
    }
    std::remove(path.c_str());
}

void failed_open_and_retry(const std::string& directory)
{
    const std::string path = directory + "/later/report.log";
    const std::string parent = directory + "/later";
    {
        port::ProfileOutput out(path.c_str(), "task", true);
        CHECK(out.get() == stderr);
        CHECK(!out.buffered());
        out.flush_report();
        CHECK(mkdir(parent.c_str(), 0700) == 0);
        CHECK(out.get() != stderr);
        CHECK(out.buffered());
        std::fputs("after retry\n", out.get());
        out.flush_report();
        CHECK(read_file(path) == marker("task") + "after retry\n");
    }
    std::remove(path.c_str());
    CHECK(rmdir(parent.c_str()) == 0);
}

void unopened_flush(const std::string& path)
{
    {
        port::ProfileOutput out(path.c_str(), "task", true);
        out.flush_report();
        CHECK(file_size(path) == -1);
        CHECK(!out.buffered());
    }
    CHECK(file_size(path) == -1);
}
} // namespace

int main()
{
    char temporary[] = "/tmp/strikers-profile-output-XXXXXX";
    const char* created = mkdtemp(temporary);
    if (created == nullptr)
        return 2;
    const std::string directory(created);
    buffered_report(directory + "/buffered.log");
    line_buffered_fallback(directory + "/line.log");
    bounded_buffer_overflow_and_close(directory + "/overflow.log");
    independent_append_streams(directory + "/append.log");
    failed_open_and_retry(directory);
    unopened_flush(directory + "/unopened.log");
    CHECK(rmdir(directory.c_str()) == 0);
    std::printf("profile output: 6 cases, %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
