// 包含头文件
#include <hardware/hardware.h>

// 常量定义
#define CAM_DEV_NUM   1
#define CAM_DEV_FILE  "/dev/video0"

// 函数声明
int camdev_open (const hw_module_t* mod, const char* name, hw_device_t** dev);
int camdev_close(hw_device_t* device);
