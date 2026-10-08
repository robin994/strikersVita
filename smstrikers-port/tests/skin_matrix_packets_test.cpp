#include "port/skin_matrix_packets.hpp"
#include <atomic>
#include <cstdio>
#include <thread>

namespace
{
using Packets = port::SkinMatrixPackets;
unsigned int checks = 0;
unsigned int failures = 0;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #value); } } while (0)

void fill(std::vector<port::SkinMatrixSource>& source, size_t count)
{
    source.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        source[i].reg = static_cast<int>((i % 10) * 3);
        Mtx pose;
        PSMTXScale(pose, 1.0f + (i % 3) * 0.25f, 2.0f, 0.5f);
        pose[0][3] = static_cast<float>(i);
        pose[1][3] = 3.0f;
        std::memcpy(source[i].mat, pose, sizeof(Mtx));
    }
}

void makeView(Mtx view)
{
    PSMTXRotRad(view, 'y', 0.7f);
    view[0][3] = 2.0f;
    view[1][3] = -1.0f;
    view[2][3] = 4.0f;
}

bool serial(size_t count, Packets::RangeTask task, void* context) noexcept
{
    return task(context, 0, count, 0);
}

bool parallel(size_t count, Packets::RangeTask task, void* context) noexcept
{
    // Deliberately uneven ranges exercise the tail and independent output spans.
    const size_t a = count / 3;
    const size_t b = 2 * a;
    bool ok[3]{};
    std::atomic<unsigned int> arrivals{0};
    std::atomic<bool> start{false};
    auto worker = [&](size_t begin, size_t end, uint32_t lane) {
        arrivals.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        ok[lane] = task(context, begin, end, lane);
    };
    std::thread first(worker, a, b, 1);
    std::thread second(worker, b, count, 2);
    while (arrivals.load(std::memory_order_acquire) != 2)
        std::this_thread::yield();
    start.store(true, std::memory_order_release);
    ok[0] = task(context, 0, a, 0);
    first.join();
    second.join();
    return ok[0] && ok[1] && ok[2];
}

bool parallelFour(size_t count, Packets::RangeTask task, void* context) noexcept
{
    bool ok[4]{};
    std::thread workers[3];
    for(unsigned lane=1;lane<4;++lane)
        workers[lane-1]=std::thread([&,lane] {ok[lane]=task(context,count*lane/4,count*(lane+1)/4,lane);});
    ok[0]=task(context,0,count/4,0);
    for(auto& worker:workers)worker.join();
    return ok[0]&&ok[1]&&ok[2]&&ok[3];
}

