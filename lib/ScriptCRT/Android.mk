#
# Copyright (C) 2011-2012 The Android Open Source Project
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

include $(CLEAR_VARS)
LOCAL_MODULE := libclcore.bc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES

ifeq "REL" "$(PLATFORM_VERSION_CODENAME)"
  RS_VERSION := $(PLATFORM_SDK_VERSION)
else
  # Increment by 1 whenever this is not a final release build, since we want to
  # be able to see the RS version number change during development.
  # See build/core/version_defaults.mk for more information about this.
  RS_VERSION := "(1 + $(PLATFORM_SDK_VERSION))"
endif

# C source files for the library
clcore_c_files := \
    clamp.c \
    rs_allocation.c \
    rs_cl.c \
    rs_core.c \
    rs_element.c \
    rs_mesh.c \
    rs_program.c \
    rs_sample.c \
    rs_sampler.c

# Hand-written bitcode for the library
clcore_ll_files := \
    convert.ll \
    matrix.ll \
    pixel_packing.ll

include $(BUILD_SYSTEM)/base_rules.mk

clcore_CLANG := $(HOST_OUT_EXECUTABLES)/clang$(HOST_EXECUTABLE_SUFFIX)
clcore_LLVM_LINK := $(HOST_OUT_EXECUTABLES)/llvm-link$(HOST_EXECUTABLE_SUFFIX)
clcore_LLVM_LD := $(HOST_OUT_EXECUTABLES)/llvm-ld$(HOST_EXECUTABLE_SUFFIX)
clcore_LLVM_AS := $(HOST_OUT_EXECUTABLES)/llvm-as$(HOST_EXECUTABLE_SUFFIX)
clcore_LLVM_DIS := $(HOST_OUT_EXECUTABLES)/llvm-dis$(HOST_EXECUTABLE_SUFFIX)

clcore_c_bc_files := $(patsubst %.c,%.bc, \
    $(addprefix $(intermediates)/, $(clcore_c_files)))

clcore_ll_bc_files := $(patsubst %.ll,%.bc, \
    $(addprefix $(intermediates)/, $(clcore_ll_files)))

$(clcore_c_bc_files): PRIVATE_INCLUDES := \
    frameworks/rs/scriptc \
    external/clang/lib/Headers

$(clcore_c_bc_files): $(intermediates)/%.bc: $(LOCAL_PATH)/%.c  $(clcore_CLANG)
	@mkdir -p $(dir $@)
	$(hide) $(clcore_CLANG) $(addprefix -I, $(PRIVATE_INCLUDES)) -MD -DRS_VERSION=$(RS_VERSION) -std=c99 -c -O3 -fno-builtin -emit-llvm -ccc-host-triple armv7-none-linux-gnueabi -fsigned-char $< -o $@

$(clcore_ll_bc_files): $(intermediates)/%.bc: $(LOCAL_PATH)/%.ll $(clcore_LLVM_AS)
	@mkdir -p $(dir $@)
	$(hide) $(clcore_LLVM_AS) $< -o $@

-include $(clcore_c_bc_files:%.bc=%.d)
-include $(clcore_ll_bc_files:%.bc=%.d)

$(LOCAL_BUILT_MODULE): PRIVATE_BC_FILES := $(clcore_c_bc_files) $(clcore_ll_bc_files)
$(LOCAL_BUILT_MODULE): $(clcore_c_bc_files) $(clcore_ll_bc_files)
$(LOCAL_BUILT_MODULE): $(clcore_LLVM_LINK) $(clcore_LLVM_LD)
$(LOCAL_BUILT_MODULE): $(clcore_LLVM_AS) $(clcore_LLVM_DIS)
	@mkdir -p $(dir $@)
	$(hide) $(clcore_LLVM_LINK) $(PRIVATE_BC_FILES) -o $@
