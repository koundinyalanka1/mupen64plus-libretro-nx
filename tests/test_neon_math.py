"""NEON geometry-path checks (no ROM/GPU needed; ARM hosts only)."""
import os
import platform
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

# The translation unit under test is written in ARM NEON intrinsics, so it can
# only be built and run on an ARM host. Everywhere else there is nothing to
# check -- the scalar 3DMath.cpp is what ships.
_IS_ARM = platform.machine().lower() in {"arm64", "aarch64", "armv7l", "armv8l"}


class NeonMathTests(unittest.TestCase):
    def build_and_run(self, source):
        with tempfile.TemporaryDirectory(prefix="m64-neon-math-") as directory:
            executable = Path(directory) / "neon-test"
            flags = ["-mfpu=neon"] if platform.machine().lower() in {"armv7l", "armv8l"} else []
            subprocess.run(
                shlex.split(os.environ.get("CXX", "c++"))
                + ["-std=c++11", "-O2", "-DNDEBUG", "-D__LIBRETRO__",
                   "-DMUPENPLUSAPI",
                   "-I" + str(ROOT / "GLideN64/src"),
                   "-I" + str(ROOT / "GLideN64/src/inc"),
                   "-I" + str(ROOT / "custom/GLideN64"),
                   str(ROOT / "tests" / source),
                   "-o", str(executable)] + flags, check=True,
            )
            subprocess.run([str(executable)], check=True)

    @unittest.skipUnless(_IS_ARM, "NEON intrinsics require an ARM host")
    def test_normalization_handles_degenerate_vectors(self):
        self.build_and_run("neon_math_test.cpp")

    @unittest.skipUnless(_IS_ARM, "NEON intrinsics require an ARM host")
    def test_software_lighting_matches_scalar_colors(self):
        self.build_and_run("neon_lighting_test.cpp")


if __name__ == "__main__":
    unittest.main()
