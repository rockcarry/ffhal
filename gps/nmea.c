#define LOG_TAG  "apical_gpshal"
#include <cutils/log.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include "nmea.h"

typedef struct {
    char       dev[PATH_MAX];
    GPS_STATUS gps_status;
    GPS_STATUS bds_status;
    GPS_STATUS gns_status;

    #define TS_EXIT   (1 << 0)
    uint32_t          thread_status;
    pthread_t         read_thread;
    PFN_NMEA_CALLBACK callback;
    void             *cbparams;

    #define NMEA_MAX_SIZE 128  // !!important, this size must be power of 2
    char              nmeabuf[NMEA_MAX_SIZE];
    int               nmeahead;
    int               nmeatail;
    int               nmeanum ;

    int8_t            gps_satidx;
    GPS_SATELLITE     gps_satlist[16];
    int8_t            gps_inuse[12];

    int8_t            bds_satidx;
    GPS_SATELLITE     bds_satlist[16];
    int8_t            bds_inuse[12];

    int8_t            gns_satidx;
    GPS_SATELLITE     gns_satlist[16];
    int8_t            gns_inuse[12];
} CONTEXT;

static int str2int(char *str, int len)
{
    char tmp[12];
    len = len < 11 ? len : 11;
    memcpy(tmp, str, len);
    tmp[len] = '\0';
    return atoi(tmp);
}

static double dm2ddd(double dm)
{
    int    degrees = (int)(dm / 100);
    double minutes = dm - degrees * 100;
    return degrees + minutes / 60.0;
}

