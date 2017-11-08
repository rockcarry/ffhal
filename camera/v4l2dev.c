#define LOG_TAG "v4l2dev"

// 包含头文件
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <system/graphics.h>
#include <utils/Log.h>
#include "v4l2dev.h"

// 内部常量定义
#define DO_USE_VAR(v)   do { v = v; } while (0)
#define VIDEO_CAPTURE_BUFFER_COUNT  3
#define NATIVE_WIN_BUFFER_COUNT     3
#define DEF_WIN_PIX_FMT         HAL_PIXEL_FORMAT_YCrCb_420_SP // HAL_PIXEL_FORMAT_RGBX_8888 or HAL_PIXEL_FORMAT_YCrCb_420_SP
#define V4L2DEV_GRALLOC_USAGE   (GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_NEVER | GRALLOC_USAGE_HW_TEXTURE)

// 内部类型定义
struct video_buffer {
    void    *addr;
    unsigned len;
};

// v4l2dev context
typedef struct {
    struct v4l2_buffer       buf;
    struct video_buffer      vbs[VIDEO_CAPTURE_BUFFER_COUNT];
    int                      fd;
    void                    *window;
    int                      win_w;
    int                      win_h;
    #define V4L2DEV_TS_EXIT       (1 << 0)
    #define V4L2DEV_TS_PAUSE      (1 << 1)
    #define V4L2DEV_TS_PREVIEW    (1 << 2)
    sem_t                    sem_render;
    pthread_t                thread_id_render;
    pthread_t                thread_id_capture;
    int                      thread_state;
    int                      update_flag;
    int                      cam_pixfmt;
    int                      cam_stride;
    int                      cam_w;
    int                      cam_h;
    int                      cam_frate_num; // camera frame rate num get from v4l2 interface
    int                      cam_frate_den; // camera frame rate den get from v4l2 interface
    V4L2DEV_CAPTURE_CALLBACK callback;
} V4L2DEV;

// 内部函数实现
static void render_v4l2(V4L2DEV *dev,
                        void *dstbuf, int dstlen, int dstfmt, int dstw, int dsth,
                        void *srcbuf, int srclen, int srcfmt, int srcw, int srch, int pts)
{
    DO_USE_VAR(pts);

    if (dstfmt != DEF_WIN_PIX_FMT) {
        return;
    }

//  ALOGD("srcfmt = 0x%0x, srcw = %d, srch = %d, srclen = %d, sws_src_fmt = 0x%0x\n", srcfmt, srcw, srch, srclen, sws_src_fmt);
//  ALOGD("dstfmt = 0x%0x, dstw = %d, dsth = %d, dstlen = %d, sws_dst_fmt = 0x%0x\n", dstfmt, dstw, dsth, dstlen, sws_dst_fmt);

    // memcpy if same fmt and size
    if (srcfmt == dstfmt && srcw == dstw && srch == dsth) {
//      ALOGD("===ck=== sws_src_fmt = 0x%0x, sws_dst_fmt = 0x%0x", sws_src_fmt, sws_dst_fmt);
//      memcpy(dstbuf, (uint8_t*)srcbuf + 0, (dstlen < srclen ? dstlen : srclen) - 0);
        memcpy(dstbuf, (uint8_t*)srcbuf + 1, (dstlen < srclen ? dstlen : srclen) - 1);
        return;
    } else {
        // todo...
    }
}

