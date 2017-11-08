#ifndef __V4L2DEV_H__
#define __V4L2DEV_H__

// 类型定义
// v4l2 capture callback
typedef int (*V4L2DEV_CAPTURE_CALLBACK)(void *v4l2, void *data[8], int linesize[8], int pts);

// 函数定义
void* v4l2dev_init (const char *name, int sub, int w, int h, int frate);
void  v4l2dev_close(void *ctxt);
void  v4l2dev_set_preview_window(void *ctxt, void *win);

void  v4l2dev_capture_start(void *ctxt);
void  v4l2dev_capture_stop (void *ctxt);
void  v4l2dev_preview_start(void *ctxt);
void  v4l2dev_preview_stop (void *ctxt);
void  v4l2dev_set_callback (void *ctxt, void *callback);

#endif





