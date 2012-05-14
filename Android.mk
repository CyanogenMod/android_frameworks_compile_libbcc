#
# Copyright (C) 2010-2012 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)
include $(LOCAL_PATH)/libbcc-config.mk


#=====================================================================
# Whole Static Library to Be Linked In
#=====================================================================

ifeq ($(libbcc_USE_DISASSEMBLER),1)
libbcc_WHOLE_STATIC_LIBRARIES += libbccDisassembler
endif

libbcc_WHOLE_STATIC_LIBRARIES += \
  libbccExecutionEngine \
  libbccHelper \
  libbccTransforms


#=====================================================================
# Calculate SHA1 checksum for libbcc.so and libRS.so
#=====================================================================

include $(CLEAR_VARS)

LOCAL_MODULE := libbcc.so.sha1
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES

include $(BUILD_SYSTEM)/base_rules.mk
libbcc_SHA1_SRCS := \
  $(TARGET_OUT_INTERMEDIATE_LIBRARIES)/libbcc.so \
  $(TARGET_OUT_INTERMEDIATE_LIBRARIES)/libRS.so

libbcc_GEN_SHA1_STAMP := $(LOCAL_PATH)/tools/build/gen-sha1-stamp.py

$(LOCAL_BUILT_MODULE): PRIVATE_SHA1_SRCS := $(libbcc_SHA1_SRCS)
$(LOCAL_BUILT_MODULE): $(libbcc_SHA1_SRCS) $(libbcc_GEN_SHA1_STAMP)
	$(hide) mkdir -p $(dir $@) && \
	        $(libbcc_GEN_SHA1_STAMP) $@ $(PRIVATE_SHA1_SRCS)


#=====================================================================
# Device Shared Library libbcc
#=====================================================================

include $(CLEAR_VARS)

LOCAL_MODULE := libbcc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES

LOCAL_CFLAGS := $(libbcc_CFLAGS)
LOCAL_C_INCLUDES := $(libbcc_C_INCLUDES)

LOCAL_SRC_FILES := lib/ExecutionEngine/bcc.cpp

LOCAL_WHOLE_STATIC_LIBRARIES := $(libbcc_WHOLE_STATIC_LIBRARIES)

ifeq ($(TARGET_ARCH),$(filter $(TARGET_ARCH),arm x86))
LOCAL_WHOLE_STATIC_LIBRARIES += libbccCompilerRT
endif

LOCAL_STATIC_LIBRARIES += librsloader

ifeq ($(libbcc_USE_DISASSEMBLER),1)
  ifeq ($(TARGET_ARCH),arm)
    LOCAL_STATIC_LIBRARIES += \
      libLLVMARMDisassembler \
      libLLVMARMAsmPrinter
  else
    ifeq ($(TARGET_ARCH),mips)
	  $(error "Disassembler is not available for MIPS architecture")
    else
      ifeq ($(TARGET_ARCH),x86)
        LOCAL_STATIC_LIBRARIES += \
          libLLVMX86Disassembler
      else
        $(error Unsupported TARGET_ARCH $(TARGET_ARCH))
      endif
    endif
  endif
endif

ifeq ($(TARGET_ARCH),arm)
  LOCAL_STATIC_LIBRARIES += \
    libLLVMARMCodeGen \
    libLLVMARMDesc \
    libLLVMARMInfo
else
  ifeq ($(TARGET_ARCH), mips)
    LOCAL_STATIC_LIBRARIES += \
      libLLVMMipsCodeGen \
      libLLVMMipsAsmPrinter \
      libLLVMMipsDesc \
      libLLVMMipsInfo
  else
    ifeq ($(TARGET_ARCH),x86) # We don't support x86-64 right now
      LOCAL_STATIC_LIBRARIES += \
        libLLVMX86CodeGen \
        libLLVMX86Desc \
        libLLVMX86Info \
        libLLVMX86Utils \
        libLLVMX86AsmPrinter
    else
      $(error Unsupported TARGET_ARCH $(TARGET_ARCH))
    endif
  endif
endif

LOCAL_STATIC_LIBRARIES += \
  libLLVMAsmPrinter \
  libLLVMBitReader \
  libLLVMSelectionDAG \
  libLLVMCodeGen \
  libLLVMLinker \
  libLLVMScalarOpts \
  libLLVMInstCombine \
  libLLVMipo \
  libLLVMipa \
  libLLVMTransformUtils \
  libLLVMAnalysis \
  libLLVMTarget \
  libLLVMMCParser \
  libLLVMMC \
  libLLVMCore \
  libLLVMSupport