static void do_nmea_parse(CONTEXT *ctxt, char *sentence)
{
    #define MAX_TOKEN_NUM  22
    char *tokens[MAX_TOKEN_NUM] = { sentence };
    GPS_STATUS    *status   = NULL;
    GPS_SATELLITE *satelist = NULL;
    int8_t        *satidx   = NULL;
    int8_t        *inuse    = NULL;
    int type, i, j;

    if (memcmp(sentence, "$GP", 3) == 0) {
        type     = GS_TYPE_GPS;
        status   =&ctxt->gps_status;
        satelist = ctxt->gps_satlist;
        satidx   =&ctxt->gps_satidx;
        inuse    = ctxt->gps_inuse;
    } else if (memcmp(sentence, "$GB", 3) == 0) {
        type     = GS_TYPE_BEIDOU;
        status   =&ctxt->bds_status;
        satelist = ctxt->bds_satlist;
        satidx   =&ctxt->bds_satidx;
        inuse    = ctxt->bds_inuse;
    } else if (memcmp(sentence, "$GN", 3) == 0) {
        type     = GS_TYPE_GLONASS;
        status   =&ctxt->gns_status;
        satelist = ctxt->gns_satlist;
        satidx   =&ctxt->gns_satidx;
        inuse    = ctxt->gns_inuse;
    } else return;

    ALOGD("%s", sentence);
    ctxt->callback(ctxt->cbparams, type, GD_TYPE_RAWDATA, sentence);

    //++ split sentence to tokens
    for (i=0,j=1; sentence[i]&&j<MAX_TOKEN_NUM; i++) {
        if (sentence[i] == ',' || sentence[i] == '*') {
            sentence[i] = '\0';
            tokens[j++] = sentence + i + 1;
        }
    }
    for (; j<MAX_TOKEN_NUM; j++) tokens[j] = "";
    //-- split sentence to tokens

    tokens[0] += 3;
    if (memcmp(tokens[0], "RMC", 3) == 0) {
        status->latitude  = (tokens[4][0] == 'S' ? -1 : 1) * dm2ddd(strtod(tokens[3], NULL));
        status->longitude = (tokens[6][0] == 'W' ? -1 : 1) * dm2ddd(strtod(tokens[5], NULL));
        status->speed     = tokens[7][0] ? (float)(strtod(tokens[7], NULL) * 1.852 / 3.6) : -1;
        status->course    = tokens[8][0] ? (float)(strtod(tokens[8], NULL)) : -1;
        status->datetime.tm_year = str2int(tokens[9] + 4, 2) + 2000 - 1900;
        status->datetime.tm_mon  = str2int(tokens[9] + 2, 2) - 1;
        status->datetime.tm_mday = str2int(tokens[9] + 0, 2);
        status->datetime.tm_hour = str2int(tokens[1] + 0, 2);
        status->datetime.tm_min  = str2int(tokens[1] + 2, 2);
        status->datetime.tm_sec  = str2int(tokens[1] + 4, 2);
        if (tokens[2][0] == 'V') {
            status->fixstatus = 0;
        } else if (status->fixstatus == 0) {
            status->fixstatus = GPS_FIXED;
        }
    } else if (memcmp(tokens[0], "GGA", 3) == 0) {
        status->datetime.tm_hour = str2int(tokens[1] + 0, 2);
        status->datetime.tm_min  = str2int(tokens[1] + 2, 2);
        status->datetime.tm_sec  = str2int(tokens[1] + 4, 2);
        status->latitude  = (tokens[3][0] == 'S' ? -1 : 1) * dm2ddd(strtod(tokens[2], NULL));
        status->longitude = (tokens[5][0] == 'W' ? -1 : 1) * dm2ddd(strtod(tokens[4], NULL));
        status->hdop      = (float)strtod(tokens[8], NULL);
        status->altitude  = (float)strtod(tokens[9], NULL);
        status->inuse     = atoi(tokens[7]);
    } else if (memcmp(tokens[0], "GSA", 3) == 0) {
        status->fixstatus = tokens[2][0] > '1' ? tokens[2][0] - '0' : 0;
        status->pdop      = (float)strtod(tokens[15], NULL);
        status->hdop      = (float)strtod(tokens[16], NULL);
        status->vdop      = (float)strtod(tokens[17], NULL);
        for (i=0; i<12; i++) inuse[i] = tokens[3+i][0] ? atoi(tokens[3+i]) : -1;
    } else if (memcmp(tokens[0], "GSV", 3) == 0) {
        if (atoi(tokens[2]) == 1) {
            memset(satelist, 0, sizeof(GPS_SATELLITE) * 16);
            *satidx = 0;
        }
        status->inview = atoi(tokens[3]);
        status->inview = status->inview < 16 ? status->inview : 16;
        for (i=0; i<4; i++) {
            if (*satidx >= 16) break;
            satelist[*satidx].prn       = tokens[i*4+4][0] ? atoi(tokens[i*4+4]) : -1;
            satelist[*satidx].elevation = tokens[i*4+5][0] ? atoi(tokens[i*4+5]) : -1;
            satelist[*satidx].azimuth   = tokens[i*4+6][0] ? atoi(tokens[i*4+6]) : -1;
            satelist[*satidx].snr       = tokens[i*4+7][0] ? atoi(tokens[i*4+7]) :  0;
            for (j=0; j<12; j++) {
                if (inuse[j] == satelist[*satidx].prn) {
                    satelist[*satidx].inuse = 1;
                }
            }
            (*satidx)++;
        }
        if (atoi(tokens[1]) == atoi(tokens[2])) {
            memcpy(status->satellites, satelist, sizeof(GPS_SATELLITE) * 16);
            ctxt->callback(ctxt->cbparams, type, GD_TYPE_STATUS, status);
        }
    }

    if (memcmp(tokens[0], "RMC", 3) == 0 || memcmp(tokens[0], "GGA", 3) == 0) {
        time_t   utctime  = timegm(&status->datetime);
        struct tm loc_tm  = {0};
        localtime_r(&utctime, &loc_tm);
        status->timestamp = (int64_t)mktime(&loc_tm) * 1000;
    }
}

static void add_nmea_buf(CONTEXT *ctxt, char *buf, int n)
{
    int i;
    for (i=0; i<n; i++) {
        //++ enqueue a char to nmeabuf
        ctxt->nmeabuf[ctxt->nmeatail++] = buf[i];
        ctxt->nmeatail &= (NMEA_MAX_SIZE - 1);
        if (ctxt->nmeanum < NMEA_MAX_SIZE) {
            ctxt->nmeanum++;
        } else {
            ctxt->nmeahead++;
            ctxt->nmeahead &= (NMEA_MAX_SIZE - 1);
        }
        //-- enqueue a char to nmeabuf

        //++ dequeue a nmea sentence
        if (buf[i] == '\n') {
            char sentence[NMEA_MAX_SIZE+1];
            int  j = 0;
            while (ctxt->nmeanum > 0) {
                sentence[j++] = ctxt->nmeabuf[ctxt->nmeahead++];
                ctxt->nmeahead &= (NMEA_MAX_SIZE - 1);
                ctxt->nmeanum--;
            }
            sentence[j] = '\0';
            do_nmea_parse(ctxt, sentence);
        }
        //-- dequeue a nmea sentence
    }
}

