#include "port/vita_profiler.h"

#if defined(PORT_VITA) && defined(STRIKERS_VITA_PROFILER)

#include "NL/nlDLRing.h"
#include "NL/nlTask.h"

#include <vitaprofiler.h>
#include <vitaprofiler_stream.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>

namespace {

constexpr uint32_t kRingCapacity = 4096;
constexpr uint32_t kNameCapacity = 128;
constexpr size_t kNameTextCapacity = 8192;
constexpr size_t kStreamBufferCapacity = 16 * 1024;
constexpr size_t kDrainBatch = kStreamBufferCapacity / VP_WIRE_EVENT_SIZE;
constexpr uint32_t kTaskCapacity = 48;

vp_context s_context;
vp_slot s_slots[kRingCapacity];
vp_name_dictionary s_names;
vp_name_entry s_nameEntries[kNameCapacity];
char s_nameText[kNameTextCapacity];
uint8_t s_streamBuffer[kStreamBufferCapacity];
vp_stream_writer_v2 s_writer;

uint32_t s_frameName;
uint32_t s_tasksName;
uint32_t s_audioFillName;
uint32_t s_audioTickName;
uint32_t s_audioQueuedName;
uint32_t s_audioProducedName;
uint32_t s_mainThreadName;
uint32_t s_audioThreadName;
uint32_t s_gxThreadName;
uint32_t s_gxPublishName;
uint32_t s_gxConsumeName;
uint32_t s_unknownTaskRunName;
uint32_t s_unknownTaskTransitionName;
uint32_t s_renderPhaseNames[5];
uint32_t s_renderViewNames[34];

struct TaskNames
{
    const void* task;
    uint32_t runName;
    uint32_t transitionName;
};
TaskNames s_taskNames[kTaskCapacity];
uint32_t s_taskNameCount;

std::atomic<bool> s_active{false};
std::atomic<bool> s_writerStop{false};
std::atomic<unsigned int> s_audioThreadId{0};
std::atomic<unsigned int> s_gxThreadId{0};
pthread_t s_writerThread;
bool s_writerThreadStarted;
bool s_contextInitialized;
bool s_namesInitialized;
bool s_writerInitialized;
FILE* s_output;
uint32_t s_mainThreadId;
unsigned long s_frameCounter;

thread_local vp_zone_scope s_tasksScope;
thread_local vp_zone_scope s_audioFillScope;
thread_local vp_zone_scope s_audioTickScope;
thread_local vp_zone_scope s_gxConsumeScope;
thread_local vp_zone_scope s_gxPublishScope;
thread_local vp_zone_scope s_taskRunScope;
thread_local vp_zone_scope s_taskTransitionScope;
thread_local vp_zone_scope s_renderPhaseScope;
thread_local vp_zone_scope s_renderViewScope;

bool envEnabled(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
}

int fileWrite(void* user, const uint8_t* data, size_t size)
{
    FILE* file = static_cast<FILE*>(user);
    return file != nullptr && std::fwrite(data, 1, size, file) == size ? 0 : -1;
}

void resetZone(vp_zone_scope& scope)
{
    std::memset(&scope, 0, sizeof(scope));
}

void beginZone(uint32_t name, vp_zone_scope& scope)
{
    resetZone(scope);
    if (!s_active.load(std::memory_order_acquire))
        return;
    (void)vp_zone_begin(&s_context, name, &scope);
}

void endZone(vp_zone_scope& scope)
{
    if (!scope.active)
        return;
    if (s_active.load(std::memory_order_acquire))
        (void)vp_zone_end(&s_context, &scope);
    scope.active = 0;
}

void drainUntilEmpty()
{
    for (;;)
    {
        size_t written = 0;
        const int result = vp_stream_writer_drain_v2(&s_writer, kDrainBatch, &written);
        if (result != VP_RESULT_OK)
        {
            s_active.store(false, std::memory_order_release);
            return;
        }
        if (written == 0)
            return;
    }
}

void* writerMain(void*)
{
    while (!s_writerStop.load(std::memory_order_acquire))
    {
        size_t written = 0;
        const int result = vp_stream_writer_drain_v2(&s_writer, kDrainBatch, &written);
        if (result != VP_RESULT_OK)
        {
            s_active.store(false, std::memory_order_release);
            return nullptr;
        }
        if (written == 0)
            sceKernelDelayThread(5000);
    }

    drainUntilEmpty();
    if (s_writer.state == VP_STREAM_WRITER_STREAMING)
        (void)vp_stream_writer_close_v2(&s_writer);
    if (s_output != nullptr)
        std::fflush(s_output);
    return nullptr;
}

bool registerName(const char* text, uint32_t* id)
{
    return vp_name_dictionary_register(&s_names, text, id) == VP_RESULT_OK;
}

bool registerTasks()
{
    s_taskNameCount = 0;
    if (!registerName("task run (unregistered)", &s_unknownTaskRunName)
        || !registerName("task transition (unregistered)", &s_unknownTaskTransitionName))
        return false;

    if (nlTaskManager::m_pInstance == nullptr || nlTaskManager::m_pInstance->m_lTaskList == nullptr)
        return true;

    nlTask* task = nlDLRingGetStart<nlTask>(nlTaskManager::m_pInstance->m_lTaskList);
    for (;;)
    {
        if (s_taskNameCount >= kTaskCapacity)
            break;

        const char* raw = task->GetName();
        if (raw == nullptr || *raw == '\0')
            raw = "?";

        char runName[128];
        char transitionName[128];
        std::snprintf(runName, sizeof(runName), "task run: %.110s", raw);
        std::snprintf(transitionName, sizeof(transitionName), "task transition: %.103s", raw);

        TaskNames& names = s_taskNames[s_taskNameCount];
        names.task = task;
        if (!registerName(runName, &names.runName)
            || !registerName(transitionName, &names.transitionName))
            return false;
        ++s_taskNameCount;

        if (nlDLRingIsEnd<nlTask>(nlTaskManager::m_pInstance->m_lTaskList, task) != 0)
            break;
        task = task->m_next;
    }
    return true;
}

bool registerRenderViews()
{
    static const char* kViewNames[34] = {
        "ShadowTexture", "GrabTexture", "Skybox", "Shadowed", "Shadow0",
        "ShadowBlend0", "WorldShadowed", "Unshadowed", "BigBlackPolygon",
        "Warble", "WarbleBlend", "Characters", "CoPlanar0", "CoPlanar",
        "Shadow1", "ShadowBlend1", "UnsortedPerspective", "DepthOfField",
        "LingeringParticles", "Particles", "InvisiblePlane", "ElectricFence",
        "CameraSpace", "ScreenBlur", "ScreenBlur2", "ScreenGrab", "FrontEnd",
        "UnsortedOrtho", "Transitions3D", "Transitions", "Anark3D_BG", "Anark",
        "Anark3D_FG", "Debug"
    };

    for (unsigned int i = 0; i < 34u; ++i)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "render view: %s", kViewNames[i]);
        if (!registerName(name, &s_renderViewNames[i]))
            return false;
    }
    return true;
}

