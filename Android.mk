#
# Copyright (C) 2010 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

ifneq ($(TARGET_SIMULATOR),true)

local_cflags_for_libbcc := -Wall -Wno-unused-parameter -Werror
ifneq ($(TARGET_BUILD_VARIANT),eng)
local_cflags_for_libbcc += -D__DISABLE_ASSERTS
endif

LOCAL_PATH := $(call my-dir)

LLVM_ROOT_PATH := external/llvm

RSLOADER_ROOT_PATH := frameworks/compile/linkloader

# Extract Configuration from Config.h

libbcc_GET_CONFIG = $(shell cat "$(LOCAL_PATH)/Config.h" | \
                            grep "^\#define $1 [01]$$" | \
                            cut -d ' ' -f 3)

libbcc_USE_OLD_JIT := $(call libbcc_GET_CONFIG,USE_OLD_JIT)
libbcc_USE_MCJIT := $(call libbcc_GET_CONFIG,USE_MCJIT)
libbcc_USE_CACHE := $(call libbcc_GET_CONFIG,USE_CACHE)
libbcc_USE_DISASSEMBLER := $(call libbcc_GET_CONFIG,USE_DISASSEMBLER)
libbcc_USE_LIBBCC_SHA1SUM := $(call libbcc_GET_CONFIG,USE_LIBBCC_SHA1SUM)

# Source Files

libbcc_SRC_FILES := \
  lib/ExecutionEngine/bcc.cpp \
  lib/ExecutionEngine/Compiler.cpp \
  lib/ExecutionEngine/ContextManager.cpp \
  lib/ExecutionEngine/FileHandle.cpp \
  lib/ExecutionEngine/Runtime.c \
  lib/ExecutionEngine/RuntimeStub.c \
  lib/ExecutionEngine/Script.cpp \
  lib/ExecutionEngine/ScriptCompiled.cpp \
  lib/ExecutionEngine/SourceInfo.cpp \
  lib/Disassembler/Disassembler.cpp

ifeq ($(libbcc_USE_OLD_JIT),1)
libbcc_SRC_FILES += \
  lib/CodeGen/CodeEmitter.cpp \
  lib/CodeGen/CodeMemoryManager.cpp
endif

ifeq ($(libbcc_USE_CACHE),1)
libbcc_SRC_FILES += \
  lib/ExecutionEngine/CacheReader.cpp \
  lib/ExecutionEngine/CacheWriter.cpp \
  lib/ExecutionEngine/ScriptCached.cpp \
  lib/ExecutionEngine/Sha1Helper.cpp \
  lib/ExecutionEngine/MCCacheWriter.cpp \
  lib/ExecutionEngine/MCCacheReader.cpp \
  helper/sha1.c
endif

FULL_PATH_libbcc_SRC_FILES := \
  $(addprefix $(LOCAL_PATH)/, $(libbcc_SRC_FILES)) \
  $(sort $(shell find $(LOCAL_PATH) -name "*.h"))

# Build Host SHA1 Command Line
# ========================================================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := helper/sha1.c
LOCAL_MODULE := sha1sum
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS += -DCMDLINE
include $(BUILD_HOST_EXECUTABLE)

# Calculate SHA1 checksum for libbcc.so and libRS.so
# ========================================================
include $(CLEAR_VARS)
LOCAL_MODULE := libbcc.so.sha1
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES

include $(BUILD_SYSTEM)/base_rules.mk
libbcc_SHA1_SRCS := $(TARGET_OUT_INTERMEDIATE_LIBRARIES)/libbcc.so \
    $(TARGET_OUT_INTERMEDIATE_LIBRARIES)/libRS.so

$(LOCAL_BUILT_MODULE): PRIVATE_SHA1_SRCS := $(libbcc_SHA1_SRCS)
$(LOCAL_BUILT_MODULE) : $(libbcc_SHA1_SRCS) \
                       $(HOST_OUT_EXECUTABLES)/sha1sum
	$(hide) mkdir -p $(dir $@) && \
          cat $(PRIVATE_SHA1_SRCS) | $(HOST_OUT_EXECUTABLES)/sha1sum -B $@

#
# Shared library for target
# ========================================================
include $(CLEAR_VARS)

LOCAL_MODULE := libbcc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_CFLAGS += $(local_cflags_for_libbcc)
LOCAL_SRC_FILES := \
  $(libbcc_SRC_FILES)

LOCAL_CFLAGS += -DTARGET_BUILD

