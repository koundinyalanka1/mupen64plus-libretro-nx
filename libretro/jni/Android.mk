LOCAL_PATH := $(abspath $(call my-dir))

ROOT_DIR     := $(abspath $(LOCAL_PATH)/../..)
LIBRETRO_DIR := $(ROOT_DIR)/libretro
INCFLAGS     := -I$(ROOT_DIR)/GLideN64/src/GLideNHQ/inc

# ndk-build evaluates this file once per ABI. Keep Makefile.common's additions
# local to that ABI instead of carrying flags and plugin settings into the next.
M64_SAVED_CFLAGS := $(CFLAGS)
M64_SAVED_CXXFLAGS := $(CXXFLAGS)
M64_SAVED_LDFLAGS := $(LDFLAGS)
CFLAGS :=
CXXFLAGS :=
LDFLAGS :=

# Reset flags that the common makefile doesn't properly handle
platform     := android
COREFLAGS    :=
COREASMFLAGS :=
CORELDLIBS   :=
DYNAFLAGS    :=
GLES         :=
GLFLAGS      :=
HAVE_NEON    :=
SOURCES_C    :=
SOURCES_CXX  :=
SOURCES_ASM  :=
SOURCES_NASM :=
ANDROID      := 1
AWK          ?= awk
STRINGS      ?= strings
TR           ?= tr
ifndef M64_PLUGIN_DEFAULTS_SAVED
M64_DEFAULT_PARALLEL_RSP := $(or $(HAVE_PARALLEL_RSP),0)
M64_DEFAULT_PARALLEL_RDP := $(or $(HAVE_PARALLEL_RDP),0)
M64_DEFAULT_THR_AL := $(or $(HAVE_THR_AL),0)
M64_DEFAULT_LLE := $(or $(LLE),0)
M64_PLUGIN_DEFAULTS_SAVED := 1
endif
HAVE_PARALLEL_RSP := $(M64_DEFAULT_PARALLEL_RSP)
HAVE_PARALLEL_RDP := $(M64_DEFAULT_PARALLEL_RDP)
HAVE_THR_AL       := $(M64_DEFAULT_THR_AL)
LLE              := $(M64_DEFAULT_LLE)
LOCAL_SHORT_COMMANDS := true
WITH_DYNAREC :=

ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
  WITH_DYNAREC := arm
  HAVE_NEON := 1
  LLE = 1
  HAVE_PARALLEL_RSP = 1
  HAVE_PARALLEL_RDP = 1
  HAVE_THR_AL = 1
else ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
  WITH_DYNAREC := aarch64
  HAVE_NEON := 1
  LLE = 1
  HAVE_PARALLEL_RSP = 1
  HAVE_PARALLEL_RDP = 1
  HAVE_THR_AL = 1
else ifeq ($(TARGET_ARCH_ABI),x86)
  WITH_DYNAREC := x86
  COREASMFLAGS := -DELF_TYPE -DPIC
  COREFLAGS := -fPIC
else ifeq ($(TARGET_ARCH_ABI),x86_64)
  WITH_DYNAREC := x86_64
  COREASMFLAGS := -DELF_TYPE -DPIC
endif

ifeq ($(GLES3),1)
  GLLIB := -lGLESv3
else
  GLES  := 1
  GLLIB := -lGLESv2
endif

AWK_DEST_DIR := $(TARGET_OBJS)/retro/asm-defines
include $(ROOT_DIR)/Makefile.common

COREFLAGS += -D__LIBRETRO__ -DOS_ANDROID -DUSE_FILE32API -DM64P_PLUGIN_API -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE -DSINC_LOWER_QUALITY -DMUPENPLUSAPI -DTXFILTER_LIB -D__VEC4_OPT $(INCFLAGS) $(GLFLAGS) $(DYNAFLAGS) -DANDROID -DEGL_EGLEXT_PROTOTYPES -DHAVE_POSIX_MEMALIGN=1

ifeq ($(LLE), 1)
   COREFLAGS += -DHAVE_LLE
endif

ifeq ($(HAVE_PARALLEL_RSP), 1)
   COREFLAGS += -DHAVE_MMAP=1
endif

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
  COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

include $(CLEAR_VARS)
LOCAL_MODULE           := retro
LOCAL_SRC_FILES        := $(patsubst $(ROOT_DIR)/%,../../%,$(SOURCES_CXX) $(SOURCES_C) $(SOURCES_ASM) $(SOURCES_NASM))
LOCAL_ASMFLAGS         := $(COREASMFLAGS) -I$(AWK_DEST_DIR)/
LOCAL_CPPFLAGS         := -std=gnu++11 $(M64_SAVED_CXXFLAGS) $(CXXFLAGS) -fno-fast-math -ffp-contract=off
LOCAL_CFLAGS           := $(M64_SAVED_CFLAGS) $(CFLAGS) $(COREFLAGS) -O3 -fno-strict-aliasing -fno-fast-math -ffp-contract=off
LOCAL_LDFLAGS          := $(M64_SAVED_LDFLAGS) $(LDFLAGS) -Wl,-version-script=$(LIBRETRO_DIR)/link.T -Wl,-z,max-page-size=16384 -fno-fast-math
LOCAL_LDLIBS           := -llog -ldl -lEGL $(GLLIB) $(CORELDLIBS)
LOCAL_STATIC_LIBRARIES := 
LOCAL_CPP_FEATURES     := exceptions
LOCAL_ARM_MODE         := arm
LOCAL_ARM_NEON         := true
LOCAL_CONLYFLAGS       := -std=gnu11

# Generate structure offsets using the target compiler's object, separately for
# each ABI. Never consume an untracked header left in the source tree.
M64_ASM_OBJECT := $(TARGET_OBJS)/retro/__/__/mupen64plus-core/src/asm_defines/asm_defines.o
M64_LINKAGE_OBJECTS := $(addprefix $(TARGET_OBJS)/retro/,$(subst ../,__/,$(patsubst %.S,%.o,$(patsubst %.asm,%.o,$(filter %.S %.asm,$(LOCAL_SRC_FILES))))))
$(M64_LINKAGE_OBJECTS): $(AWK_DEST_DIR)/asm_defines_gas.h $(AWK_DEST_DIR)/asm_defines_nasm.h
$(AWK_DEST_DIR)/asm_defines_gas.h: $(AWK_DEST_DIR)/asm_defines_nasm.h
	@test -f "$@" || sed 's/^%define /#define /' "$<" > "$@"
$(AWK_DEST_DIR)/asm_defines_nasm.h: PRIVATE_M64_ASM_DIR := $(AWK_DEST_DIR)
$(AWK_DEST_DIR)/asm_defines_nasm.h: PRIVATE_M64_AWK := $(CORE_DIR)/tools/gen_asm_defines.awk
$(AWK_DEST_DIR)/asm_defines_nasm.h: $(M64_ASM_OBJECT) $(CORE_DIR)/tools/gen_asm_defines.awk
	@mkdir -p "$(PRIVATE_M64_ASM_DIR)"
	$(STRINGS) "$<" | $(TR) -d '\r' | $(AWK) -v dest_dir="$(PRIVATE_M64_ASM_DIR)" -f "$(PRIVATE_M64_AWK)"

include $(BUILD_SHARED_LIBRARY)

CFLAGS := $(M64_SAVED_CFLAGS)
CXXFLAGS := $(M64_SAVED_CXXFLAGS)
LDFLAGS := $(M64_SAVED_LDFLAGS)