uint32_t taskName(const void* task, bool transition)
{
    for (uint32_t i = 0; i < s_taskNameCount; ++i)
    {
        if (s_taskNames[i].task == task)
            return transition ? s_taskNames[i].transitionName : s_taskNames[i].runName;
    }
    return transition ? s_unknownTaskTransitionName : s_unknownTaskRunName;
}

void cleanup()
{
    if (s_output != nullptr)
    {
        std::fclose(s_output);
        s_output = nullptr;
    }
    if (s_namesInitialized)
    {
        vp_name_dictionary_deinit(&s_names);
        s_namesInitialized = false;
    }
    if (s_contextInitialized)
    {
        vp_deinit(&s_context);
        s_contextInitialized = false;
    }
    s_writerInitialized = false;
}

} // namespace

extern "C" int PortProfilerStart(void)
{
    if (!envEnabled("STRIKERS_VITA_PROFILE"))
        return 0;
    if (s_active.load(std::memory_order_acquire))
        return 1;

    vp_name_dictionary_config nameConfig = {};
    nameConfig.entries = s_nameEntries;
    nameConfig.entry_capacity = kNameCapacity;
    nameConfig.text = s_nameText;
    nameConfig.text_capacity = kNameTextCapacity;

    if (vp_vita_init(&s_context, s_slots, kRingCapacity) != VP_RESULT_OK)
        return 0;
    s_contextInitialized = true;

    if (vp_name_dictionary_init(&s_names, &nameConfig) != VP_RESULT_OK)
    {
        cleanup();
        return 0;
    }
    s_namesInitialized = true;

    if (!registerName("main frame", &s_frameName)
        || !registerName("main tasks", &s_tasksName)
        || !registerName("audio fill", &s_audioFillName)
        || !registerName("audio tick", &s_audioTickName)
        || !registerName("audio queued bytes", &s_audioQueuedName)
        || !registerName("audio produced buffers", &s_audioProducedName)
        || !registerName("main thread", &s_mainThreadName)
        || !registerName("audio worker", &s_audioThreadName)
        || !registerName("GX consumer", &s_gxThreadName)
        || !registerName("GX FIFO publish", &s_gxPublishName)
        || !registerName("GX FIFO consume", &s_gxConsumeName)
        || !registerName("render swap_pre", &s_renderPhaseNames[0])
        || !registerName("render send_frame", &s_renderPhaseNames[1])
        || !registerName("render send_views", &s_renderPhaseNames[2])
        || !registerName("render swap_post", &s_renderPhaseNames[3])
        || !registerName("render frame_alloc", &s_renderPhaseNames[4])
        || !registerRenderViews()
        || !registerTasks()
        || vp_name_dictionary_seal(&s_names) != VP_RESULT_OK)
    {
        cleanup();
        return 0;
    }

    const char* path = std::getenv("STRIKERS_VITA_PROFILE_PATH");
    if (path == nullptr || *path == '\0')
        path = "ux0:data/strikersVita/profile.vptrace";
    s_output = std::fopen(path, "wb");
    if (s_output == nullptr)
    {
        cleanup();
        return 0;
    }

    vp_stream_writer_config writerConfig = {};
    writerConfig.context = &s_context;
    writerConfig.names = &s_names;
    writerConfig.write = fileWrite;
    writerConfig.write_user = s_output;
    writerConfig.dictionary_buffer = s_streamBuffer;
    writerConfig.dictionary_buffer_capacity = sizeof(s_streamBuffer);
    if (vp_stream_writer_init_v2(&s_writer, &writerConfig) != VP_RESULT_OK)
    {
        cleanup();
        return 0;
    }
    s_writerInitialized = true;

    const uint64_t now = (uint64_t)sceKernelGetProcessTimeWide();
    s_mainThreadId = (uint32_t)sceKernelGetThreadId();
    vp_stream_v2_session session = {};
    session.session_id = now != 0 ? now : 1;
    session.flags = VP_STREAM_V2_SESSION_TIMER_SOURCE | VP_STREAM_V2_SESSION_TIMER_UNIT;
    session.timer_source = "sceKernelGetProcessTimeWide";
    session.timer_unit = "microseconds";
    if (vp_stream_writer_begin_v2(&s_writer, now, &session) != VP_RESULT_OK)
    {
        cleanup();
        return 0;
    }

    vp_stream_v2_thread mainThread = {};
    mainThread.thread_id = s_mainThreadId;
    mainThread.generation = 1;
    mainThread.name_id = s_mainThreadName;
    if (vp_stream_writer_write_thread_v2(&s_writer, &mainThread) != VP_RESULT_OK)
    {
        (void)vp_stream_writer_close_v2(&s_writer);
        cleanup();
        return 0;
    }

    const unsigned int audioThreadId = s_audioThreadId.load(std::memory_order_acquire);
    if (audioThreadId != 0)
    {
        vp_stream_v2_thread audioThread = {};
        audioThread.thread_id = audioThreadId;
        audioThread.generation = 1;
        audioThread.name_id = s_audioThreadName;
        if (vp_stream_writer_write_thread_v2(&s_writer, &audioThread) != VP_RESULT_OK)
        {
            (void)vp_stream_writer_close_v2(&s_writer);
            cleanup();
            return 0;
        }
    }

    const unsigned int gxThreadId = s_gxThreadId.load(std::memory_order_acquire);
    if (gxThreadId != 0)
    {
        vp_stream_v2_thread gxThread = {};
        gxThread.thread_id = gxThreadId;
        gxThread.generation = 1;
        gxThread.name_id = s_gxThreadName;
        if (vp_stream_writer_write_thread_v2(&s_writer, &gxThread) != VP_RESULT_OK)
        {
            (void)vp_stream_writer_close_v2(&s_writer);
            cleanup();
            return 0;
        }
    }

    s_frameCounter = 0;
    s_writerStop.store(false, std::memory_order_release);
    s_active.store(true, std::memory_order_release);
    if (pthread_create(&s_writerThread, nullptr, writerMain, nullptr) != 0)
    {
        s_active.store(false, std::memory_order_release);
        drainUntilEmpty();
        if (s_writer.state == VP_STREAM_WRITER_STREAMING)
            (void)vp_stream_writer_close_v2(&s_writer);
        cleanup();
        return 0;
    }
    s_writerThreadStarted = true;
    return 1;
}