ifeq ($(TARGET_ARCH),arm)
  LOCAL_SRC_FILES += \
    runtime/lib/arm/adddf3vfp.S \
    runtime/lib/arm/addsf3vfp.S \
    runtime/lib/arm/divdf3vfp.S \
    runtime/lib/arm/divsf3vfp.S \
    runtime/lib/arm/eqdf2vfp.S \
    runtime/lib/arm/eqsf2vfp.S \
    runtime/lib/arm/extendsfdf2vfp.S \
    runtime/lib/arm/fixdfsivfp.S \
    runtime/lib/arm/fixsfsivfp.S \
    runtime/lib/arm/fixunsdfsivfp.S \
    runtime/lib/arm/fixunssfsivfp.S \
    runtime/lib/arm/floatsidfvfp.S \
    runtime/lib/arm/floatsisfvfp.S \
    runtime/lib/arm/floatunssidfvfp.S \
    runtime/lib/arm/floatunssisfvfp.S \
    runtime/lib/arm/gedf2vfp.S \
    runtime/lib/arm/gesf2vfp.S \
    runtime/lib/arm/gtdf2vfp.S \
    runtime/lib/arm/gtsf2vfp.S \
    runtime/lib/arm/ledf2vfp.S \
    runtime/lib/arm/lesf2vfp.S \
    runtime/lib/arm/ltdf2vfp.S \
    runtime/lib/arm/ltsf2vfp.S \
    runtime/lib/arm/muldf3vfp.S \
    runtime/lib/arm/mulsf3vfp.S \
    runtime/lib/arm/nedf2vfp.S \
    runtime/lib/arm/negdf2vfp.S \
    runtime/lib/arm/negsf2vfp.S \
    runtime/lib/arm/nesf2vfp.S \
    runtime/lib/arm/subdf3vfp.S \
    runtime/lib/arm/subsf3vfp.S \
    runtime/lib/arm/truncdfsf2vfp.S \
    runtime/lib/arm/unorddf2vfp.S \
    runtime/lib/arm/unordsf2vfp.S
else
  ifeq ($(TARGET_ARCH),x86) # We don't support x86-64 right now
    LOCAL_SRC_FILES += \
      runtime/lib/i386/ashldi3.S \
      runtime/lib/i386/ashrdi3.S \
      runtime/lib/i386/divdi3.S \
      runtime/lib/i386/floatdidf.S \
      runtime/lib/i386/floatdisf.S \
      runtime/lib/i386/floatdixf.S \
      runtime/lib/i386/floatundidf.S \
      runtime/lib/i386/floatundisf.S \
      runtime/lib/i386/floatundixf.S \
      runtime/lib/i386/lshrdi3.S \
      runtime/lib/i386/moddi3.S \
      runtime/lib/i386/muldi3.S \
      runtime/lib/i386/udivdi3.S \
      runtime/lib/i386/umoddi3.S
  else
    $(error Unsupported TARGET_ARCH $(TARGET_ARCH))
  endif
endif

ifeq ($(TARGET_ARCH),arm)
  LOCAL_STATIC_LIBRARIES := \
    libLLVMARMCodeGen \
    libLLVMARMInfo
else
  ifeq ($(TARGET_ARCH),x86) # We don't support x86-64 right now
    LOCAL_STATIC_LIBRARIES := \
      libLLVMX86CodeGen \
      libLLVMX86Info \
      libLLVMX86Utils
  else
    $(error Unsupported TARGET_ARCH $(TARGET_ARCH))
  endif
endif

ifeq ($(libbcc_USE_MCJIT),1)
  LOCAL_STATIC_LIBRARIES += librsloader
endif

LOCAL_STATIC_LIBRARIES += \
  libLLVMBitReader \
  libLLVMSelectionDAG \
  libLLVMAsmPrinter \
  libLLVMCodeGen \
  libLLVMLinker \
  libLLVMJIT \
  libLLVMTarget \
  libLLVMMC \
  libLLVMScalarOpts \
  libLLVMInstCombine \
  libLLVMipo \
  libLLVMipa \
  libLLVMTransformUtils \
  libLLVMCore \
  libLLVMAnalysis \
  libLLVMSupport

LOCAL_SHARED_LIBRARIES := libdl libcutils libutils libstlport

LOCAL_C_INCLUDES := \
  $(RSLOADER_ROOT_PATH)/android \
  $(LOCAL_PATH)/lib/ExecutionEngine \
  $(LOCAL_PATH)/lib/CodeGen \
  $(LOCAL_PATH)/lib \
  $(LOCAL_PATH)/helper \
  $(LOCAL_PATH)/include \
  $(LOCAL_PATH)

