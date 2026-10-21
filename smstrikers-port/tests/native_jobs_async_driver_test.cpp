#include "port/native_jobs.hpp"

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <thread>
#include <vector>

namespace
{
pthread_t caller{};
std::atomic<unsigned> affinities{0};
std::atomic<unsigned> priorities{0};

struct Fixture
{
    std::vector<unsigned> results;
    std::atomic<bool> entered{false}, release{false};
    std::atomic<unsigned> lanes{0};
};

bool async_populate(void* data, size_t begin, size_t end, unsigned lane) noexcept
{
    auto& fixture = *static_cast<Fixture*>(data);
    assert(lane == 1);
    fixture.lanes.fetch_or(1u << lane);
    fixture.entered.store(true, std::memory_order_release);
    while (!fixture.release.load(std::memory_order_acquire))
        std::this_thread::yield();
    for (size_t i = begin; i < end; ++i)
    {
        assert(fixture.results[i] == 0);
        fixture.results[i] = unsigned(i + 1);
    }
    return true;
}
bool sync_populate(void* opaque, size_t begin, size_t end, unsigned lane) noexcept
{
    auto& fixture = *static_cast<Fixture*>(opaque);
    assert(lane == 0);
    fixture.lanes.fetch_or(1u << lane);
    for (size_t i = begin; i < end; ++i)
        fixture.results[i] = unsigned(i + 1);
    return true;
}
bool failed(void*, size_t, size_t, unsigned lane) noexcept
{
    assert(lane == 1);
    return false;
}
void await_worker(const Fixture& fixture)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!fixture.entered.load(std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() >= end)
            std::abort();
        std::this_thread::yield();
    }
}
void check(const Fixture& fixture)
{
    for (size_t i = 0; i < fixture.results.size(); ++i)
        assert(fixture.results[i] == i + 1);
}
} // namespace

extern "C"
{
SceUID sceKernelGetThreadId()
{
    assert(!pthread_equal(pthread_self(), caller));
    return 17;
}
int sceKernelChangeThreadCpuAffinityMask(SceUID id, int affinity)
{
    assert(id == 17 && affinity == SCE_KERNEL_CPU_MASK_USER_0);
    ++affinities;
    return 0;
}
int sceKernelChangeThreadPriority(SceUID id, int priority)
{
    assert(id == 17 && priority == 0x10000100);
    ++priorities;
    return 0;
}
}
int main()
{
    using namespace port::jobs;
    caller = pthread_self();
    assert(worker_threads() == 0);
    {
        Fixture dummy;
        dummy.results.resize(4);
        AsyncWork unavailable;
        assert(!begin_async(unavailable, 4, 1, sync_populate, &dummy));
        assert(!unavailable.active);
        assert(finish_async(unavailable));
        assert(parallel_for(4, 1, sync_populate, &dummy));
    }
    assert(initialize() && worker_threads() == 1);
    for (unsigned i = 0; i != 64; ++i)
    {
        Fixture work;
        work.results.resize(36);
        AsyncWork running;
        assert(begin_async(running, work.results.size(), 3, async_populate, &work));
        assert(running.active);
        await_worker(work);
        Fixture unrelated;
        unrelated.results.resize(91);
        // The worker is still blocked in its own job. Nested parallel_for
        // must not overwrite the callback/context or wait for that worker.
        assert(parallel_for(unrelated.results.size(), 7, sync_populate, &unrelated));
        check(unrelated);
        assert(unrelated.lanes.load() == 1);
        AsyncWork rejected;
        assert(!begin_async(rejected, 42, 3, sync_populate, &unrelated));
        work.release.store(true, std::memory_order_release);
        assert(finish_async(running));
        assert(!running.active && running.result);
        assert(finish_async(running));
        check(work);
        assert(work.lanes.load() == 2);
    }
    {
        AsyncWork failure;
        assert(begin_async(failure, 3, 1, failed, nullptr));
        assert(!finish_async(failure));
        assert(!failure.result);
    }
    {
        Fixture work;
        work.results.resize(17);
        AsyncWork scoped;
        assert(begin_async(scoped, 17, 2, async_populate, &work));
        await_worker(work);
        work.release.store(true, std::memory_order_release);
        // The scoped job's destructor must join before its stack context
        // and destination storage are destroyed.
    }
    // Shutdown after an async job has started waits for worker completion.
    {
        Fixture work;
        work.results.resize(14);
        AsyncWork live;
        assert(begin_async(live, 14, 2, async_populate, &work));
        await_worker(work);
        work.release.store(true, std::memory_order_release);
        shutdown();
        assert(worker_threads() == 0);
        assert(finish_async(live));
        check(work);
    }
    assert(affinities == 1 && priorities == 1);
    std::puts("Async jobs: true worker/frontend overlap, nested serial fallback, failure, "
              "scoped completion, shutdown draining and unchanged CPU affinity PASS");
}
