"""Host checks for JIT page sizes and the AArch64 coroutine calling convention."""
import os
from pathlib import Path
import platform
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimePortabilityTests(unittest.TestCase):
    def build_and_run(self, compiler, arguments):
        with tempfile.TemporaryDirectory(prefix="m64-runtime-") as directory:
            executable = Path(directory) / "runtime-test"
            subprocess.run(
                shlex.split(compiler) + arguments + ["-o", str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)

    @unittest.skipIf(os.name == "nt", "allocator test mocks the POSIX memory API")
    def test_jit_page_alignment_and_failures(self):
        self.build_and_run(
            os.environ.get("CXX", "c++"),
            ["-std=c++11", "-O2", str(ROOT / "tests/jit_allocator_test.cpp")],
        )

    @unittest.skipUnless(platform.machine().lower() in {"arm64", "aarch64"},
                         "AArch64 coroutine test requires an AArch64 host")
    def test_coroutine_preserves_fp_registers(self):
        self.build_and_run(
            os.environ.get("CC", "cc"),
            ["-std=gnu11", "-O2", "-DHAVE_POSIX_MEMALIGN=1",
             "-I" + str(ROOT / "libretro-common/include"),
             str(ROOT / "tests/libco_aarch64_test.c"),
             str(ROOT / "libretro-common/libco/libco.c")],
        )


if __name__ == "__main__":
    unittest.main()
