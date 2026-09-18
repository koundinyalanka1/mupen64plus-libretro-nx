"""Device-neutral release-mode frame skipping checks (no ROM/GPU needed)."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FrameSkipTests(unittest.TestCase):
    def test_release_policy_and_thread_handoff(self):
        with tempfile.TemporaryDirectory(prefix="m64-frame-skip-") as directory:
            executable = Path(directory) / "frame-skip-test"
            subprocess.run(
                shlex.split(os.environ.get("CXX", "c++"))
                + ["-std=c++11", "-O3", "-DNDEBUG", "-D__LIBRETRO__",
                   "-DMUPENPLUSAPI", "-pthread",
                   "-I" + str(ROOT / "GLideN64/src"),
                   "-I" + str(ROOT / "GLideN64/src/inc"),
                   "-I" + str(ROOT / "custom/GLideN64"),
                   "-I" + str(ROOT / "libretro"),
                   str(ROOT / "tests/frame_skip_test.cpp"),
                   str(ROOT / "libretro/frame_skip.cpp"),
                   "-o", str(executable)], check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