LOCAL_SHARED_LIBRARIES := libbcinfo libdl libutils libcutils libstlport

# Modules that need get installed if and only if the target libbcc.so is
# installed.
LOCAL_REQUIRED_MODULES := libclcore.bc libbcc.so.sha1

ifeq ($(ARCH_ARM_HAVE_NEON),true)
LOCAL_REQUIRED_MODULES += libclcore_neon.bc
endif

# Link-Time Optimization on libbcc.so
#
# -Wl,--exclude-libs=ALL only applies to library archives. It would hide most
# of the symbols in this shared library. As a result, it reduced the size of
# libbcc.so by about 800k in 2010.
#
# Note that libLLVMBitReader:libLLVMCore:libLLVMSupport are used by
# pixelflinger2.

LOCAL_LDFLAGS += -Wl,--exclude-libs=libLLVMARMDisassembler:libLLVMARMAsmPrinter:libLLVMX86Disassembler:libLLVMX86AsmPrinter:libLLVMMCParser:libLLVMARMCodeGen:libLLVMARMDesc:libLLVMARMInfo:libLLVMSelectionDAG:libLLVMAsmPrinter:libLLVMCodeGen:libLLVMLinker:libLLVMTarget:libLLVMMC:libLLVMScalarOpts:libLLVMInstCombine:libLLVMipo:libLLVMipa:libLLVMTransformUtils:libLLVMAnalysis

# Generate build stamp (Build time + Build git revision + Build Semi SHA1)
include $(LOCAL_PATH)/libbcc-gen-build-stamp.mk

include $(LIBBCC_ROOT_PATH)/libbcc-gen-config-from-mk.mk
include $(LLVM_ROOT_PATH)/llvm-device-build.mk
include $(BUILD_SHARED_LIBRARY)


#=====================================================================
# Host Shared Library libbcc
#=====================================================================

include $(CLEAR_VARS)

LOCAL_MODULE := libbcc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_IS_HOST_MODULE := true

LOCAL_CFLAGS := $(libbcc_CFLAGS)
LOCAL_CFLAGS += -D__HOST__
LOCAL_C_INCLUDES := $(libbcc_C_INCLUDES)

LOCAL_SRC_FILES := lib/ExecutionEngine/bcc.cpp

LOCAL_WHOLE_STATIC_LIBRARIES += $(libbcc_WHOLE_STATIC_LIBRARIES)

LOCAL_STATIC_LIBRARIES += librsloader

ifeq ($(libbcc_USE_DISASSEMBLER),1)
  LOCAL_STATIC_LIBRARIES += \
    libLLVMARMDisassembler \
    libLLVMARMAsmPrinter \
    libLLVMX86Disassembler \
    libLLVMMCParser
endif

LOCAL_STATIC_LIBRARIES += \
  libLLVMARMCodeGen \
  libLLVMARMDesc \
  libLLVMARMInfo

LOCAL_STATIC_LIBRARIES += \
  libLLVMMipsCodeGen \
  libLLVMMipsAsmPrinter \
  libLLVMMipsDesc \
  libLLVMMipsInfo

LOCAL_STATIC_LIBRARIES += \
  libLLVMX86CodeGen \
  libLLVMX86Desc \
  libLLVMX86AsmPrinter \
  libLLVMX86Info \
  libLLVMX86Utils

LOCAL_STATIC_LIBRARIES += \
  libLLVMAsmPrinter \
  libLLVMBitReader \
  libLLVMSelectionDAG \
  libLLVMCodeGen \
  libLLVMLinker \
  libLLVMScalarOpts \
  libLLVMInstCombine \
  libLLVMipo \
  libLLVMipa \
  libLLVMTransformUtils \
  libLLVMAnalysis \
  libLLVMTarget \
  libLLVMMCParser \
  libLLVMMC \
  libLLVMCore \
  libLLVMSupport

LOCAL_STATIC_LIBRARIES += \
  libutils \
  libcutils

LOCAL_SHARED_LIBRARIES := libbcinfo

LOCAL_LDLIBS := -ldl -lpthread

# Generate build stamp (Build time + Build git revision + Build Semi SHA1)
include $(LOCAL_PATH)/libbcc-gen-build-stamp.mk

include $(LIBBCC_ROOT_PATH)/libbcc-gen-config-from-mk.mk
include $(LLVM_ROOT_PATH)/llvm-host-build.mk
include $(BUILD_HOST_SHARED_LIBRARY)


#=====================================================================
# Include Subdirectories
#=====================================================================
include $(call all-makefiles-under,$(LOCAL_PATH))
