#define LOG_TAG  "sensorshal"
#include <stdlib.h>
#include <string.h>
#include <cutils/log.h>
#include <hardware/sensors.h>

static struct sensor_t g_sensor_list[] = {
    { "fake_accel"    , "ffhal", 1, 1, SENSOR_TYPE_ACCELEROMETER    , 1.0, 1.0 / 0x10000, 50, 50, 0, 0, SENSOR_STRING_TYPE_ACCELEROMETER    , 0, 0, SENSOR_FLAG_WAKE_UP, {} },
    { "fake_light"    , "ffhal", 1, 2, SENSOR_TYPE_LIGHT            , 1.0, 1.0 / 0x10000, 50, 50, 0, 0, SENSOR_STRING_TYPE_LIGHT            , 0, 0, SENSOR_FLAG_WAKE_UP, {} },
    { "fake_proximity", "ffhal", 1, 3, SENSOR_TYPE_PROXIMITY        , 1.0, 1.0 / 0x10000, 50, 50, 0, 0, SENSOR_STRING_TYPE_PROXIMITY        , 0, 0, SENSOR_FLAG_WAKE_UP, {} },
    { "fake_temp"     , "ffhal", 1, 4, SENSOR_TYPE_TEMPERATURE      , 1.0, 1.0 / 0x10000, 50, 50, 0, 0, SENSOR_STRING_TYPE_TEMPERATURE      , 0, 0, SENSOR_FLAG_WAKE_UP, {} },
    { "fake_humidity" , "ffhal", 1, 5, SENSOR_TYPE_RELATIVE_HUMIDITY, 1.0, 1.0 / 0x10000, 50, 50, 0, 0, SENSOR_STRING_TYPE_RELATIVE_HUMIDITY, 0, 0, SENSOR_FLAG_WAKE_UP, {} },
};

static int poll_close(struct hw_device_t *dev)
{
    ALOGD("poll_close dev: %p\n", dev);
    return 0;
}

static int poll_activate(struct sensors_poll_device_t *dev, int handle, int enabled) {
    ALOGD("poll_activate dev: %p, handle: %d, enabled: %d\n", dev, handle, enabled);
    return 0;
}

static int poll_setDelay(struct sensors_poll_device_t *dev, int handle, int64_t ns) {
    ALOGD("poll_setDelay dev: %p, handle: %d, ns: %lld\n", dev, handle, (long long)ns);
    return 0;
}

static int poll_poll(struct sensors_poll_device_t *dev, sensors_event_t *data, int count) {
    ALOGD("poll_poll dev: %p, data: %p, count: %d\n", dev, data, count);
    data[0].version        = 1;
    data[0].sensor         = 0;
    data[0].type           = SENSOR_TYPE_ACCELEROMETER;
    data[0].timestamp      = 0;
    data[0].acceleration.x = 0;
    data[0].acceleration.y = 0;
    data[0].acceleration.z = 0;
    data[0].acceleration.status = SENSOR_STATUS_ACCURACY_HIGH;
    usleep(1000*1000);
    return 0;
}

static int open_sensors(const struct hw_module_t *module, const char *name, struct hw_device_t **device)
{
    struct sensors_poll_device_t *dev = calloc(1, sizeof(struct sensors_poll_device_t));

    ALOGD("open_sensors module: %p, name: %s, device: %p\n", module, name, device);
    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version  = 1;
    dev->common.module   = (struct hw_module_t*)module;
    dev->common.close    = poll_close;
    dev->activate        = poll_activate;
    dev->setDelay        = poll_setDelay;
    dev->poll            = poll_poll;

    *device = &dev->common;
    return 0;
}

static struct hw_module_methods_t sensors_module_methods = {
	.open = open_sensors,
};

static int sensors_get_sensors_list(struct sensors_module_t *module, struct sensor_t const **list)
{
    ALOGD("sensors_get_sensors_list module: %p\n", module);
	*list = g_sensor_list;
	return sizeof(g_sensor_list)/sizeof(g_sensor_list[0]);
};

struct sensors_module_t HAL_MODULE_INFO_SYM = {
    .common =  {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 1,
        .id      = SENSORS_HARDWARE_MODULE_ID,
        .name    = "sensor module",
        .author  = "rockcarry",
        .methods = &sensors_module_methods,
    },
    .get_sensors_list = sensors_get_sensors_list,
};