static int open_serial_port(char *name)
{
    struct termios termattr;
    int    ret;
    int    fd ;

    fd = open(name, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        ALOGD("failed to open serial port: %s !\n", name);
        return fd;
    }

    ret = tcgetattr(fd, &termattr);
    if (ret < 0) {
        ALOGD("tcgetattr failed !\n");
    }

    cfmakeraw(&termattr);
    cfsetospeed(&termattr, B9600);
    cfsetispeed(&termattr, B9600);

    termattr.c_cflag &= ~CRTSCTS; // flow ctrl
    termattr.c_cflag &= ~CSIZE;   // data size
    termattr.c_cflag |=  CS8;
    termattr.c_cflag &= ~PARENB;  // no parity
    termattr.c_iflag &= ~INPCK;
    termattr.c_cflag &= ~CSTOPB;  // stopbits 1

    tcsetattr(fd, TCSANOW, &termattr);
    tcflush  (fd, TCIOFLUSH);
    return fd;
}

static void* read_thread_proc(void *param)
{
    CONTEXT *ctxt = (CONTEXT*)param;
    int nodata = 0, fd = -1;

    while (!(ctxt->thread_status & TS_EXIT)) {
        struct timeval tv;
        fd_set fds;
        int    ret;

        if (nodata == 5) {
            ALOGD("read_thread_proc no gps data, clear gps status !\n");
            memset(&ctxt->gps_status, 0, sizeof(GPS_STATUS)); ctxt->callback(ctxt->cbparams, GS_TYPE_GPS    , GD_TYPE_STATUS, &ctxt->gps_status);
            memset(&ctxt->bds_status, 0, sizeof(GPS_STATUS)); ctxt->callback(ctxt->cbparams, GS_TYPE_BEIDOU , GD_TYPE_STATUS, &ctxt->bds_status);
            memset(&ctxt->gns_status, 0, sizeof(GPS_STATUS)); ctxt->callback(ctxt->cbparams, GS_TYPE_GLONASS, GD_TYPE_STATUS, &ctxt->gns_status);
        }

        if (fd == -1) {
//          fd = open(ctxt->dev, O_RDONLY);
            fd = open_serial_port(ctxt->dev);
        }
        if (fd <= 0) {
            usleep(100*1000);
            continue;
        }

        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        /* timeout */
        tv.tv_sec  = 1;
        tv.tv_usec = 0;

        ret = select(fd + 1, &fds, NULL, NULL, &tv);
        switch (ret) {
        case  0: ALOGD("select again !\n"); nodata++; continue;
        case -1: ALOGD("select error !\n"); goto end;
        }
        if (FD_ISSET(fd, &fds)) {
            char buf[32];
            int  n = read(fd, buf, sizeof(buf));
            add_nmea_buf(ctxt, buf, n);
        }
        nodata = 0;
    }

end:
    if (fd != -1) {
        close(fd);
    }
    return NULL;
}

void* nmea_init(char *dev, PFN_NMEA_CALLBACK cb, PFN_CREATE_THREAD pfnct, void *params)
{
    CONTEXT *ctxt = calloc(1, sizeof(CONTEXT));
    if (!ctxt) return NULL;

    strcpy(ctxt->dev, dev);
    ctxt->callback = cb;
    ctxt->cbparams = params;
    if (pfnct) {
        ctxt->read_thread = pfnct("nmea_read_thread", (void (*)(void*))read_thread_proc, ctxt);
    } else {
        pthread_create(&ctxt->read_thread, NULL, read_thread_proc, ctxt);
    }
    return ctxt;
}

void nmea_exit(void *ctx)
{
    CONTEXT *ctxt = (CONTEXT*)ctx;
    ctxt->thread_status |= TS_EXIT;
    pthread_join(ctxt->read_thread, NULL);
    free(ctxt);
}

void* nmea_gps_status(void *ctx, int type)
{
    CONTEXT *ctxt = (CONTEXT*)ctx;
    switch (type) {
    case GS_TYPE_GPS    : return &ctxt->gps_status;
    case GS_TYPE_BEIDOU : return &ctxt->bds_status;
    case GS_TYPE_GLONASS: return &ctxt->gns_status;
    }
    return NULL;
}
