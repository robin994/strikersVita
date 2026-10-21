#pragma once
#include <cstddef>
#include <cstdint>
namespace port::jobs
{
using Task = bool (*)(void*, size_t, size_t, uint32_t) noexcept;
// One bounded worker-only job. The caller can do unrelated CPU work until
// finish_async joins the worker before touching any of the job's outputs.
struct AsyncWork
{
    bool active = false;
    bool result = true;
    AsyncWork() = default;
    AsyncWork(const AsyncWork&) = delete;
    AsyncWork& operator=(const AsyncWork&) = delete;
    ~AsyncWork();
};
bool initialize() noexcept;
void shutdown() noexcept;
unsigned worker_threads() noexcept;
unsigned game_execution_lanes() noexcept;
bool parallel_for(size_t count, size_t grain, Task task, void* context) noexcept;
bool begin_async(AsyncWork& job, size_t count, size_t grain, Task task, void* context) noexcept;
bool finish_async(AsyncWork& job) noexcept;
}   // namespace port::jobs
