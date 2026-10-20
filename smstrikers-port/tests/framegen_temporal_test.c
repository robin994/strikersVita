/* Standalone host checks for the scanout-only C9 interpolation kernel. */
#include "../extern/vitaGL/source/framegen_temporal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned rng = 0x53545249u;
static uint8_t random_byte(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (uint8_t)(rng >> 24);
}

static void test_lengths(void)
{
    size_t size = 0;
    assert(!vgl_temporal_frame_bytes(0, 544, 960, &size));
    assert(!vgl_temporal_frame_bytes(960, 0, 960, &size));
    assert(!vgl_temporal_frame_bytes(960, 544, 959, &size));
    assert(!vgl_temporal_frame_bytes(960, 544, 4097, &size));
    assert(!vgl_temporal_frame_bytes(960, 544, 960, NULL));
    assert(vgl_temporal_frame_bytes(960, 544, 960, &size));
    assert(size == 960u * 544u * 4u);
    assert(vgl_temporal_frame_bytes(963, 7, 1024, &size));
    assert(size == 1024u * 7u * 4u);
}

static void test_schedule(void)
{
    assert(!vgl_temporal_should_insert(37000, 0, 1, 1));
    assert(!vgl_temporal_should_insert(37000, 1, 0, 1));
    assert(!vgl_temporal_should_insert(37000, 1, 1, 0));
    assert(!vgl_temporal_should_insert(16667, 1, 1, 1));
    assert(!vgl_temporal_should_insert(22000, 1, 1, 1));
    assert(vgl_temporal_should_insert(25000, 1, 1, 1));
    assert(vgl_temporal_should_insert(33333, 1, 1, 1));
    assert(vgl_temporal_should_insert(37000, 1, 1, 1));
    assert(vgl_temporal_should_insert(50000, 1, 1, 1));
    assert(!vgl_temporal_should_insert(50001, 1, 1, 1));
    assert(!vgl_temporal_should_insert(100000, 1, 1, 1));
    assert(!vgl_temporal_should_insert(UINT64_MAX, 1, 1, 1));
}

static void test_scene_cut(void)
{
    uint8_t before[1024], after[1024];
    memset(before, 0, sizeof before);
    memset(after, 0, sizeof after);
    assert(!vgl_temporal_scene_cut(before, after, sizeof before));
    memset(after, 255, sizeof after);
    assert(vgl_temporal_scene_cut(before, after, sizeof before));
    memset(after, 0, sizeof after);
    // Alpha-only changes do not constitute a visual scene cut.
    for (size_t i = 3; i < sizeof after; i += 4)
        after[i] = 255;
    assert(!vgl_temporal_scene_cut(before, after, sizeof before));
    // A small moving object must not reset all temporal history.
    memset(after, 0, sizeof after);
    memset(after + 128, 255, 24);
    assert(!vgl_temporal_scene_cut(before, after, sizeof before));
    assert(!vgl_temporal_scene_cut(before, after, 0));
}

static void test_pixels(size_t n, size_t offset)
{
    const size_t allocated = n + 32u;
    uint8_t *a = malloc(allocated);
    uint8_t *b = malloc(allocated);
    uint8_t *out = malloc(allocated);
    assert(a && b && out);
    memset(a, 0xAA, allocated);
    memset(b, 0xBB, allocated);
    memset(out, 0xDD, allocated);
    for (size_t i = 0; i < n; ++i) {
        a[i + offset] = random_byte();
        b[i + offset] = random_byte();
    }
    if (n >= 4) {
        a[offset] = 0; b[offset] = 255;
        a[offset + 1] = 255; b[offset + 1] = 0;
        a[offset + 2] = 255; b[offset + 2] = 255;
        a[offset + 3] = 0; b[offset + 3] = 0;
    }
    vgl_temporal_blend_midpoint(out + offset, a + offset, b + offset, n);
    for (size_t i = 0; i < n; ++i) {
        unsigned avg = (unsigned)a[i + offset] + (unsigned)b[i + offset] + 1u;
        assert(out[i + offset] == (uint8_t)(avg >> 1));
    }
    for (size_t i = 0; i < offset; ++i)
        assert(out[i] == 0xDD);
    for (size_t i = n + offset; i < allocated; ++i)
        assert(out[i] == 0xDD);
    free(a);
    free(b);
    free(out);
}

int main(void)
{
    test_lengths();
    test_schedule();
    test_scene_cut();
    for (size_t offset = 0; offset < 8; ++offset) {
        for (size_t n = 0; n < 1024; ++n)
            test_pixels(n, offset);
        test_pixels(960u * 544u * 4u, offset);
    }
    puts("framegen_temporal: PASS (edge lengths, random midpoint, schedule, RGBA, guard bytes)");
    return 0;
}