extern "C" void PortProfilerStop(void)
{
    if (!s_contextInitialized)
        return;

    s_active.store(false, std::memory_order_release);
    if (s_writerThreadStarted)
    {
        s_writerStop.store(true, std::memory_order_release);
        pthread_join(s_writerThread, nullptr);
        s_writerThreadStarted = false;
    }
    else if (s_writerInitialized && s_writer.state == VP_STREAM_WRITER_STREAMING)
    {
        drainUntilEmpty();
        if (s_writer.state == VP_STREAM_WRITER_STREAMING)
            (void)vp_stream_writer_close_v2(&s_writer);
    }
    cleanup();
}

extern "C" void PortProfilerFrameMark(void)
{
    if (!s_active.load(std::memory_order_acquire))
        return;

    (void)vp_frame_mark(&s_context, s_frameName);
    ++s_frameCounter;

    if ((s_frameCounter % 30u) == 0u)
    {
        (void)vp_vita_record_thread(&s_context, s_mainThreadId, nullptr);
        const unsigned int audioThread = s_audioThreadId.load(std::memory_order_acquire);
        if (audioThread != 0)
            (void)vp_vita_record_thread(&s_context, audioThread, nullptr);
        const unsigned int gxThread = s_gxThreadId.load(std::memory_order_acquire);
        if (gxThread != 0)
            (void)vp_vita_record_thread(&s_context, gxThread, nullptr);
    }
    if ((s_frameCounter % 60u) == 0u)
        (void)vp_vita_record_memory(&s_context, nullptr);
}

