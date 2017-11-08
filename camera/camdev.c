// 包含头文件
#define LOG_TAG "ffhalcamdev"
#include <stdlib.h>
#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/camera_common.h>
#include <hardware/camera.h>
#include "camdev.h"

// 内部类型定义
typedef struct {
    hw_device_t       common;
    camera_device_ops_t *ops;
    void               *priv;

    //++ camera device context
    int                             cameraid;
    preview_stream_ops_t           *window;
    camera_notify_callback          cb_notify;
    camera_data_callback            cb_data;
    camera_data_timestamp_callback  cb_timestamp;
    camera_request_memory           cb_memory;
    void                           *cb_user;
    int32_t                         msg_enabler;
    //-- camera device context
} ffhal_camera_device_t;

// 内部函数实现
static int camdev_set_preview_window(struct camera_device *dev, struct preview_stream_ops *window)
{
    ffhal_camera_device_t *cam = (ffhal_camera_device_t*)dev;
    cam->window = window;
    return 0;
}

static void camdev_set_callbacks(struct camera_device *dev,
                    camera_notify_callback notify_cb,
                    camera_data_callback data_cb,
                    camera_data_timestamp_callback data_cb_timestamp,
                    camera_request_memory get_memory,
                    void *user)
{
    ffhal_camera_device_t *cam = (ffhal_camera_device_t*)dev;
    cam->cb_notify    = notify_cb;
    cam->cb_data      = data_cb;
    cam->cb_timestamp = data_cb_timestamp;
    cam->cb_memory    = get_memory;
    cam->cb_user      = user;
}

static void camdev_enable_msg_type(struct camera_device *dev, int32_t msg_type)
{
    ffhal_camera_device_t *cam = (ffhal_camera_device_t*)dev;
    cam->msg_enabler |= msg_type;
}

static void camdev_disable_msg_type(struct camera_device *dev, int32_t msg_type)
{
    ffhal_camera_device_t *cam = (ffhal_camera_device_t*)dev;
    cam->msg_enabler &= ~msg_type;
}

static int camdev_msg_type_enabled(struct camera_device *dev, int32_t msg_type)
{
    ffhal_camera_device_t *cam = (ffhal_camera_device_t*)dev;
    return (cam->msg_enabler & msg_type);
}

static int camdev_start_preview(struct camera_device *dev)
{
    return 0;
}

static void camdev_stop_preview(struct camera_device *dev)
{
}

static int camdev_preview_enabled(struct camera_device *dev)
{
    return 0;
}

static int camdev_enable_preview(struct camera_device *dev)
{
    return 0;
}

static int camdev_disable_preview(struct camera_device *dev)
{
    return 0;
}

static int camdev_store_meta_data_in_buffers(struct camera_device *dev, int enable)
{
    return 0;
}

static int camdev_start_recording(struct camera_device *dev)
{
    return 0;
}

static void camdev_stop_recording(struct camera_device *dev)
{
}

static int camdev_recording_enabled(struct camera_device *dev)
{
    return 0;
}

static void camdev_release_recording_frame(struct camera_device *dev, const void *opaque)
{
}

static int camdev_auto_focus(struct camera_device *dev)
{
    return 0;
}

static int camdev_cancel_auto_focus(struct camera_device *dev)
{
    return 0;
}

static int camdev_take_picture(struct camera_device *dev)
{
    return 0;
}

static int camdev_cancel_picture(struct camera_device *dev)
{
    return 0;
}

static int camdev_set_parameters(struct camera_device *dev, const char *parms)
{
    return 0;
}

static char* camdev_get_parameters(struct camera_device *dev)
{
    return NULL;
}

static void camdev_put_parameters(struct camera_device *dev, char *params)
{
}

static int camdev_send_command(struct camera_device *dev, int32_t cmd, int32_t arg1, int32_t arg2)
{
    switch (cmd)
    {
    case CAMERA_CMD_PING:
        break;
    case CAMERA_CMD_SET_DISPLAY_ORIENTATION:
        break;
    case CAMERA_CMD_SET_CEDARX_RECORDER:
        break;
    case CAMERA_CMD_ENABLE_FOCUS_MOVE_MSG:
        break;
    case CAMERA_CMD_START_FACE_DETECTION:
        break;
    case CAMERA_CMD_STOP_FACE_DETECTION:
        break;
    case CAMERA_CMD_START_SMART_DETECTION:
        break;
    case CAMERA_CMD_STOP_SMART_DETECTION:
        break;
    }
    return 0;
}

static void camdev_release(struct camera_device *dev)
{
}

static int camdev_dump(struct camera_device *dev, int fd)
{
    return 0;
}

static int camdev_setfd(struct camera_device *dev, int fd)
{
    return 0;
}

static camera_device_ops_t g_camdev_ops = {
    camdev_set_preview_window,
    camdev_set_callbacks,
    camdev_enable_msg_type,
    camdev_disable_msg_type,
    camdev_msg_type_enabled,
    camdev_start_preview,
    camdev_stop_preview,
    camdev_preview_enabled,
    camdev_enable_preview,
    camdev_disable_preview,
    camdev_store_meta_data_in_buffers,
    camdev_start_recording,
    camdev_stop_recording,
    camdev_recording_enabled,
    camdev_release_recording_frame,
    camdev_auto_focus,
    camdev_cancel_auto_focus,
    camdev_take_picture,
    camdev_cancel_picture,
    camdev_set_parameters,
    camdev_get_parameters,
    camdev_put_parameters,
    camdev_send_command,
    camdev_release,
    camdev_dump,
    camdev_setfd,
};

int camdev_open(const hw_module_t *mod, const char *name, hw_device_t **dev)
{
    int id = atoi(name);
    ffhal_camera_device_t *camdev = NULL;
    camera_device_ops_t   *camops = NULL;

    if (id >= CAM_DEV_NUM) {
        ALOGD("cameraid out of bounds, cameraid = %d, num supported = %d", id, CAM_DEV_NUM);
        return -EINVAL;
    }

    camdev = (ffhal_camera_device_t*) calloc(1, sizeof(ffhal_camera_device_t));
    if (!camdev) {
        ALOGD("failed to allocate memory for camera device !");
        return -ENOMEM;
    }

    camdev->common.tag     = HARDWARE_DEVICE_TAG;
    camdev->common.version = 0;
    camdev->common.module  = (hw_module_t*) mod;
    camdev->common.close   = camdev_close;
    camdev->ops            = &g_camdev_ops;
    camdev->cameraid       = id;

    *dev = &camdev->common;
    return 0;
}

int camdev_close(hw_device_t *device)
{
    return 0;
}
