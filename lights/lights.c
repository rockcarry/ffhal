#define LOG_TAG "lights"
#include <cutils/log.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <hardware/lights.h>
#include <hardware/hardware.h>

// 常量定义
#define DISP_CMD_LCD_SET_BRIGHTNESS  0x102
#define DO_USE_VAR(v)  do { v = v; } while (0)

// 内部函数实现
static int rgb_to_brightness(struct light_state_t const *state)
{
    int color = state->color & 0x00ffffff;
    int r = (state->color >> 0 ) & 0xff;
    int g = (state->color >> 8 ) & 0xff;
    int b = (state->color >> 16) & 0xff;
    int brightness = (r * 77 + g * 150 + b * 29) / 256;
    return brightness;
}

static int set_light_backlight(struct light_device_t *dev, struct light_state_t const *state)
{
    int bright = rgb_to_brightness(state);
    int fd     = open("/dev/disp", O_RDONLY);;
    int err    = 0;
    unsigned long args[3];

    DO_USE_VAR(dev  );
    DO_USE_VAR(state);

    // check fd
    if (fd < 0) return -1;

    // handle brightness
    if (bright) {
        bright = 10 + 160 * bright / 255;
    }

    args[0] = 0;
    args[1] = bright;
    args[2] = 0;
    err = ioctl(fd, DISP_CMD_LCD_SET_BRIGHTNESS, args);
    close(fd);
    return err;
}

static int set_light_keyboard(struct light_device_t *dev, struct light_state_t const *state)
{
    DO_USE_VAR(dev  );
    DO_USE_VAR(state);
    return 0;
}

static int set_light_buttons(struct light_device_t *dev, struct light_state_t const *state)
{
    DO_USE_VAR(dev  );
    DO_USE_VAR(state);
    return 0;
}

static int set_light_battery(struct light_device_t *dev, struct light_state_t const *state)
{
    DO_USE_VAR(dev  );
    DO_USE_VAR(state);
    return 0;
}

static int set_light_notifications(struct light_device_t *dev, struct light_state_t const *state)
{
    DO_USE_VAR(dev  );
    DO_USE_VAR(state);
    return 0;
}

static int set_light_attention(struct light_device_t *dev, struct light_state_t const *state)
{
    DO_USE_VAR(dev  );
    DO_USE_VAR(state);
    return 0;
}

/** Close the lights device */
static int close_lights(struct hw_device_t *dev)
{
    if (dev) {
        free(dev);
    }
    return 0;
}

/** Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t *module, char const *name,
               struct hw_device_t **device)
{
    void* set_light = NULL;
    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name)) {
        set_light = set_light_backlight;
    } else if (0 == strcmp(LIGHT_ID_KEYBOARD, name)) {
        set_light = set_light_keyboard;
    } else if (0 == strcmp(LIGHT_ID_BUTTONS, name)) {
        set_light = set_light_buttons;
    } else if (0 == strcmp(LIGHT_ID_BATTERY, name)) {
        set_light = set_light_battery;
    } else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name)) {
        set_light = set_light_notifications;
    } else if (0 == strcmp(LIGHT_ID_ATTENTION, name)) {
        set_light = set_light_attention;
    } else {
        return -EINVAL;
    }

    struct light_device_t *dev = calloc(1, sizeof(struct light_device_t));
    dev->common.tag     = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module  = (struct hw_module_t *)module;
    dev->common.close   = close_lights;
    dev->set_light      = set_light;

    *device = (struct hw_device_t *)dev;
    return 0;
}

static struct hw_module_methods_t lights_methods =
{
    .open = open_lights,
};

/*
 * The backlight Module
 */
struct hw_module_t HAL_MODULE_INFO_SYM =
{
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "ffhal lights Module",
    .author = "rockcarry",
    .methods = &lights_methods,
};

