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


#=====================================================================
# Root Path for Other Projects
#=====================================================================

LLVM_ROOT_PATH := external/llvm

LIBBCC_ROOT_PATH := frameworks/compile/libbcc

RSLOADER_ROOT_PATH := frameworks/compile/linkloader


#=====================================================================
# Extract Configuration from Config.h
#=====================================================================

libbcc_GET_CONFIG = $(shell cat "$(LIBBCC_ROOT_PATH)/Config.h" | \
                            grep "^\#define $1 [01]$$" | \
                            cut -d ' ' -f 3)

libbcc_USE_OLD_JIT			:= $(call libbcc_GET_CONFIG,USE_OLD_JIT)
libbcc_USE_MCJIT			:= $(call libbcc_GET_CONFIG,USE_MCJIT)
libbcc_USE_CACHE			:= $(call libbcc_GET_CONFIG,USE_CACHE)
libbcc_USE_DISASSEMBLER		:= $(call libbcc_GET_CONFIG,USE_DISASSEMBLER)
libbcc_USE_LIBBCC_SHA1SUM	:= $(call libbcc_GET_CONFIG,USE_LIBBCC_SHA1SUM)


#=====================================================================
# Common Variables
#=====================================================================

libbcc_CFLAGS := -Wall -Wno-unused-parameter -Werror
ifneq ($(TARGET_BUILD_VARIANT),eng)
libbcc_CFLAGS += -D__DISABLE_ASSERTS
endif

# Include File Search Path
libbcc_C_INCLUDES := \
  $(RSLOADER_ROOT_PATH)/android \
  $(LIBBCC_ROOT_PATH)/lib \
  $(LIBBCC_ROOT_PATH)/helper \
  $(LIBBCC_ROOT_PATH)/include \
  $(LIBBCC_ROOT_PATH)
