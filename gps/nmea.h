#ifndef __NMEA_H__
#define __NMEA_H__

#include <stdint.h>
#include <time.h>

typedef struct {
    int8_t  prn;
    int8_t  snr;
    int16_t elevation;
    int16_t azimuth;
    int8_t  isinuse;
} GPS_SATELLITE;

typedef struct {
    #define   GPS_FIXED     1
    #define   GPS_FIXED_2D  2
    #define   GPS_FIXED_3D  3
    uint32_t  fixstatus;
    double    latitude ;
    double    longitude;
    float     altitude ;
    struct tm datetime ;
    int64_t   timestamp;
    float     speed  ;
    float     course ;
    float     pdop   ;
    float     hdop   ;
    float     vdop   ;
    int8_t    ninuse ;
    int8_t    ninview;
    #define MAX_SATELLITES_NUM  32
    GPS_SATELLITE satellites[MAX_SATELLITES_NUM];
} GPS_STATUS;

enum {
    GS_TYPE_GPS,
    GS_TYPE_BEIDOU,
    GS_TYPE_GLONASS,
};

enum {
    GD_TYPE_LOCATION,
    GD_TYPE_SATELLITES,
    GD_TYPE_RAWDATA,
};

typedef pthread_t (*PFN_CREATE_THREAD)(const char *name, void (*start)(void*), void *arg);
typedef void (*PFN_NMEA_CALLBACK)(void *params, int stype, int dtype, void *data);

void* nmea_init(char *dev, PFN_NMEA_CALLBACK cb, PFN_CREATE_THREAD pfnct, void *params);
void  nmea_exit(void *ctx);
GPS_STATUS* nmea_gps_status(void *ctx, int type);

#endif

