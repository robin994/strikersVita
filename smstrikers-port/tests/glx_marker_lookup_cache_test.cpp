#include "port/glx_marker_lookup_cache.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

// Model only the handle/marker lifetime contract. The actual GLX registry
// remains single-thread owned; a cache does not make it async or thread-safe.
struct Texture
{
    int identity = 0;
};
struct Registry
{
    port::glx::MarkerLookupCache<Texture*> cache;
    std::array<std::unordered_map<uintptr_t, Texture*>, 16> levels;
    std::unordered_map<uintptr_t, uintptr_t> animation;
    int active = 0;
    mutable unsigned scans = 0;
    bool cacheEnabled = true;

    void add(uintptr_t key, Texture* value)
    {
        levels[active][key] = value;
        cache.invalidate();
    }
    void advance()
    {
        assert(active < 15);
        cache.invalidate();
        ++active;
    }
    void backup(int level)
    {
        assert(level >= 0 && level <= active);
        cache.invalidate();
        for (int i = active; i > level; --i)
            levels[i].clear();
        active = level;
    }
    void replace(uintptr_t key, Texture* value)
    {
        for (int i = active; i >= 0; --i)
        {
            if (levels[i].count(key))
            {
                levels[i][key] = value;
                cache.invalidate();
                return;
            }
        }
    }
    void reset()
    {
        cache.invalidate();
        for (auto& level : levels)
            level.clear();
        active = 0;
    }

    Texture* lookup(uintptr_t key, Texture* direct = nullptr, bool grid = false,
                    bool missingFatal = false)
    {
        // Exact GLX contract: direct textures bypass the marker trees;
        // animation re-resolves the current target before every lookup.
        if (direct)
            return direct;
        const auto animated = animation.find(key);
        const bool hasAnimation = animated != animation.end();
        if (hasAnimation)
            key = animated->second;
        const bool useCache = cacheEnabled && !hasAnimation && !grid && !missingFatal;
        Texture* value = nullptr;
        if (useCache && cache.find(key, value))
            return value;
        for (int i = active; i >= 0; --i)
        {
            ++scans;
            if (const auto it = levels[i].find(key); it != levels[i].end())
            {
                if (useCache)
                    cache.remember(key, it->second);
                return it->second;
            }
        }
        return nullptr;
    }
};

int main()
{
    Texture old{1}, newer{2}, animatedA{3}, animatedB{4}, reused{5};
    Registry original, fast;
    original.cacheEnabled = false;

    auto repeat = [&](uintptr_t key, Texture* expected, Texture* direct = nullptr,
                      bool grid = false) {
        assert(original.lookup(key, direct, grid) == expected);
        assert(fast.lookup(key, direct, grid) == expected);
    };
    auto both = [&](auto op) {
        op(original);
        op(fast);
    };

    both([&](Registry& registry) { registry.add(0x100, &old); });
    for (unsigned i = 0; i < 40; ++i)
        repeat(0x100, &old);
    assert(fast.scans < original.scans);

    // A new marker may shadow a cached lower-level registration.
    both([](Registry& r) { r.advance(); });
    repeat(0x100, &old);
    both([&](Registry& r) { r.add(0x100, &newer); });
    repeat(0x100, &newer);
    both([](Registry& r) { r.backup(0); });
    repeat(0x100, &old);
    both([&](Registry& r) { r.replace(0x100, &reused); });
    repeat(0x100, &reused);

    // Animation changes are dynamic; never retain an animation's chosen
    // frame/underlying handle across calls.
    both([&](Registry& r) {
        r.add(0x200, &animatedA);
        r.add(0x300, &animatedB);
        r.animation[0x400] = 0x200;
    });
    repeat(0x400, &animatedA);
    both([](Registry& r) { r.animation[0x400] = 0x300; });
    repeat(0x400, &animatedB);
    for (unsigned i = 0; i < 20; ++i)
        repeat(0x400, &animatedB);
    repeat(0x400, &newer, &newer); // tagged PTEX direct path bypasses AVL.

    // A grid request and a missing-fatal request must not reuse shortcuts.
    repeat(0x100, &reused, nullptr, true);
    assert(original.lookup(0x999, nullptr, false, true) == nullptr);
    assert(fast.lookup(0x999, nullptr, false, true) == nullptr);

    // Clear-on-release invalidates a pointer even if its address is reused.
    both([](Registry& r) { r.reset(); });
    repeat(0x100, nullptr);
    both([&](Registry& r) { r.add(0x100, &reused); });
    repeat(0x100, &reused);

    // Exercise collision replacement and generations at marker 0..15.
    std::array<Texture, 512> values{};
    both([](Registry& r) {
        for (int i = 0; i != 15; ++i)
            r.advance();
    });
    for (unsigned i = 0; i < values.size(); ++i)
    {
        values[i].identity = int(i + 10);
        both([&](Registry& r) { r.add(0x10000 + uintptr_t(i) * 16, &values[i]); });
    }
    for (unsigned trial = 0; trial < 4; ++trial)
        for (unsigned i = 0; i < values.size(); ++i)
            repeat(0x10000 + uintptr_t(i) * 16, &values[i]);
    both([](Registry& r) { r.backup(0); });
    for (unsigned i = 0; i < values.size(); ++i)
        repeat(0x10000 + uintptr_t(i) * 16, nullptr);

    // 256 slots are bounded; repeated hot lookups avoid the 7-level scan.
    both([](Registry& r) { r.reset(); });
    for (unsigned i = 0; i < 32; ++i)
        both([&](Registry& r) { r.add(0x8000 + i, &values[i]); });
    // The game searches newest marker to oldest. Stable stadium handles
    // commonly live in level zero while six transient levels are active.
    both([](Registry& r) {
        for (int i = 0; i != 6; ++i)
            r.advance();
    });
    const auto originalBefore = original.scans;
    const auto cachedBefore = fast.scans;
    for (unsigned j = 0; j < 1000; ++j)
        for (unsigned i = 0; i < 32; ++i)
            repeat(0x8000 + i, &values[i]);
    assert(original.scans > originalBefore + 200000);
    assert(fast.scans < cachedBefore + 7000);
    static_assert(port::glx::MarkerLookupCache<Texture*>::Capacity == 256);
    std::puts("GLX marker cache: shadowing, generation, rollback, replacements, "
              "animations, pointer reuse, invalidations and 32k repeated hits PASS");
}