static void* v4l2dev_capture_thread_proc(void *param)
{
    V4L2DEV  *dev = (V4L2DEV*)param;
    int       err;

    //++ for select
    fd_set        fds;
    struct timeval tv;
    //-- for select

    while (!(dev->thread_state & V4L2DEV_TS_EXIT)) {
        FD_ZERO(&fds);
        FD_SET (dev->fd, &fds);
        tv.tv_sec  = 1;
        tv.tv_usec = 0;
        if (select(dev->fd + 1, &fds, NULL, NULL, &tv) <= 0) {
            ALOGD("select error or timeout !\n");
            continue;
        }

        if (dev->thread_state & V4L2DEV_TS_EXIT) {
            break;
        } else if (dev->thread_state & V4L2DEV_TS_PAUSE) {
            usleep(10*1000);
            continue;
        }

        // dequeue camera video buffer
        if (-1 == ioctl(dev->fd, VIDIOC_DQBUF, &dev->buf)) {
            ALOGD("failed to de-queue buffer !\n");
            continue;
        }

//      ALOGD("%d. bytesused: %d, sequence: %d, length = %d\n", dev->buf.index, dev->buf.bytesused,
//              dev->buf.sequence, dev->buf.length);
//      ALOGD("timestamp: %ld, %ld\n", dev->buf.timestamp.tv_sec, dev->buf.timestamp.tv_usec);

        if (dev->thread_state & V4L2DEV_TS_PREVIEW) {
            sem_post(&dev->sem_render);
        }

        if (dev->callback) {
            int      camw = dev->cam_w;
            int      camh = dev->cam_h;
            uint8_t *cbuf = (uint8_t*)dev->vbs[dev->buf.index].addr;
            void    *data[8]     = { (uint8_t*)cbuf, (uint8_t*)cbuf + camw * camh, (uint8_t*)cbuf + camw * camh };
            int      linesize[8] = { camw, camw / 1, camw / 1 };
            int      pts  = (int)(dev->buf.timestamp.tv_sec * 1000 + dev->buf.timestamp.tv_usec / 1000);
            if (dev->cam_pixfmt == V4L2_PIX_FMT_YUYV) {
                linesize[0] = camw * 2;
            }
            dev->callback(dev, data, linesize, pts);
        }

        // requeue camera video buffer
        if (-1 == ioctl(dev->fd, VIDIOC_QBUF , &dev->buf)) {
            ALOGD("failed to en-queue buffer !\n");
        }
    }

//  ALOGD("v4l2dev_capture_thread_proc exited !");
    return NULL;
}

static void* v4l2dev_render_thread_proc(void *param)
{
    V4L2DEV *dev = (V4L2DEV*)param;
    int      err;

    while (!(dev->thread_state & V4L2DEV_TS_EXIT)) {
        if (0 != sem_wait(&dev->sem_render)) {
            ALOGD("failed to wait sem_render !\n");
            break;
        }

        if (dev->thread_state & V4L2DEV_TS_EXIT) {
            break;
        }

        if (dev->update_flag) {
            dev->update_flag = 0;
        }

        int   pts  = (int)(dev->buf.timestamp.tv_usec + dev->buf.timestamp.tv_sec * 1000000);
        char *data = (char*)dev->vbs[dev->buf.index].addr;
        int   len  = dev->buf.bytesused;

#if 0
        ANativeWindowBuffer *buf;
        if (dev->cur_win != NULL && 0 == native_window_dequeue_buffer_and_wait(dev->cur_win.get(), &buf)) {
            GraphicBufferMapper &mapper = GraphicBufferMapper::get();
            Rect bounds(buf->width, buf->height);
            void *dst = NULL;

            if (0 == mapper.lock(buf->handle, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst)) {
                render_v4l2(dev,
                    dst , -1 , buf->format    , buf->width, buf->height,
                    data, len, dev->cam_pixfmt, dev->cam_w, dev->cam_h, pts);
                mapper.unlock(buf->handle);
            }

            if ((err = dev->cur_win->queueBuffer(dev->cur_win.get(), buf, -1)) != 0) {
                ALOGW("Surface::queueBuffer returned error %d\n", err);
            }
        }
#endif
    }

//  ALOGD("v4l2dev_render_thread_proc exited !");
    return NULL;
}

static int v4l2_try_fmt_size(int fd, int fmt, int *width, int *height)
{
    struct v4l2_fmtdesc fmtdesc;
    struct v4l2_format  v4l2fmt;
    int                 find    =  0;
    int                 ret     =  0;

    fmtdesc.index = 0;
    fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;

#if 0
    ALOGD("\n");
    ALOGD("VIDIOC_ENUM_FMT   \n");
    ALOGD("------------------\n");
#endif

    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1) {
#if 0
        ALOGD("%d. flags: %d, description: %-16s, pixelfmt: 0x%0x\n",
            fmtdesc.index, fmtdesc.flags,
            fmtdesc.description,
            fmtdesc.pixelformat);
#endif
        if (fmt == (int)fmtdesc.pixelformat) {
            find = 1;
        }
        fmtdesc.index++;
    }
    ALOGD("\n");

    if (!find) {
        ALOGD("video device can't support pixel format: 0x%0x\n", fmt);
        return -1;
    }

    // try format
    v4l2fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2fmt.fmt.pix.width       = *width;
    v4l2fmt.fmt.pix.height      = *height;
    v4l2fmt.fmt.pix.pixelformat = fmt;
    v4l2fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    ret = ioctl(fd, VIDIOC_TRY_FMT, &v4l2fmt);
    if (ret < 0)
    {
        ALOGE("VIDIOC_TRY_FMT Failed: %s\n", strerror(errno));
        return ret;
    }

    // driver surpport this size
    *width  = v4l2fmt.fmt.pix.width;
    *height = v4l2fmt.fmt.pix.height;

    ALOGD("using pixel format: 0x%0x\n", fmt);
    ALOGD("using frame size: %d, %d\n\n", *width, *height);
    return 0;
}

