// MemoryAllocator over an address range the port owns, so a pointer is identified by comparison.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#include "NL/MemAlloc.h"
#include "port/host.h"

#if defined(STRIKERS_VITA)
#include <malloc.h>
#include <psp2/kernel/sysmem.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace
{

// Address space, committed by the OS on first touch; nothing here is returned to the OS.
#if defined(STRIKERS_VITA)
const std::size_t kRegionSize = 112u * 1024u * 1024u;
#else
const std::size_t kRegionSize = 768u * 1024u * 1024u;
#endif

// At least the largest alignment any caller asks for, so the payload never runs over the header.
const std::size_t kHeaderSize = 32;

struct BlockHeader
{
    std::size_t size;     // payload bytes, as the caller asked for them
    std::size_t offset;   // payload - block start, so Free can recover it
    void* owner;          // which allocator's accounting this belongs to
    std::size_t pad;
};

char* s_region;
char* s_bump;
char* s_end;
#if defined(STRIKERS_VITA)
SceUID s_region_memblock = -1;
#endif

bool region_init()
{
    if (s_region != nullptr)
        return true;
#if defined(STRIKERS_VITA)
    // Keep the game's large backing slab out of newlib's 128 MiB heap.  The old
    // malloc path consumed almost the whole heap before nlInitMemory tried to
    // create its arena, which made that second allocation fail at boot.
    s_region_memblock = sceKernelAllocMemBlock(
        "strikersGameRegion", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
        (SceSize)kRegionSize, nullptr);
    void* p = nullptr;
    if (s_region_memblock >= 0
        && sceKernelGetMemBlockBase(s_region_memblock, &p) < 0)
    {
        sceKernelFreeMemBlock(s_region_memblock);
        s_region_memblock = -1;
        p = nullptr;
    }
#elif defined(_WIN32)
    void* p = VirtualAlloc(nullptr, kRegionSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void* p =
        mmap(nullptr, kRegionSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        p = nullptr;
#endif
    if (p == nullptr)
        return false;
    s_region = (char*)p;
    s_bump = s_region;
    s_end = s_region + kRegionSize;
    return true;
}

inline bool region_owns(const void* p)
{
    // Never dereferences p, so it is safe for a foreign pointer.
    return s_region != nullptr && (const char*)p >= s_region && (const char*)p < s_end;
}

// Per-instance accounting; the class layout is fixed, so the unused m_free_block_list points here.
struct AllocState
{
    std::size_t live;
    std::size_t pool;
    char* begin;
    char* bump;
    char* end;
    // Size class i holds blocks of 2^(i+5) bytes.
    void* freeList[32];
};

AllocState* state_for(FreeBlockList** slot)
{
    if (*slot == nullptr)
        *slot = (FreeBlockList*)std::calloc(1, sizeof(AllocState));
    return (AllocState*)*slot;
}

inline int size_class(std::size_t n)
{
    int c = 0;
    std::size_t s = 32;
    while (s < n && c < 31)
    {
        s <<= 1;
        c++;
    }
    return c;
}

inline std::size_t class_size(int c) { return (std::size_t)32 << c; }

}   // namespace

void MemoryAllocator::Initialize(void* memory, unsigned int size)
{
    m_free_block_list = nullptr;
    AllocState* st = state_for(&m_free_block_list);
    st->pool = size;
    st->live = 0;
#if defined(STRIKERS_VITA)
    st->begin = (char*)memory;
    st->bump = st->begin;
    st->end = st->begin ? st->begin + size : nullptr;
#else
    (void)memory;
    st->begin = nullptr;
    st->bump = nullptr;
    st->end = nullptr;
#endif
}

void* MemoryAllocator::Allocate(unsigned long size, unsigned int alignment, bool fromEnd)
{
    // fromEnd placed long-lived blocks at the top of the console's arena.
    (void)fromEnd;
    if (alignment < alignof(std::max_align_t))
        alignment = alignof(std::max_align_t);
    if (alignment > kHeaderSize)
        alignment = kHeaderSize;   // the header already reserves the largest

    AllocState* st = state_for(&m_free_block_list);
    const std::size_t want = kHeaderSize + (size ? (std::size_t)size : 1);
    const int cls = size_class(want);

    char* block;
    if (st->freeList[cls] != nullptr)
    {
        block = (char*)st->freeList[cls];
        st->freeList[cls] = *(void**)block;
    }
    else
    {
        const std::size_t take = class_size(cls);
#if defined(STRIKERS_VITA)
        if (st->bump == nullptr || st->end == nullptr || st->bump + take > st->end)
            return nullptr;
        block = st->bump;
        st->bump += take;
#else
        if (!region_init() || s_bump + take > s_end)
            return nullptr;
        block = s_bump;
        s_bump += take;
#endif
    }

    std::uintptr_t raw = (std::uintptr_t)block + kHeaderSize;
    std::uintptr_t aligned = (raw + alignment - 1) & ~(std::uintptr_t)(alignment - 1);
    BlockHeader* h = (BlockHeader*)(aligned - sizeof(BlockHeader));
    h->size = (std::size_t)size;
    h->offset = (std::size_t)(aligned - (std::uintptr_t)block);
    h->owner = st;
    st->live += (std::size_t)size;
    return (void*)aligned;
}

void MemoryAllocator::Free(void* p)
{
    if (p == nullptr)
        return;

    if (!region_owns(p))
    {
        // A foreign allocation: no header probe, since that read would be out of bounds.
        std::free(p);
        return;
    }

    BlockHeader* h = (BlockHeader*)((char*)p - sizeof(BlockHeader));
    char* block = (char*)p - h->offset;

    // Charge the block to whoever allocated it: nlFree picks its instance by testing bit 31.
    AllocState* st = h->owner ? (AllocState*)h->owner : state_for(&m_free_block_list);
    st->live -= h->size < st->live ? h->size : st->live;

    const int cls = size_class(h->offset + h->size);
    *(void**)block = st->freeList[cls];
    st->freeList[cls] = block;
}

unsigned int MemoryAllocator::TotalFreeMemory()
{
    AllocState* st = state_for(&m_free_block_list);
    return (unsigned int)(st->pool > st->live ? st->pool - st->live : 0);
}

unsigned int MemoryAllocator::LargestFreeBlock()
{
    // No fragmentation to report; the region satisfies anything up to the budget.
    return TotalFreeMemory();
}

// A carve-out for the VM window in vm.c; from the region, or pointers inside it test as foreign.
extern "C" void* port_region_reserve(std::size_t size, std::size_t align)
{
    if (!region_init())
        return nullptr;
    if (align < alignof(std::max_align_t))
        align = alignof(std::max_align_t);
    std::uintptr_t aligned = ((std::uintptr_t)s_bump + align - 1) & ~(std::uintptr_t)(align - 1);
    if ((char*)(aligned + size) > s_end)
        return nullptr;
    s_bump = (char*)(aligned + size);
    return (void*)aligned;
}

// Replaces the 0x8xxxxxxx tests that told a console pointer from a small integer id.
extern "C" int port_region_owns(const void* p) { return region_owns(p) ? 1 : 0; }

extern "C" void port_region_stats(std::size_t* used, std::size_t* total)
{
    if (used)
        *used = (s_region != nullptr) ? (std::size_t)(s_bump - s_region) : 0;
    if (total)
        *total = (s_region != nullptr) ? (std::size_t)(s_end - s_region) : 0;
}

