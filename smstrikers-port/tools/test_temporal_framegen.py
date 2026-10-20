#!/usr/bin/env python3
"""Host-only C9 framegen tests; NEVER builds a Vita SELF or runs on device.

Runs the deterministic midpoint and presentation-eligibility drivers under
Clang address/undefined-behavior sanitizers. Works on macOS with VitaSDK.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


SOURCE = Path(__file__).resolve().parents[1]
CXX = os.environ.get("CXX", shutil.which("clang++") or "clang++")
CC = os.environ.get("CC", shutil.which("clang") or "clang")
VITASDK = Path(os.environ.get("VITASDK", "/usr/local/vitasdk"))
INCLUDES = [
    "-I" + str(SOURCE / "include"),
    "-I" + str(SOURCE / "extern/vitaGL/source"),
    "-idirafter", str(VITASDK / "arm-vita-eabi/include"),
]
SANITIZERS = ["-O1", "-g", "-UNDEBUG", "-fsanitize=address,undefined",
              "-fno-omit-frame-pointer"]


def check(command):
    print("+ " + " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=SOURCE, check=True, timeout=150)


def main():
    with tempfile.TemporaryDirectory(prefix="strikers-c9-framegen-host-") as workspace:
        out = Path(workspace)
        for extra in ([], ["-DVGL_TEMPORAL_FORCE_SCALAR=1"]):
            name = "scalar" if extra else "native"
            binary = out / ("framegen_" + name)
            check([CC, "-std=c11", *SANITIZERS, *extra,
                   SOURCE / "tests/framegen_temporal_test.c", "-o", binary])
            check([binary])
        driver = out / "vitagl_framegen_driver"
        check([CXX, "-std=c++20", *SANITIZERS,
               "-DPORT_VITA=1", "-DSTRIKERS_VITA_VITAGL=1",
               "-DSTRIKERS_BOOT_SCREEN_TEST=1", *INCLUDES,
               SOURCE / "tests/vitagl_renderer_driver_test.cpp",
               SOURCE / "src/platform/vitagl_renderer.cpp",
               "-o", driver])
        check([driver])
    print("PASS: temporal interpolation + C9 renderer integration (ASan/UBSan)")


if __name__ == "__main__":
    main()
