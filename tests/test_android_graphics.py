"""Host regressions: python3 -m unittest discover -s tests -v (C++11 + make)."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GL = ROOT / "GLideN64/src/Graphics/OpenGLContext"


class AndroidGraphicsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="m64-graphics-tests-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.make = shutil.which("gmake") or shutil.which("make")
        cls.compiler = cls.directory / "target-compiler"
        cls.compiler.write_text('#!/bin/sh\necho "$TEST_COMPILER_TARGET"\n')
        cls.compiler.chmod(0o755)
        cls.print_make = cls.directory / "print.mk"
        cls.print_make.write_text(
            "$(info TEST_FLAGS=$(CXXFLAGS))\n"
            "$(info TEST_SOURCES=$(SOURCES_CXX))\n"
            "$(info TEST_LINK=$(LDFLAGS))\n"
            ".PHONY: print-config\nprint-config:\n\t@:\n"
        )

        # Supply only the Android/GL interfaces used by the production sources.
        headers = {
            "android/hardware_buffer.h": """
#pragma once
#include <stdint.h>
struct AHardwareBuffer { int value; };
struct AHardwareBuffer_Desc {
    uint32_t width, height, layers, format;
    uint64_t usage;
    uint32_t stride, rfu0;
    uint64_t rfu1;
};
struct ARect { int32_t left, top, right, bottom; };
""",
            "android/sensor.h": "#pragma once\n",
            "EGL/egl.h": "#pragma once\ntypedef void* EGLClientBuffer;\n",
            "sys/system_properties.h": """
#pragma once
#define PROP_VALUE_MAX 92
extern "C" int __system_property_get(const char*, char*);
""",
            "interfaces.h": """
#pragma once
#define GLFUNCTIONS_H
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#include <android/hardware_buffer.h>
extern void* (*ptrGetNativeClientBufferANDROID)(const AHardwareBuffer*);
#define IS_GL_FUNCTION_VALID(name) (ptr##name != nullptr)
#define eglGetNativeClientBufferANDROID(buffer) ptrGetNativeClientBufferANDROID(buffer)
#define dlopen test_dlopen
#define dlsym test_dlsym
#define dlclose test_dlclose
""",
        }
        for name, content in headers.items():
            path = cls.directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        cls.executable = cls.directory / "android-graphics-test"
        subprocess.run(
            shlex.split(os.environ.get("CXX", "c++"))
            + ["-std=c++11", "-DNDEBUG", "-DGLESX",
               "-I" + str(cls.directory), "-I" + str(ROOT / "GLideN64/src"),
               "-I" + str(ROOT / "GLideN64/src/inc"),
               "-include", str(cls.directory / "interfaces.h"),
               str(ROOT / "tests/android_graphics_test.cpp"),
               str(GL / "opengl_Utils.cpp"),
               str(GL / "GraphicBuffer/GraphicBufferWrapper.cpp"),
               str(GL / "GraphicBuffer/PublicApi/android_hardware_buffer_compat.cpp"),
               "-o", str(cls.executable)], check=True,
        )

    def config(self, platform, target, *extra):
        output = subprocess.check_output(
            [self.make, "--no-print-directory", "-f", "Makefile", "-f",
             str(self.print_make), "print-config", "platform=" + platform,
             "ARCH=aarch64", "HAVE_NEON=0", "GIT_VERSION=\" unknown\"",
             "CC=" + str(self.compiler), *extra], cwd=ROOT, text=True,
            env={**os.environ, "TEST_COMPILER_TARGET": target},
        )
        return dict(line[5:].split("=", 1) for line in output.splitlines()
                    if line.startswith("TEST_"))

    def test_android_platforms_include_reader_dependencies(self):
        for platform in ("android", "android-gles3", "android-x86_64-gles3",
                         "arm64_cortex_a53_gles3"):
            with self.subTest(platform=platform):
                config = self.config(platform, "aarch64-unknown-linux-android23")
                self.assertIn("-DOS_ANDROID", config["FLAGS"].split())
                self.assertIn("-DEGL", config["FLAGS"].split())
                self.assertIn("GraphicBufferWrapper.cpp", config["SOURCES"])
                self.assertNotIn("-lpthread", config["LINK"].split())

    def test_linux_target_stays_linux(self):
        config = self.config("arm64_cortex_a53_gles3", "aarch64-linux-gnu")
        self.assertNotIn("-DOS_ANDROID", config["FLAGS"].split())
        self.assertNotIn("GraphicBufferWrapper.cpp", config["SOURCES"])
        self.assertIn("-lpthread", config["LINK"].split())

    def test_debug_requests_keep_release_flags(self):
        for debug in ("0", "1"):
            with self.subTest(debug=debug):
                flags = self.config("android-gles3", "arm-linux-android23",
                                    "DEBUG=" + debug)["FLAGS"].split()
                self.assertIn("-O3", flags)
                self.assertIn("-DNDEBUG", flags)
                self.assertNotIn("-O0", flags)
                self.assertNotIn("-DOPENGL_DEBUG", flags)

    def test_ndk_debug_requests_keep_release_optimization(self):
        output = subprocess.check_output(
            [self.make, "--no-print-directory", "-f", "libretro/jni/Application.mk",
             "--eval=print-optim:;@echo $(APP_OPTIM)", "print-optim",
             "NDK_DEBUG=1", "APP_OPTIM=debug"], cwd=ROOT, text=True,
        )
        self.assertEqual(output.strip(), "release")

    def test_runtime_failures_and_buffer_lifetime(self):
        for scenario in ("success", "old-api", "missing-property",
                         "missing-library", "missing-symbol", "missing-egl"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.executable), scenario], check=True)


if __name__ == "__main__":
    unittest.main()