// 函数实现
void* v4l2dev_init(const char *name, int sub, int w, int h, int frate)
{
    int i;
    V4L2DEV *dev = calloc(1, sizeof(V4L2DEV));
    if (!dev) {
        return NULL;
    }

#ifdef PLATFORM_ALLWINNER_A33
    ion_alloc_open();
#endif

    // open camera device
    dev->fd = open(name, O_RDWR | O_NONBLOCK);
    if (dev->fd < 0) {
        ALOGW("failed to open video device: %s\n", name);
        free(dev);
        return NULL;
    }

    struct v4l2_capability cap;
    ioctl(dev->fd, VIDIOC_QUERYCAP, &cap);
    ALOGD("\n");
    ALOGD("video device caps \n");
    ALOGD("------------------\n");
    ALOGD("driver:       %s\n" , cap.driver      );
    ALOGD("card:         %s\n" , cap.card        );
    ALOGD("bus_info:     %s\n" , cap.bus_info    );
    ALOGD("version:      %0x\n", cap.version     );
    ALOGD("capabilities: %0x\n", cap.capabilities);
    ALOGD("\n");

    if (strcmp((char*)cap.driver, "uvcvideo") != 0) {
        struct v4l2_input input;
        input.index = sub;
        ioctl(dev->fd, VIDIOC_S_INPUT, &input);
    }

    if (0 == v4l2_try_fmt_size(dev->fd, V4L2_PIX_FMT_NV12, &w, &h)) {
//      ALOGD("===ck=== V4L2_PIX_FMT_NV12");
        dev->cam_pixfmt = V4L2_PIX_FMT_NV12;
        dev->cam_w      = w;
        dev->cam_h      = h;
    }
    else if (0 == v4l2_try_fmt_size(dev->fd, V4L2_PIX_FMT_NV21, &w, &h)) {
//      ALOGD("===ck=== V4L2_PIX_FMT_NV21");
        dev->cam_pixfmt = V4L2_PIX_FMT_NV21;
        dev->cam_w      = w;
        dev->cam_h      = h;
    }
    else if (0 == v4l2_try_fmt_size(dev->fd, V4L2_PIX_FMT_YUYV, &w, &h)) {
//      ALOGD("===ck=== V4L2_PIX_FMT_YUYV");
        dev->cam_pixfmt = V4L2_PIX_FMT_YUYV;
        dev->cam_w      = w;
        dev->cam_h      = h;
    }

    struct v4l2_format v4l2fmt;
    v4l2fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(dev->fd, VIDIOC_G_FMT, &v4l2fmt);
    v4l2fmt.fmt.pix.pixelformat = dev->cam_pixfmt;
    v4l2fmt.fmt.pix.width       = dev->cam_w;
    v4l2fmt.fmt.pix.height      = dev->cam_h;
    if (ioctl(dev->fd, VIDIOC_S_FMT, &v4l2fmt) != -1) {
        ALOGD("VIDIOC_S_FMT      \n");
        ALOGD("------------------\n");
        ALOGD("width:        %d\n",    v4l2fmt.fmt.pix.width       );
        ALOGD("height:       %d\n",    v4l2fmt.fmt.pix.height      );
        ALOGD("pixfmt:       0x%0x\n", v4l2fmt.fmt.pix.pixelformat );
        ALOGD("field:        %d\n",    v4l2fmt.fmt.pix.field       );
        ALOGD("bytesperline: %d\n",    v4l2fmt.fmt.pix.bytesperline);
        ALOGD("sizeimage:    %d\n",    v4l2fmt.fmt.pix.sizeimage   );
        ALOGD("colorspace:   %d\n",    v4l2fmt.fmt.pix.colorspace  );
        dev->cam_stride = v4l2fmt.fmt.pix.bytesperline;
    }
    else {
        ALOGW("failed to set camera preview size and pixel format !\n");
        close(dev->fd);
        free (dev);
        return NULL;
    }

    struct v4l2_streamparm streamparam;
    streamparam.parm.capture.timeperframe.numerator   = 1;
    streamparam.parm.capture.timeperframe.denominator = frate;
    streamparam.parm.capture.capturemode              = 0;
    streamparam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(dev->fd, VIDIOC_S_PARM, &streamparam) != -1) {
        ioctl(dev->fd, VIDIOC_G_PARM, &streamparam);
        ALOGD("current camera frame rate: %d/%d !\n",
            streamparam.parm.capture.timeperframe.denominator,
            streamparam.parm.capture.timeperframe.numerator );
        dev->cam_frate_num = streamparam.parm.capture.timeperframe.denominator;
        dev->cam_frate_den = streamparam.parm.capture.timeperframe.numerator;
    }
    else {
        ALOGW("failed to set camera frame rate !\n");
    }

    struct v4l2_requestbuffers req;
    req.count  = VIDEO_CAPTURE_BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(dev->fd, VIDIOC_REQBUFS, &req);

    for (i=0; i<VIDEO_CAPTURE_BUFFER_COUNT; i++)
    {
        dev->buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        dev->buf.memory = V4L2_MEMORY_MMAP;
        dev->buf.index  = i;
        ioctl(dev->fd, VIDIOC_QUERYBUF, &dev->buf);

        dev->vbs[i].addr= mmap(NULL, dev->buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                               dev->fd, dev->buf.m.offset);
        dev->vbs[i].len = dev->buf.length;

        ioctl(dev->fd, VIDIOC_QBUF, &dev->buf);
    }

    // set test frame rate flag
    dev->thread_state |= V4L2DEV_TS_PAUSE;

    // create sem_render
    sem_init(&dev->sem_render, 0, 0);

    // create capture thread
    pthread_create(&dev->thread_id_capture, NULL, v4l2dev_capture_thread_proc, dev);

    // create render thread
    pthread_create(&dev->thread_id_render , NULL, v4l2dev_render_thread_proc , dev);

    return dev;
}

