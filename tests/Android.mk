LOCAL_PATH := $(call my-dir)

# Executable for host
# ========================================================
include $(CLEAR_VARS)

LOCAL_MODULE := bcc

LOCAL_SRC_FILES:= \
	main.cpp

LOCAL_SHARED_LIBRARIES := \
    libbcc

LOCAL_C_INCLUDES :=	\
	$(LOCAL_PATH)/../include

#LOCAL_MODULE_TAGS := tests

include $(BUILD_HOST_EXECUTABLE)

# Executable for target
# ========================================================
include $(CLEAR_VARS)

LOCAL_MODULE:= bcc

LOCAL_SRC_FILES:= \
	main.cpp \
    disassem.cpp

LOCAL_SHARED_LIBRARIES := \
    libbcc

LOCAL_C_INCLUDES :=	\
	$(LOCAL_PATH)/../include

#LOCAL_MODULE_TAGS := tests

include $(BUILD_EXECUTABLE)
