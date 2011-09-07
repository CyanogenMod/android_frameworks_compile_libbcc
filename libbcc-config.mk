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

LLVM_ROOT_PATH      := external/llvm
LIBBCC_ROOT_PATH    := frameworks/compile/libbcc
RSLOADER_ROOT_PATH  := frameworks/compile/linkloader


#=====================================================================
# Configurations
#=====================================================================

libbcc_USE_OLD_JIT                  := 0
libbcc_USE_MCJIT                    := 1

libbcc_USE_CACHE                    := 1

libbcc_DEBUG_OLD_JIT_DISASSEMBLER   := 0
libbcc_DEBUG_MCJIT_DISASSEMBLER     := 0

libbcc_USE_LOGGER                   := 1
libbcc_USE_FUNC_LOGGER              := 0
libbcc_DEBUG_BCC_REFLECT            := 0
libbcc_DEBUG_MCJIT_REFLECT          := 0


#=====================================================================
# Automatic Configurations
#=====================================================================

ifeq ($(libbcc_USE_OLD_JIT),0)
libbcc_DEBUG_OLD_JIT_DISASSEMBLER := 0
endif

ifeq ($(libbcc_USE_MCJIT),0)
libbcc_DEBUG_MCJIT_DISASSEMBLER := 0
endif

ifeq ($(libbcc_DEBUG_OLD_JIT_DISASSEMBLER)$(libbcc_DEBUG_MCJIT_DISASSEMBLER),00)
libbcc_USE_DISASSEMBLER := 0
else
libbcc_USE_DISASSEMBLER := 1
endif


#=====================================================================
# Common Variables
#=====================================================================

libbcc_CFLAGS := -Wall -Wno-unused-parameter -Werror
ifneq ($(TARGET_BUILD_VARIANT),eng)
libbcc_CFLAGS += -D__DISABLE_ASSERTS
endif

ifeq ($(TARGET_ARCH),arm)
  libbcc_CFLAGS += -DFORCE_ARM_CODEGEN=1
  ifeq (true,$(ARCH_ARM_HAVE_VFP))
    libbcc_CFLAGS += -DARCH_ARM_HAVE_VFP
    ifeq (true,$(ARCH_ARM_HAVE_VFP_D32))
      libbcc_CFLAGS += -DARCH_ARM_HAVE_VFP_D32
    endif
  endif
  ifeq (true,$(ARCH_ARM_HAVE_NEON))
    libbcc_CFLAGS += -DARCH_ARM_HAVE_NEON
  endif
else
  ifeq ($(TARGET_ARCH),x86)
    libbcc_CFLAGS += -DFORCE_X86_CODEGEN=1
  else
    $(error Unsupported TARGET_ARCH $(TARGET_ARCH))
  endif
endif

# Include File Search Path
libbcc_C_INCLUDES := \
  $(RSLOADER_ROOT_PATH)/android \
  $(LIBBCC_ROOT_PATH)/lib \
  $(LIBBCC_ROOT_PATH)/helper \
  $(LIBBCC_ROOT_PATH)/include \
  $(LIBBCC_ROOT_PATH)
