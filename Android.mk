ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH := $(call my-dir)
LLVM_ROOT_PATH := external/llvm/llvm

USE_DISASSEMBLER := true

# Shared library for target
# ========================================================
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE := libbcc
LOCAL_SRC_FILES :=	\
	bcc.cpp	\
	bcc_runtime.c	\
	runtime/lib/arm/adddf3vfp.S	\
	runtime/lib/arm/addsf3vfp.S	\
	runtime/lib/arm/divdf3vfp.S	\
	runtime/lib/arm/divsf3vfp.S	\
	runtime/lib/arm/eqdf2vfp.S	\
	runtime/lib/arm/eqsf2vfp.S	\
	runtime/lib/arm/extendsfdf2vfp.S	\
	runtime/lib/arm/fixdfsivfp.S	\
	runtime/lib/arm/fixsfsivfp.S	\
	runtime/lib/arm/fixunsdfsivfp.S	\
	runtime/lib/arm/fixunssfsivfp.S	\
	runtime/lib/arm/floatsidfvfp.S	\
	runtime/lib/arm/floatsisfvfp.S	\
	runtime/lib/arm/floatunssidfvfp.S	\
	runtime/lib/arm/floatunssisfvfp.S	\
	runtime/lib/arm/gedf2vfp.S	\
	runtime/lib/arm/gesf2vfp.S	\
	runtime/lib/arm/gtdf2vfp.S	\
	runtime/lib/arm/gtsf2vfp.S	\
	runtime/lib/arm/ledf2vfp.S	\
	runtime/lib/arm/lesf2vfp.S	\
	runtime/lib/arm/ltdf2vfp.S	\
	runtime/lib/arm/ltsf2vfp.S	\
	runtime/lib/arm/muldf3vfp.S	\
	runtime/lib/arm/mulsf3vfp.S	\
	runtime/lib/arm/nedf2vfp.S	\
	runtime/lib/arm/negdf2vfp.S	\
	runtime/lib/arm/negsf2vfp.S	\
	runtime/lib/arm/nesf2vfp.S	\
	runtime/lib/arm/subdf3vfp.S	\
	runtime/lib/arm/subsf3vfp.S	\
	runtime/lib/arm/truncdfsf2vfp.S	\
	runtime/lib/arm/unorddf2vfp.S	\
	runtime/lib/arm/unordsf2vfp.S

LOCAL_STATIC_LIBRARIES :=	\
	libLLVMARMCodeGen	\
	libLLVMARMInfo	\
	libLLVMBitReader	\
	libLLVMSelectionDAG	\
	libLLVMAsmPrinter	\
	libLLVMCodeGen	\
	libLLVMJIT	\
	libLLVMTarget	\
	libLLVMMC	\
	libLLVMScalarOpts	\
	libLLVMTransformUtils	\
	libLLVMCore	\
	libLLVMSupport	\
	libLLVMSystem	\
	libLLVMAnalysis

LOCAL_SHARED_LIBRARIES := libdl libcutils libstlport

LOCAL_C_INCLUDES :=	\
	$(LOCAL_PATH)/include

ifeq ($(USE_DISASSEMBLER),true)
LOCAL_CFLAGS += -DUSE_DISASSEMBLER
LOCAL_STATIC_LIBRARIES :=	\
	libLLVMARMDisassembler	\
	libLLVMARMAsmPrinter	\
	libLLVMMCParser	\
	$(LOCAL_STATIC_LIBRARIES)
endif

# This hides most of the symbols in the shared library and reduces the size
# of libbcc.so by about 800k.
LOCAL_LDFLAGS += -Wl,--exclude-libs=ALL

include $(LLVM_ROOT_PATH)/llvm-device-build.mk
include $(BUILD_SHARED_LIBRARY)

# Shared library for host
# ========================================================
include $(CLEAR_VARS)

LOCAL_MODULE := libbcc
LOCAL_SRC_FILES := bcc.cpp bcc_runtime.c

LOCAL_STATIC_LIBRARIES :=	\
	libcutils	\
	libLLVMX86CodeGen	\
	libLLVMX86Info	\
	libLLVMBitReader	\
	libLLVMSelectionDAG	\
	libLLVMAsmPrinter	\
	libLLVMMCParser	\
	libLLVMCodeGen	\
	libLLVMJIT	\
	libLLVMTarget	\
	libLLVMMC	\
	libLLVMScalarOpts	\
	libLLVMTransformUtils	\
	libLLVMCore	\
	libLLVMSupport	\
	libLLVMSystem	\
	libLLVMAnalysis

LOCAL_LDLIBS := -ldl -lpthread

LOCAL_C_INCLUDES :=	\
	$(LOCAL_PATH)/include

ifeq ($(USE_DISASSEMBLER),true)
LOCAL_CFLAGS += -DUSE_DISASSEMBLER
LOCAL_STATIC_LIBRARIES :=	\
	libLLVMX86Disassembler	\
	libLLVMX86AsmPrinter	\
	$(LOCAL_STATIC_LIBRARIES)
endif

include $(LLVM_ROOT_PATH)/llvm-host-build.mk
include $(BUILD_HOST_SHARED_LIBRARY)

# Build children
# ========================================================

include $(call all-makefiles-under,$(LOCAL_PATH))

endif # TARGET_SIMULATOR != true