void verify(const port::SkinMatrixResult* results, const port::SkinMatrixSource* source,
            size_t count, const Mtx view, bool inverse)
{
    CHECK(results != nullptr);
    if (results == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
    {
        Mtx position, normal;
        PSMTXConcat(view, *reinterpret_cast<const Mtx*>(source[i].mat), position);
        CHECK(std::memcmp(position, results[i].position, sizeof(Mtx)) == 0);
        CHECK(results[i].valid);
        if (inverse)
        {
            CHECK(PSMTXInvXpose(position, normal) != 0);
            CHECK(std::memcmp(normal, results[i].normal, sizeof(Mtx)) == 0);
        }
    }
}

void equivalence(bool inverse)
{
    Mtx view;
    makeView(view);
    std::vector<port::SkinMatrixSource> source;
    fill(source, 257);
    const auto original = source;
    Packets packets;
    packets.begin(view, inverse);
    // Capture keys out of submission order; lookup must not change bone order.
    CHECK(packets.capture(30, source.data(), 31));
    CHECK(packets.capture(10, source.data() + 31, 127));
    CHECK(packets.capture(20, source.data() + 158, 99));
    CHECK(packets.prepare(parallel));
    verify(packets.find(30, source.data(), 31, view, inverse), source.data(), 31, view, inverse);
    verify(packets.find(10, source.data() + 31, 127, view, inverse), source.data() + 31, 127, view, inverse);
    verify(packets.find(20, source.data() + 158, 99, view, inverse), source.data() + 158, 99, view, inverse);
    CHECK(packets.laneItems(0) == 85);
    CHECK(packets.laneItems(1) == 85);
    CHECK(packets.laneItems(2) == 87);
    CHECK(packets.laneItems(3) == 0);
    CHECK(std::memcmp(source.data(), original.data(), source.size() * sizeof(source[0])) == 0);
    Packets four;four.begin(view,inverse);
    CHECK(four.capture(30,source.data(),source.size()));CHECK(four.prepare(parallelFour));
    verify(four.find(30,source.data(),source.size(),view,inverse),source.data(),source.size(),view,inverse);
    CHECK(four.laneItems(3)==65);
    CHECK(four.laneItems(0)+four.laneItems(1)+four.laneItems(2)+four.laneItems(3)==source.size());
}

// A guest write during dispatch must neither change worker input nor produce
// stale GX loads. Restore it afterwards to inspect the immutable computation.
port::SkinMatrixSource* mutatingSource;
bool mutateDuringDispatch(size_t count, Packets::RangeTask task, void* context) noexcept
{
    mutatingSource[0].mat[0] += 1000.0f;
    return parallel(count, task, context);
}

void mutationAndReuse()
{
    Mtx view;
    makeView(view);
    std::vector<port::SkinMatrixSource> source;
    fill(source, 128);
    const auto original = source;
    Packets packets;
    packets.begin(view, true);
    CHECK(packets.capture(1, source.data(), source.size()));
    mutatingSource = source.data();
    CHECK(packets.prepare(mutateDuringDispatch));
    CHECK(packets.find(1, source.data(), source.size(), view, true) == nullptr);
    source[0] = original[0];
    verify(packets.find(1, source.data(), source.size(), view, true), original.data(), original.size(), view, true);
    // Slots are part of the validated payload, even when all matrix bytes match.
    ++source.back().reg;
    CHECK(packets.find(1, source.data(), source.size(), view, true) == nullptr);
    source.back() = original.back();
    const auto sameBytes = source;
    CHECK(packets.find(1, sameBytes.data(), sameBytes.size(), view, true) == nullptr);
    CHECK(packets.find(1, source.data(), source.size() - 1, view, true) == nullptr);
    CHECK(packets.find(2, source.data(), source.size(), view, true) == nullptr);
    CHECK(packets.find(1, source.data(), source.size(), view, false) == nullptr);
    Mtx changedView;
    std::memcpy(changedView, view, sizeof(Mtx));
    changedView[1][3] += 1;
    CHECK(packets.find(1, source.data(), source.size(), changedView, true) == nullptr);
    packets.clear();
    CHECK(packets.find(1, source.data(), source.size(), view, true) == nullptr);
    // The allocator reuses the same key and address for a new frame's content.
    source[0].mat[3] += 200.0f;
    packets.begin(view, false);
    CHECK(packets.capture(1, source.data(), source.size()));
    CHECK(packets.prepare(serial));
    verify(packets.find(1, source.data(), source.size(), view, false), source.data(), source.size(), view, false);
    CHECK(packets.laneItems(0) == source.size());
    CHECK(packets.laneItems(1) == 0);
    CHECK(!packets.capture(2, source.data(), 1));
}

void singularFallback()
{
    Mtx view;
    makeView(view);
    std::vector<port::SkinMatrixSource> source;
    fill(source, 130);
    std::memset(source[128].mat, 0, sizeof(Mtx));
    Packets packets;
    packets.begin(view, true);
    CHECK(packets.capture(1, source.data(), 127));
    CHECK(packets.capture(2, source.data() + 127, 3));
    CHECK(packets.prepare(parallel));
    CHECK(packets.find(1, source.data(), 127, view, true) != nullptr);
    CHECK(packets.find(2, source.data() + 127, 3, view, true) == nullptr);
    packets.begin(view, false); // No inverse requested: a singular pose is allowed.
    CHECK(packets.capture(1, source.data(), source.size()));
    CHECK(packets.prepare(serial));
    CHECK(packets.find(1, source.data(), source.size(), view, false) != nullptr);
}

unsigned int dispatchCalls = 0;
bool countedDispatch(size_t count, Packets::RangeTask task, void* context) noexcept
{
    ++dispatchCalls;
    return serial(count, task, context);
}
bool rejectDispatch(size_t count, Packets::RangeTask task, void* context) noexcept
{
    task(context, 0, count / 2, 0); // Partially finished work must not become visible.
    return false;
}
bool forbiddenLane(size_t count, Packets::RangeTask task, void* context) noexcept
{
    return task(context, 0, count, Packets::ExecutionLanes);
}

void boundsAndFailure()
{
    Mtx view;
    makeView(view);
    std::vector<port::SkinMatrixSource> source;
    fill(source, Packets::MaxMatrices);
    Packets packets;
    packets.begin(view, false);
    CHECK(!packets.prepare(countedDispatch));
    CHECK(dispatchCalls == 0);
    CHECK(packets.capture(1, source.data(), 127));
    CHECK(!packets.prepare(countedDispatch));
    CHECK(dispatchCalls == 0);
    CHECK(packets.capture(2, source.data() + 127, 1));
    CHECK(packets.prepare(countedDispatch));
    CHECK(dispatchCalls == 1);
    CHECK(packets.find(2, source.data() + 127, 1, view, false) != nullptr);
    packets.begin(view, true);
    CHECK(packets.capture(1, source.data(), source.size()));
    CHECK(packets.prepare(parallel));
    verify(packets.find(1, source.data(), source.size(), view, true), source.data(), source.size(), view, true);
    packets.begin(view, false);
    CHECK(packets.capture(1, source.data(), source.size()));
    CHECK(!packets.capture(2, source.data(), 1));
    CHECK(packets.blocked());
    CHECK(!packets.prepare(countedDispatch));
    CHECK(dispatchCalls == 1);
    CHECK(packets.find(1, source.data(), source.size(), view, false) == nullptr);
    packets.begin(view, false);
    CHECK(!packets.capture(1, source.data(), Packets::MaxMatrices + 1));
    CHECK(packets.matrices() == 0);
    packets.begin(view, false);
    for (size_t i = 0; i < Packets::MaxPackets; ++i)
        CHECK(packets.capture(i + 1, source.data(), 1));
    CHECK(!packets.capture(Packets::MaxPackets + 1, source.data(), 1));
    CHECK(!packets.prepare(countedDispatch));
    packets.begin(view, false);
    CHECK(!packets.capture(1, source.data(), 0));
    packets.begin(view, false);
    CHECK(!packets.capture(1, nullptr, 1));
    packets.begin(view, false);
    CHECK(!packets.capture(0, source.data(), 1));
    packets.begin(view, false);
    CHECK(packets.capture(1, source.data(), 128));
    CHECK(!packets.prepare(nullptr));
    CHECK(!packets.prepare(rejectDispatch));
    CHECK(packets.find(1, source.data(), 128, view, false) == nullptr);
    CHECK(!packets.prepare(forbiddenLane));
    CHECK(packets.find(1, source.data(), 128, view, false) == nullptr);
    CHECK(packets.laneItems(3) == 0);
}
} // namespace

int main()
{
    equivalence(false);
    equivalence(true);
    mutationAndReuse();
    singularFallback();
    boundsAndFailure();
    std::printf("skin_matrix_packets: 5 cases, %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