extern "C" void PortProfilerTasksBegin(void)
{
    beginZone(s_tasksName, s_tasksScope);
}

extern "C" void PortProfilerTasksEnd(void)
{
    endZone(s_tasksScope);
}

extern "C" void PortProfilerTaskRunBegin(const void* task)
{
    beginZone(taskName(task, false), s_taskRunScope);
}

extern "C" void PortProfilerTaskRunEnd(void)
{
    endZone(s_taskRunScope);
}

extern "C" void PortProfilerTaskTransitionBegin(const void* task)
{
    beginZone(taskName(task, true), s_taskTransitionScope);
}

extern "C" void PortProfilerTaskTransitionEnd(void)
{
    endZone(s_taskTransitionScope);
}

extern "C" void PortProfilerRenderPhaseBegin(unsigned int phase)
{
    if (phase >= 5u)
        return;
    beginZone(s_renderPhaseNames[phase], s_renderPhaseScope);
}

extern "C" void PortProfilerRenderPhaseEnd(void)
{
    endZone(s_renderPhaseScope);
}

extern "C" void PortProfilerRenderViewBegin(unsigned int view)
{
    if (view >= 34u)
        return;
    beginZone(s_renderViewNames[view], s_renderViewScope);
}

extern "C" void PortProfilerRenderViewEnd(void)
{
    endZone(s_renderViewScope);
}

