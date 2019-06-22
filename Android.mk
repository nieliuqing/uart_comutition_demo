LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := mcu_fw_update
LOCAL_SRC_FILES := $(call all-subdir-c-files)
$(info $(shell cp -vf $(LOCAL_PATH)/*.bin $(PRODUCT_OUT)/system/etc/))
include $(BUILD_EXECUTABLE)
