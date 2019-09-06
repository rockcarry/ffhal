
#define LOG_TAG  "apical_gpshal"
#include <cutils/log.h>
#include <hardware/gps.h>
#include "nmea.h"

#define DO_USE_VAR(v) do { v = v; } while (0)
#define GPS_UART_DEV_NAME  "/dev/ttyS2"
#define APICAL_DEV_NAME    "/dev/apical"

typedef struct {
    void        *nmea;
    GpsCallbacks callbacks;
    int          start;
} GPSHALCTXT;

static GPSHALCTXT g_gps_ctxt = { NULL };

static void set_gps_hw_pwr(int pwr) {
    DO_USE_VAR(pwr);
#if 0
    int fd = open(APICAL_DEV_NAME, O_RDWR);
    char str[16];
    snprintf(str, sizeof(str), "gps %d", pwr);
    write(fd, str, strlen(str));
    close(fd);
#endif
}

static void nmea_callback(void *params, int stype, int dtype, void *data)
{
    GPSHALCTXT *halctxt  = (GPSHALCTXT*)params;
    GPS_STATUS *status   = (GPS_STATUS*)data;
    char       *rawdata  = (char*)data;
    GpsLocation location = {0};
    GpsSvStatus svstatus = {0};
    int i;

    ALOGD("nmea_callback stype: %d, dtype: %d\n", stype, dtype);
    if (halctxt->start == 0) return;
    if (dtype == GD_TYPE_RAWDATA) {
        GPS_STATUS *s = nmea_gps_status(halctxt->nmea, stype);
        halctxt->callbacks.nmea_cb(s->timestamp, rawdata, strlen(rawdata));
        return;
    }

    switch (stype) {
    case GS_TYPE_GPS:
        location.latitude  = status->latitude;
        location.longitude = status->longitude;
        location.altitude  = status->altitude;
        location.speed     = status->speed;
        location.bearing   = status->course;
        location.accuracy  = status->hdop;
        location.timestamp = status->timestamp;
        switch (status->fixstatus) {
        case GPS_FIXED:
        case GPS_FIXED_2D:
            location.flags = GPS_LOCATION_HAS_LAT_LONG;
            break;
        case GPS_FIXED_3D:
            location.flags = GPS_LOCATION_HAS_LAT_LONG|GPS_LOCATION_HAS_ALTITUDE;
            break;
        }
        location.size    = sizeof(GpsLocation);
        location.flags  |= location.speed    > 0 ? GPS_LOCATION_HAS_SPEED    : 0;
        location.flags  |= location.accuracy > 0 ? GPS_LOCATION_HAS_ACCURACY : 0;
        location.flags  |= location.bearing  > 0 ? GPS_LOCATION_HAS_BEARING  : 0;
        svstatus.size    = sizeof(GpsSvStatus);
        svstatus.num_svs = status->inview;
        for (i=0; i<status->inview&&i<GPS_MAX_SVS&&i<16; i++) {
            svstatus.sv_list[i].size     = sizeof(GpsSvInfo);
            svstatus.sv_list[i].prn      = status->satellites[i].prn;
            svstatus.sv_list[i].snr      = status->satellites[i].snr;
            svstatus.sv_list[i].elevation= status->satellites[i].elevation;
            svstatus.sv_list[i].azimuth  = status->satellites[i].azimuth;
            if (status->satellites[i].inuse) {
                svstatus.used_in_fix_mask |= (1 << (status->satellites[i].prn - 1));
            }
        }
        halctxt->callbacks.location_cb (&location);
        halctxt->callbacks.sv_status_cb(&svstatus);
        break;
    case GS_TYPE_BEIDOU:
        break;
    case GS_TYPE_GLONASS:
        break;
    }
}

static int apical_gps_init(GpsCallbacks *callbacks)
{
    ALOGD("apical_gps_init\n");
    g_gps_ctxt.callbacks = *callbacks;
    g_gps_ctxt.nmea      = nmea_init(GPS_UART_DEV_NAME, nmea_callback, g_gps_ctxt.callbacks.create_thread_cb, &g_gps_ctxt);
    return 0;
}

static int apical_gps_start(void)
{
    ALOGD("apical_gps_start\n");
    g_gps_ctxt.start = 1;
    set_gps_hw_pwr(1);
    return 0;
}

static int apical_gps_stop(void)
{
    ALOGD("apical_gps_stop\n");
    set_gps_hw_pwr(0);
    g_gps_ctxt.start = 0;
    return 0;
}

static void apical_gps_cleanup(void)
{
    ALOGD("apical_gps_cleanup\n");
    nmea_exit(g_gps_ctxt.nmea);
}

static int apical_gps_inject_time(GpsUtcTime time, int64_t timeReference, int uncertainty)
{
    DO_USE_VAR(time);
    DO_USE_VAR(timeReference);
    DO_USE_VAR(uncertainty);
    return 0;
}

static int apical_gps_inject_location(double latitude, double longitude, float accuracy)
{
    DO_USE_VAR(latitude);
    DO_USE_VAR(longitude);
    DO_USE_VAR(accuracy);
    return 0;
}

static void apical_gps_delete_aiding_data(GpsAidingData flags)
{
    DO_USE_VAR(flags);
}

static int apical_gps_set_position_mode(GpsPositionMode mode, GpsPositionRecurrence recurrence,
                uint32_t min_interval, uint32_t preferred_accuracy, uint32_t preferred_time)
{
    DO_USE_VAR(mode);
    DO_USE_VAR(recurrence);
    DO_USE_VAR(min_interval);
    DO_USE_VAR(preferred_accuracy);
    DO_USE_VAR(preferred_time);
    return 0;
}

static const void *apical_gps_get_extension(const char *name)
{
    DO_USE_VAR(name);
    return NULL;
}

static const GpsInterface apical_gps_interface = {
    sizeof(GpsInterface),
    apical_gps_init,
    apical_gps_start,
    apical_gps_stop,
    apical_gps_cleanup,
    apical_gps_inject_time,
    apical_gps_inject_location,
    apical_gps_delete_aiding_data,
    apical_gps_set_position_mode,
    apical_gps_get_extension,
};

static const GpsInterface* get_gps_interface(struct gps_device_t *dev)
{
    DO_USE_VAR(dev);
    return &apical_gps_interface;
}

static int open_gps(const struct hw_module_t *module, char const *name, struct hw_device_t ** device)
{
    struct gps_device_t *dev = calloc(1, sizeof(struct gps_device_t));
    ALOGD("open_gps: name = %s\n", name);
    dev->common.tag       = HARDWARE_DEVICE_TAG;
    dev->common.version   = 1;
    dev->common.module    = (struct hw_module_t*)module;
    dev->get_gps_interface= get_gps_interface;
    *device               = (struct hw_device_t*)dev;
    return 0;
}

static struct hw_module_methods_t gps_module_methods = {
    .open = open_gps
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag     = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id      = GPS_HARDWARE_MODULE_ID,
    .name    = "apical gps module",
    .author  = "chenk@apical.com.cn",
    .methods = &gps_module_methods,
};