extern "C" void PortProfilerAudioThreadStarted(unsigned int threadId)
{
    s_audioThreadId.store(threadId, std::memory_order_release);
}

extern "C" void PortProfilerGxThreadStarted(unsigned int threadId)
{
    s_gxThreadId.store(threadId, std::memory_order_release);
}

extern "C" void PortProfilerGxPublishBegin(void)
{
    beginZone(s_gxPublishName, s_gxPublishScope);
}

extern "C" void PortProfilerGxPublishEnd(void)
{
    endZone(s_gxPublishScope);
}

extern "C" void PortProfilerGxConsumeBegin(void)
{
    beginZone(s_gxConsumeName, s_gxConsumeScope);
}

extern "C" void PortProfilerGxConsumeEnd(void)
{
    endZone(s_gxConsumeScope);
}

extern "C" void PortProfilerAudioFillBegin(void)
{
    beginZone(s_audioFillName, s_audioFillScope);
}

extern "C" void PortProfilerAudioFillEnd(void)
{
    endZone(s_audioFillScope);
}

extern "C" void PortProfilerAudioTickBegin(void)
{
    beginZone(s_audioTickName, s_audioTickScope);
}

extern "C" void PortProfilerAudioTickEnd(void)
{
    endZone(s_audioTickScope);
}

extern "C" void PortProfilerAudioQueue(int queuedBytes, int producedBuffers)
{
    if (!s_active.load(std::memory_order_acquire))
        return;
    (void)vp_counter(&s_context, s_audioQueuedName, queuedBytes);
    (void)vp_counter(&s_context, s_audioProducedName, producedBuffers);
}

#else

extern "C" int PortProfilerStart(void) { return 0; }
extern "C" void PortProfilerStop(void) {}
extern "C" void PortProfilerFrameMark(void) {}
extern "C" void PortProfilerTasksBegin(void) {}
extern "C" void PortProfilerTasksEnd(void) {}
extern "C" void PortProfilerTaskRunBegin(const void*) {}
extern "C" void PortProfilerTaskRunEnd(void) {}
extern "C" void PortProfilerTaskTransitionBegin(const void*) {}
extern "C" void PortProfilerTaskTransitionEnd(void) {}
extern "C" void PortProfilerRenderPhaseBegin(unsigned int) {}
extern "C" void PortProfilerRenderPhaseEnd(void) {}
extern "C" void PortProfilerRenderViewBegin(unsigned int) {}
extern "C" void PortProfilerRenderViewEnd(void) {}
extern "C" void PortProfilerAudioThreadStarted(unsigned int) {}
extern "C" void PortProfilerGxThreadStarted(unsigned int) {}
extern "C" void PortProfilerGxPublishBegin(void) {}
extern "C" void PortProfilerGxPublishEnd(void) {}
extern "C" void PortProfilerGxConsumeBegin(void) {}
extern "C" void PortProfilerGxConsumeEnd(void) {}
extern "C" void PortProfilerAudioFillBegin(void) {}
extern "C" void PortProfilerAudioFillEnd(void) {}
extern "C" void PortProfilerAudioTickBegin(void) {}
extern "C" void PortProfilerAudioTickEnd(void) {}
extern "C" void PortProfilerAudioQueue(int, int) {}

#endif
