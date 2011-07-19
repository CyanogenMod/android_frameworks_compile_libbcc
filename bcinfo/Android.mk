#
# Copyright (C) 2011 The Android Open Source Project
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

local_cflags_for_libbcinfo := -Wall -Wno-unused-parameter -Werror
ifneq ($(TARGET_BUILD_VARIANT),eng)
local_cflags_for_libbcinfo += -D__DISABLE_ASSERTS
endif

LOCAL_PATH := $(call my-dir)

LLVM_ROOT_PATH := external/llvm

include $(CLEAR_VARS)

LOCAL_MODULE := libbcinfo
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_TAGS := optional
intermediates := $(local-intermediates-dir)

LOCAL_SRC_FILES := bcinfo.cpp

LOCAL_CFLAGS += $(local_cflags_for_libbcinfo)

LOCAL_C_INCLUDES := \
  $(LOCAL_PATH)/../include

LOCAL_STATIC_LIBRARIES += \
  libLLVMBitReader \
  libLLVMBitWriter \
  libLLVMCore \
  libLLVMSupport \

LOCAL_SHARED_LIBRARIES := libcutils libstlport

include $(LLVM_ROOT_PATH)/llvm-device-build.mk
include $(BUILD_SHARED_LIBRARY)
