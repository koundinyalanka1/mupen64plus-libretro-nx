"""Android architecture/optimization selection without requiring an NDK."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidAbiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="m64-abi-config-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.make = shutil.which("gmake") or shutil.which("make")
        cls.compiler = cls.directory / "clang"
        cls.compiler.write_text('#!/bin/sh\necho "clang aarch64-linux-android"\n')
        cls.compiler.chmod(0o755)
        cls.print_make = cls.directory / "print.mk"
        cls.print_make.write_text(
            "$(info TEST_ARCH=$(ARCH))\n"
            "$(info TEST_DEFAULT_GOAL=$(.DEFAULT_GOAL))\n"
            "$(info TEST_DYNAREC=$(WITH_DYNAREC))\n"
            "$(info TEST_FLAGS=$(CXXFLAGS))\n"
            "$(info TEST_LINK=$(LDFLAGS))\n"
            "$(info TEST_ASFLAGS=$(ASFLAGS))\n"
            "$(info TEST_SOURCES=$(SOURCES_CXX) $(SOURCES_ASM) $(SOURCES_NASM))\n"
            ".PHONY: print-config\nprint-config:\n\t@:\n"
        )

    def config(self, platform, *extra):
        output = subprocess.check_output(
            [self.make, "--no-print-directory", "-f", "Makefile", "-f",
             str(self.print_make), "print-config", "platform=" + platform,
             "GIT_VERSION=\" unknown\"", "CC=" + str(self.compiler), *extra],
            cwd=ROOT, text=True,
        )
        return dict(line[5:].split("=", 1) for line in output.splitlines()
                    if line.startswith("TEST_"))

    def test_each_android_target_selects_its_own_dynarec(self):
        for platform, arch, define, linkage in (
            ("android-armeabi-v7a", "arm", "3", "linkage_arm.S"),
            ("android-arm64-v8a", "aarch64", "4", "linkage_arm64.S"),
            ("android-aarch64", "aarch64", "4", "linkage_arm64.S"),
            ("android-x86_64", "x86_64", "2", "linkage_x64.asm"),
        ):
            with self.subTest(platform=platform):
                config = self.config(platform)
                self.assertEqual(config["ARCH"], arch)
                self.assertEqual(config["DYNAREC"], arch)
                self.assertIn("-DNEW_DYNAREC=" + define, config["FLAGS"].split())
                self.assertIn(linkage, config["SOURCES"])
                self.assertNotIn("linkage_x86.asm", config["SOURCES"])
                if arch == "x86_64":
                    self.assertIn("elf64", config["ASFLAGS"].split())
                    self.assertNotIn("-D__NEON_OPT", config["FLAGS"].split())
                else:
                    self.assertIn("-D__NEON_OPT", config["FLAGS"].split())
                    self.assertIn("Neon/3DMathNeon.cpp", config["SOURCES"])
                    self.assertNotIn("-mvectorize-with-neon-quad", config["FLAGS"])

    def test_android_float_semantics_survive_legacy_presets(self):
        for platform in ("android-armeabi-v7a", "android-arm64-v8a",
                         "android-x86_64", "arm64_cortex_a53_gles3"):
            with self.subTest(platform=platform):
                config = self.config(platform, "PLATCFLAGS=-ffast-math")
                flags = config["FLAGS"].split()
                self.assertGreater(flags.index("-fno-fast-math"),
                                   flags.index("-ffast-math"))
                self.assertIn("-ffp-contract=off", flags)
                link = config["LINK"].split()
                self.assertGreater(link.index("-fno-fast-math"),
                                   link.index("-ffast-math"))
                self.assertIn("-Wl,-z,max-page-size=16384", link)

    def test_generated_dependencies_do_not_change_default_goal(self):
        dependency = self.directory / "included.d"
        obj = dependency.with_suffix(".o")
        dependency.write_text(str(obj) + ":\n")
        config = self.config("android-x86_64", "OBJECTS=" + str(obj))
        self.assertEqual(config["DEFAULT_GOAL"], "all")

    def test_ndk_multi_abi_flags_and_offsets_are_isolated(self):
        # Exercise the real Android.mk repeatedly, as ndk-build does, while
        # replacing only the NDK's module-registration includes with collectors.
        clear = self.directory / "clear.mk"
        clear.write_text("")
        collect = self.directory / "collect.mk"
        collect.write_text(
            "$(info ABI_$(TARGET_ARCH_ABI)_FLAGS=$(LOCAL_CFLAGS) $(LOCAL_CPPFLAGS))\n"
            "$(info ABI_$(TARGET_ARCH_ABI)_SOURCES=$(LOCAL_SRC_FILES))\n"
            "$(info ABI_$(TARGET_ARCH_ABI)_ASM=$(LOCAL_ASMFLAGS))\n"
        )
        driver = self.directory / "ndk.mk"
        driver.write_text(
            "my-dir = libretro/jni\n"
            f"CLEAR_VARS := {clear}\nBUILD_SHARED_LIBRARY := {collect}\n"
            + "\n".join(
                f"TARGET_ARCH_ABI := {abi}\n"
                f"TARGET_OBJS := {self.directory}/{abi}\n"
                "include libretro/jni/Android.mk\n"
                for abi in ("armeabi-v7a", "arm64-v8a", "x86_64"))
            + ".PHONY: print-config\nprint-config:\n\t@:\n"
        )
        output = subprocess.check_output(
            [self.make, "--no-print-directory", "-f", str(driver), "print-config"],
            cwd=ROOT, text=True,
        )
        values = dict(line.split("=", 1) for line in output.splitlines()
                      if line.startswith("ABI_"))
        for abi, define in (("armeabi-v7a", "3"), ("arm64-v8a", "4"),
                            ("x86_64", "2")):
            flags = values[f"ABI_{abi}_FLAGS"].split()
            self.assertEqual([f for f in flags if f.startswith("-DNEW_DYNAREC=")],
                             ["-DNEW_DYNAREC=" + define])
            self.assertIn("-I" + str(self.directory / abi / "retro/asm-defines") + "/",
                          values[f"ABI_{abi}_ASM"].split())
            self.assertIn("-fno-strict-aliasing", flags)
            if abi == "x86_64":
                self.assertNotIn("-DHAVE_PARALLEL_RSP", flags)
                self.assertNotIn("-D__NEON_OPT", flags)
                self.assertIn("src/3DMath.cpp", values[f"ABI_{abi}_SOURCES"])
            else:
                self.assertIn("-D__NEON_OPT", flags)
                self.assertIn("Neon/3DMathNeon.cpp", values[f"ABI_{abi}_SOURCES"])

        output = subprocess.check_output(
            [self.make, "--no-print-directory", "-f", str(driver), "print-config"],
            cwd=ROOT, text=True, env={**os.environ, "HAVE_PARALLEL_RSP": "1"},
        )
        values = dict(line.split("=", 1) for line in output.splitlines()
                      if line.startswith("ABI_"))
        self.assertIn("-DHAVE_PARALLEL_RSP", values["ABI_x86_64_FLAGS"].split())


if __name__ == "__main__":
    unittest.main()
