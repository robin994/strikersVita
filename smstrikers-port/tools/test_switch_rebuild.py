"""Exercise first-pass failures without running the compiler or stub generator."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

SCRIPT = Path(__file__).parent / "switch" / "rebuild.sh"
CASES = [
    ("FAILED: object.o\nassembler failed", False),
    ("FAILED: [code=1] generated.h\ngenerator failed", False),
    ("ninja: error: loading 'build.ninja': No such file", False),
    ("FAILED: strikers.elf\nundefined reference to missing\nFAILED: object.o\nassembler failed", False),
    ("FAILED: strikers.elf\nundefined reference to missing", True),
]
for log, allowed in CASES:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "tools/switch").mkdir(parents=True)
        (root / "build-switch").mkdir()
        (root / "bin").mkdir()
        shutil.copyfile(SCRIPT, root / "tools/switch/rebuild.sh")
        (root / "failure.log").write_text(log + "\n")
        (root / "bin/cmake").write_text('''#!/bin/sh
case "$*" in
  *strikers_nro*) touch build-switch/strikers.nro; exit 0 ;;
  *) cat failure.log; exit 1 ;;
esac
''')
        (root / "bin/python3").write_text('#!/bin/sh\ntouch generator-called\n')
        for file in (root / "bin").iterdir():
            file.chmod(0o755)
        env = dict(os.environ, PATH=str(root / "bin") + os.pathsep + os.environ["PATH"])
        run = subprocess.run(["sh", str(root / "tools/switch/rebuild.sh")], env=env,
                             capture_output=True, text=True)
        assert (root / "generator-called").exists() == allowed, run.stdout + run.stderr
        assert run.returncode == (0 if allowed else 1), run.stdout + run.stderr
print(f"ok: {len(CASES)} Switch rebuild failure cases")
