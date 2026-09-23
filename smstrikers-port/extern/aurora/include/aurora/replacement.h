#ifndef AURORA_REPLACEMENT_H
#define AURORA_REPLACEMENT_H

/* smstrikers-port: the replacement folders and texture dumps of <aurora/texture.hpp>, for C callers. */

#include <dolphin/gx/GXStruct.h>

#ifdef __cplusplus
#include <cstdint>

extern "C" {
#else
#include <stdint.h>
#endif

/* Registers every replacement file under `root`, an existing UTF-8 path, one pack if `one_pack`; returns how many. */
uint32_t aurora_replacement_load(const char* root, int32_t priority, int one_pack);
void aurora_replacement_clear(void);
void aurora_replacement_set_cache_budget(uint64_t bytes);
/* aurora::texture::dump_texture: 1 written, 0 already there, -1 failed. */
int aurora_replacement_dump(const GXTexObj* obj, const GXTlutObj* tlut, const char* dir);
/* Starts loading the replacement for a texture the game has just made; `tlut` may be NULL unless it has a palette. */
void aurora_replacement_prefetch(const GXTexObj* obj, const GXTlutObj* tlut);
/* 1: a replacement still loading at its texture's first draw is waited for, not drawn as the original. */
void aurora_replacement_set_wait(int wait);
/* aurora::texture::set_replacement_strict_budget; call before aurora_replacement_load. */
void aurora_replacement_set_strict_budget(int strict);

#ifdef __cplusplus
}
#endif

#endif