void v4l2dev_close(void *ctxt)
{
    int i;
    V4L2DEV *dev = (V4L2DEV*)ctxt;
    if (!dev) return;

    // wait thread safely exited
    dev->thread_state |= V4L2DEV_TS_EXIT; sem_post(&dev->sem_render);
    pthread_join(dev->thread_id_capture, NULL);
    pthread_join(dev->thread_id_render , NULL);

    // unmap buffers
    for (i=0; i<VIDEO_CAPTURE_BUFFER_COUNT; i++) {
        munmap(dev->vbs[i].addr, dev->vbs[i].len);
    }

    // close & free
    close(dev->fd);
    free (dev);

#ifdef PLATFORM_ALLWINNER_A33
    ion_alloc_close();
#endif
}

void v4l2dev_set_preview_window(void *ctxt, void *win)
{
    V4L2DEV *dev = (V4L2DEV*)ctxt;
    if (!dev) return;
    dev->window      = win;
    dev->update_flag = 1;
}

void v4l2dev_capture_start(void *ctxt)
{
    V4L2DEV *dev = (V4L2DEV*)ctxt;
    // check fd valid
    if (!dev || dev->fd <= 0) return;

    // turn on stream
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(dev->fd, VIDIOC_STREAMON, &type);

    // resume thread
    dev->thread_state &= ~V4L2DEV_TS_PAUSE;
}

void v4l2dev_capture_stop(void *ctxt)
{
    V4L2DEV *dev = (V4L2DEV*)ctxt;
    // check fd valid
    if (!dev || dev->fd <= 0) return;

    // pause thread
    dev->thread_state |= V4L2DEV_TS_PAUSE;

    // turn off stream
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(dev->fd, VIDIOC_STREAMOFF, &type);
}

void v4l2dev_preview_start(void *ctxt)
{
    V4L2DEV *dev = (V4L2DEV*)ctxt;
    if (!dev) return;
    // set start prevew flag
    dev->thread_state |= V4L2DEV_TS_PREVIEW;
}

void v4l2dev_preview_stop(void *ctxt)
{
    V4L2DEV *dev = (V4L2DEV*)ctxt;
    if (!dev) return;
    // set stop prevew flag
    dev->thread_state &= ~V4L2DEV_TS_PREVIEW;
}