ifeq ($(libbcc_USE_DISASSEMBLER),1)
  ifeq ($(TARGET_ARCH),arm)
    LOCAL_STATIC_LIBRARIES += \
      libLLVMARMDisassembler \
      libLLVMARMAsmPrinter
  else
    ifeq ($(TARGET_ARCH),x86)
      LOCAL_STATIC_LIBRARIES += \
        libLLVMX86Disassembler \
        libLLVMX86AsmPrinter
    else
      $(error Unsupported TARGET_ARCH $(TARGET_ARCH))
    endif
  endif
  LOCAL_STATIC_LIBRARIES += \
    libLLVMMCParser \
    $(LOCAL_STATIC_LIBRARIES)
endif

# Modules that need get installed if and only if the target libbcc.so is installed.
LOCAL_REQUIRED_MODULES := libclcore.bc libbcc.so.sha1

# -Wl,--exclude-libs=ALL would hide most of the symbols in the shared library
# and reduces the size of libbcc.so by about 800k.
# As libLLVMBitReader:libLLVMCore:libLLVMSupport are used by pixelflinger2,
# use below instead.
LOCAL_LDFLAGS += -Wl,--exclude-libs=libLLVMARMDisassembler:libLLVMARMAsmPrinter:libLLVMX86Disassembler:libLLVMX86AsmPrinter:libLLVMMCParser:libLLVMARMCodeGen:libLLVMARMInfo:libLLVMSelectionDAG:libLLVMAsmPrinter:libLLVMCodeGen:libLLVMLinker:libLLVMJIT:libLLVMTarget:libLLVMMC:libLLVMScalarOpts:libLLVMInstCombine:libLLVMipo:libLLVMipa:libLLVMTransformUtils:libLLVMAnalysis

include $(LLVM_ROOT_PATH)/llvm-device-build.mk
include $(BUILD_SHARED_LIBRARY)

# Shared library for host
# ========================================================
include $(CLEAR_VARS)

LOCAL_MODULE := libbcc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_IS_HOST_MODULE := true
LOCAL_CFLAGS += $(local_cflags_for_libbcc)
LOCAL_SRC_FILES := \
  $(libbcc_SRC_FILES) \
  helper/DebugHelper.c

ifeq ($(libbcc_USE_MCJIT),1)
  LOCAL_STATIC_LIBRARIES += librsloader
endif

LOCAL_STATIC_LIBRARIES += \
  libcutils \
  libutils \
  libLLVMX86CodeGen \
  libLLVMX86Info \
  libLLVMX86Utils \
  libLLVMX86AsmPrinter \
  libLLVMARMCodeGen \
  libLLVMARMInfo \
  libLLVMBitReader \
  libLLVMSelectionDAG \
  libLLVMAsmPrinter \
  libLLVMMCParser \
  libLLVMCodeGen \
  libLLVMLinker \
  libLLVMJIT \
  libLLVMMC \
  libLLVMScalarOpts \
  libLLVMInstCombine \
  libLLVMipo \
  libLLVMipa \
  libLLVMTransformUtils \
  libLLVMCore \
  libLLVMTarget \
  libLLVMAnalysis \
  libLLVMSupport

LOCAL_LDLIBS := -ldl -lpthread

LOCAL_C_INCLUDES := \
  $(RSLOADER_ROOT_PATH)/android \
  $(LOCAL_PATH)/lib/ExecutionEngine \
  $(LOCAL_PATH)/lib/CodeGen \
  $(LOCAL_PATH)/lib \
  $(LOCAL_PATH)/helper \
  $(LOCAL_PATH)/include \
  $(LOCAL_PATH)

# definitions for LLVM
LOCAL_CFLAGS += -DDEBUG_CODEGEN=1

ifeq ($(TARGET_ARCH),arm)
  LOCAL_CFLAGS += -DFORCE_ARM_CODEGEN=1
else
  ifeq ($(TARGET_ARCH),x86)
    LOCAL_CFLAGS += -DFORCE_X86_CODEGEN=1
  else
    $(error Unsupported TARGET_ARCH $(TARGET_ARCH))
  endif
endif

ifeq ($(libbcc_USE_DISASSEMBLER),1)
LOCAL_STATIC_LIBRARIES := \
  libLLVMARMDisassembler \
  libLLVMARMAsmPrinter \
  libLLVMX86Disassembler \
  libLLVMMCParser \
  $(LOCAL_STATIC_LIBRARIES)
endif

include $(LLVM_ROOT_PATH)/llvm-host-build.mk
include $(BUILD_HOST_SHARED_LIBRARY)

# Build children
# ========================================================
include $(call all-makefiles-under,$(LOCAL_PATH))

endif # TARGET_SIMULATOR != true
