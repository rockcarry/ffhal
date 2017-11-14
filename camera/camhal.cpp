// 包含头文件
#define LOG_TAG "ffhalcamhal"
#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/camera_common.h>
#include "camdev.h"

// 内部函数实现
static int get_number_of_cameras()
{
    return CAM_DEV_NUM;
}

static int get_camera_info(int id, struct camera_info* info)
{
    info->facing      = 0;
    info->orientation = 0;
    info->static_camera_characteristics = NULL;
    return 0;
}

#ifdef ANDROID_5_1
static int set_callbacks(const camera_module_callbacks_t *callbacks)
{
    return 0;
}

static void get_vendor_tag_ops(vendor_tag_ops_t *ops)
{
}

static int open_legacy(const struct hw_module_t *module, const char *id, uint32_t halver, struct hw_device_t **device)
{
    return 0;
}
#endif

static hw_module_methods_t g_camera_module_methods = {
    open : camdev_open
};

// 接口声明
camera_module_t HAL_MODULE_INFO_SYM __attribute__ ((visibility("default"))) = {
    common : {
        tag                : HARDWARE_MODULE_TAG,
        module_api_version : CAMERA_DEVICE_API_VERSION_1_0,
        hal_api_version    : HARDWARE_HAL_API_VERSION,
        id                 : CAMERA_HARDWARE_MODULE_ID,
        name               : "ffhal camera hal",
        author             : "rockcarry",
        methods            : &g_camera_module_methods,
        dso                : NULL,
        reserved           : {0},
    },
    get_number_of_cameras  : get_number_of_cameras,
    get_camera_info        : get_camera_info,
#ifdef ANDROID_5_1
    set_callbacks          : set_callbacks,
    get_vendor_tag_ops     : get_vendor_tag_ops,
    open_legacy            : open_legacy,
    reserved               : { NULL }
#endif
};
