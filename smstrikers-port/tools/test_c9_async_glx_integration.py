#!/usr/bin/env python3
"""Source contract guard for the opt-in, CPU-only C9 async + GLX fast path.

Complements the C++ marker-cache parity test and real pthread/ASan tests.
The GLX texture registry is single-owner: never call it from async jobs.
"""
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
GLX=(ROOT/"src/NL/glx/glxTexture.cpp").read_text()
SNAPSHOT=(ROOT/"src/Game/RenderSnapshot.cpp").read_text()
JOBS=(ROOT/"src/platform/native_jobs.cpp").read_text()
HEADER=(ROOT/"include/port/glx_marker_lookup_cache.hpp").read_text()

def body(source: str, signature: str) -> str:
    match=re.search(signature+r"\s*\{",source)
    if not match:
        raise AssertionError("Missing C9 method: "+signature)
    start=match.end()
    depth=1
    for index in range(start,len(source)):
        if source[index]=="{":
            depth+=1
        elif source[index]=="}":
            depth-=1
            if depth==0:
                return source[start:index]
    raise AssertionError("Unbalanced C9 source: "+signature)

def main():
    assert '#include "port/glx_marker_lookup_cache.hpp"' in GLX
    assert 'STRIKERS_VITA_GLX_CACHE' in GLX
    assert "setting && setting[0] == '1' && setting[1] == '\\0'" in GLX
    assert 'const bool cacheable = markerCacheEnabled() && !pAnim && !bAllowGrids && !bMissingFatal;' in GLX
    lookup=body(GLX,r"PlatTexture\* glx_GetTex\([^)]*\)")
    assert lookup.index("if (directPointer)") < lookup.index("markerLookup.find")
    assert lookup.index("glInventory.GetTextureAnim") < lookup.index("markerLookup.find")
    assert lookup.index("markerLookup.find") < lookup.index("for (index = currentMarkerLevel")
    assert 'markerLookup.remember(handle, *tex)' in lookup

    invalidations=(
        r"glxTextureLoadCallback_t glx_SetLoadCallback\([^)]*\)",
        r"void glx_AdvanceTexMarkerLevel\(\)",
        r"void glx_BackupTexMarkerLevel\(int level\)",
        r"void glxInitTex\(\)",
        r"bool glx_SetGridMode\(bool bGrid\)",
        r"bool glx_AddTex\([^)]*\)",
        r"void glplatTextureAdd\([^)]*\)",
        r"void glplatTextureReplace\([^)]*\)",
    )
    for signature in invalidations:
        assert "invalidateMarkerLookup()" in body(GLX,signature),signature

    assert "static constexpr size_t Capacity" in HEADER
    assert "MarkerLookupCache" in HEADER
    assert "std::array<Entry, Sets * 2>" in HEADER
    assert "generation() const noexcept" in HEADER
    assert "STRIKERS_VITA_ASYNC_SNAPSHOT" in SNAPSHOT
    assert "value != nullptr && value[0] == '1' && value[1] == '\\0'" in SNAPSHOT
    grab=body(SNAPSHOT,r"void RenderSnapshot::Grab\(\)")
    blend=body(SNAPSHOT,r"void RenderSnapshot::Blend\([^)]*\)")
    assert "begin_async(pendingGrab, 10, 2, GrabCharacters, &grabJob)" in grab
    assert "finish_async(pendingGrab)" in grab
    assert grab.index("mPowerups[i].Grab") < grab.index("finish_async(pendingGrab)")
    assert grab.index("finish_async(pendingGrab)") < grab.index("mChainChomp.Grab")
    assert "begin_async(pendingBlend, 10, 2, BlendCharacters, &characterJob)" in blend
    assert "finish_async(pendingBlend)" in blend
    assert blend.index("BlendSnapshotObjects(&objectJob, 0, 170, 0)") < blend.index("finish_async(pendingBlend)")
    assert "glx_" not in body(JOBS,r"bool begin_async\([^)]*\)\s*noexcept")
    assert "asyncOutstanding" in JOBS
    assert "while (!completed)" in body(JOBS,r"bool finish_async\([^)]*\)\s*noexcept")
    assert 'SCE_KERNEL_CPU_MASK_USER_0' in JOBS
    print("PASS: GLX mutation invalidation, bypass, C9 OFF defaults, async barriers and GPU ownership contracts")

if __name__=="__main__":
    main()
