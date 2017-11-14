# Copyright (C) 2012 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := camera.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH := hw

LOCAL_C_INCLUDES += \
    system/core/include \
    system/media/camera/include \

LOCAL_SRC_FILES := \
    v4l2dev.cpp \
    camdev.cpp \
    camhal.cpp

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libui

LOCAL_CFLAGS += -Wall -Wextra -fvisibility=hidden
#LOCAL_CFLAGS += -DANDROID_5_1

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

