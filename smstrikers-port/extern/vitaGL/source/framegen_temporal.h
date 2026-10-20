/*
 * Experimental Vita scanout midpoint interpolation.
 *
 * Pure, host-testable functions: no GXM, GPU readback, GL or allocations.
 * The output must not alias either source and the caller must own completed
 * scanout pixels for the duration of this call.
 */
#ifndef VGL_TEMPORAL_FRAMEGEN_H
#define VGL_TEMPORAL_FRAMEGEN_H

#include <stddef.h>
#include <stdint.h>

#if defined(__ARM_NEON) && !defined(VGL_TEMPORAL_FORCE_SCALAR)
#include <arm_neon.h>
#endif

/* Neither zero-length nor invalid stride/dimensions can reach scanout. */
static inline int vgl_temporal_frame_bytes(unsigned width, unsigned height,
                                            unsigned stride, size_t *out)
{
    if (!out || !width || !height || stride < width ||
        width > 4096 || height > 4096 || stride > 4096 ||
        (size_t)stride > SIZE_MAX / 4u / (size_t)height)
        return 0;
    *out = (size_t)stride * (size_t)height * 4u;
    return 1;
}

/* Two consecutive 60 Hz scanouts need 33.334 ms, plus some CPU headroom.
 * Generating a midpoint for a 25-33 ms game frame would cap real FPS at 30.
 */
#define VGL_TEMPORAL_TWO_VBLANK_US 33334u
#define VGL_TEMPORAL_GUARD_US 1500u
#define VGL_TEMPORAL_CPU_BUDGET_US 8000u

static inline int vgl_temporal_should_insert(uint64_t real_frame_delta_us,
                                              int previous_valid,
                                              int normal_60hz_vsync,
                                              int game_frame_eligible)
{
    return previous_valid && normal_60hz_vsync && game_frame_eligible &&
           real_frame_delta_us >= 35000u && real_frame_delta_us <= 50000u;
}

/* Measured CPU work includes previous-frame snapshot copy as well as blending.
 * Check the dynamic slack without unsigned overflow.
 */
static inline int vgl_temporal_budget_ok(uint64_t real_frame_delta_us,
                                         uint64_t cpu_work_us)
{
    return cpu_work_us <= VGL_TEMPORAL_CPU_BUDGET_US &&
           real_frame_delta_us >= VGL_TEMPORAL_TWO_VBLANK_US + VGL_TEMPORAL_GUARD_US &&
           cpu_work_us <= real_frame_delta_us -
               (VGL_TEMPORAL_TWO_VBLANK_US + VGL_TEMPORAL_GUARD_US);
}

/* Reject near-global hard cuts/black flashes; never blend across them. */
static inline int vgl_temporal_scene_cut(const uint8_t *previous,
                                         const uint8_t *current,
                                         size_t bytes)
{
    const size_t pixels = bytes / 4u;
    const size_t samples = pixels < 64u ? pixels : 64u;
    unsigned long changed = 0;
    if (!samples)
        return 0;
    for (size_t i = 0; i < samples; ++i) {
        const size_t offset = (i * pixels / samples) * 4u;
        for (unsigned channel = 0; channel < 3u; ++channel) {
            const int difference = (int)previous[offset + channel] -
                                   (int)current[offset + channel];
            changed += (unsigned)(difference < 0 ? -difference : difference);
        }
    }
    return changed > 96u * 3u * samples;
}

/* A true intermediate image (not a duplicated frame, not optical flow). */
static inline void vgl_temporal_blend_midpoint(uint8_t *dst,
                                                const uint8_t *previous,
                                                const uint8_t *current,
                                                size_t bytes)
{
    size_t i = 0;
#if defined(__ARM_NEON) && !defined(VGL_TEMPORAL_FORCE_SCALAR)
    for (; bytes - i >= 64u; i += 64u) {
        vst1q_u8(dst + i,      vrhaddq_u8(vld1q_u8(previous + i),      vld1q_u8(current + i)));
        vst1q_u8(dst + i + 16, vrhaddq_u8(vld1q_u8(previous + i + 16), vld1q_u8(current + i + 16)));
        vst1q_u8(dst + i + 32, vrhaddq_u8(vld1q_u8(previous + i + 32), vld1q_u8(current + i + 32)));
        vst1q_u8(dst + i + 48, vrhaddq_u8(vld1q_u8(previous + i + 48), vld1q_u8(current + i + 48)));
    }
    for (; bytes - i >= 16u; i += 16u)
        vst1q_u8(dst + i, vrhaddq_u8(vld1q_u8(previous + i), vld1q_u8(current + i)));
#endif
    for (; i < bytes; ++i)
        dst[i] = (uint8_t)(((unsigned)previous[i] + (unsigned)current[i] + 1u) >> 1);
}

/* Fuse interpolation with the next-frame snapshot: only one read of current.
 * previous is private CPU storage; dst is independent mapped scanout memory.
 * Neither destination may alias current or each other.
 */
static inline void vgl_temporal_blend_and_snapshot(uint8_t *dst,
                                                   uint8_t *previous,
                                                   const uint8_t *current,
                                                   size_t bytes)
{
    size_t i = 0;
#if defined(__ARM_NEON) && !defined(VGL_TEMPORAL_FORCE_SCALAR)
    for (; bytes - i >= 16u; i += 16u) {
        const uint8x16_t prev = vld1q_u8(previous + i);
        const uint8x16_t curr = vld1q_u8(current + i);
        vst1q_u8(dst + i, vrhaddq_u8(prev, curr));
        vst1q_u8(previous + i, curr);
    }
#endif
    for (; i < bytes; ++i) {
        const unsigned prev = previous[i];
        const unsigned curr = current[i];
        dst[i] = (uint8_t)((prev + curr + 1u) >> 1);
        previous[i] = (uint8_t)curr;
    }
}

#endif /* VGL_TEMPORAL_FRAMEGEN_H */
