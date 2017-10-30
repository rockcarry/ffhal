/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_ffhal"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>


#include <audio_utils/resampler.h>
#include <audio_route/audio_route.h>

#include <hardware/hardware.h>
#include <hardware/audio.h>
#include <hardware/audio_alsaops.h>

// 常量定义
#define CARD_MIXER    0
#define CARD_CODEC    0
#define CARD_HDMI     1
#define CARD_SPDIF    2
#define PORT_CODEC    0
#define PORT_HDMI     0
#define PORT_SPDIF    0

#define PLAYBACK_SAMPLE_RATE    44100
#define PLAYBACK_PERIOD_SIZE    1024
#define PLAYBACK_PERIOD_COUNT   4
#define PLAYBACK_CHANNEL_NUM    2

#define CAPTURE_SAMPLE_RATE     44100
#define CAPTURE_PERIOD_SIZE     512
#define CAPTURE_PERIOD_COUNT    4
#define CAPTURE_CHANNEL_NUM     1

#define AUDIO_PATH_XML   "/system/etc/a64_paths.xml"

enum {
    OUT_DEVICE_SPEAKER,
    OUT_DEVICE_HEADPHONE,
    OUT_DEVICE_BT_SCO,
    OUT_DEVICE_TAB_SIZE,
};

const char * const normal_route_configs[OUT_DEVICE_TAB_SIZE] = {
    "media-speaker",    /* OUT_DEVICE_SPEAKER */
    "media-headphone",  /* OUT_DEVICE_HEADPHONE */
    "com-ap-bt",        /* OUT_DEVICE_BT_SCO */
};

struct ffhal_stream_in {
    struct audio_stream_in  stream;
    pthread_mutex_t           lock;
    struct pcm_config       config;
    struct pcm                *pcm;
    struct ffhal_audio_device *dev;
    int                    standby;
};

struct ffhal_stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t           lock;
    struct pcm_config       config;
    struct pcm                *pcm;
    struct ffhal_audio_device *dev;
    int                    standby;
    unsigned int           written;
};

struct ffhal_audio_device {
    struct audio_hw_device hw_device;
    pthread_mutex_t lock;   /* see note below on mutex acquisition order */
    int    in_devices;
    int    out_devices;
    struct audio_route      *aroute;
    struct ffhal_stream_in  *active_input;
    struct ffhal_stream_out *active_output;
    bool   mic_mute;
};

static void select_device(struct ffhal_audio_device *adev)
{
    int out_dev_id = 0;
    int hp_on      = 0;
    int spk_on     = 0;
    const char *output_route = NULL;
    const char *input_route  = NULL;

    audio_route_reset(adev->aroute);
    audio_route_update_mixer_old_value(adev->aroute);
    audio_route_apply_path(adev->aroute, "media-speaker-off");
    if (adev->active_output) {
        hp_on  = adev->out_devices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
        spk_on = adev->out_devices & AUDIO_DEVICE_OUT_SPEAKER;
        if (spk_on) {
            out_dev_id = OUT_DEVICE_SPEAKER;
        } else if (hp_on) {
            out_dev_id = OUT_DEVICE_HEADPHONE;
        }
        //++ for fmtx
        if (adev->out_devices & AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET) {
            out_dev_id = OUT_DEVICE_HEADPHONE;
        }
        //-- for fmtx
        output_route = normal_route_configs[out_dev_id];
    }
    if (adev->active_input) {
        input_route = "media-main-mic";
    }
    if (output_route) audio_route_apply_path(adev->aroute, output_route);
    if (input_route ) audio_route_apply_path(adev->aroute, input_route );
    audio_route_update_mixer(adev->aroute);
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct ffhal_stream_out *out)
{
    struct ffhal_audio_device *adev = out->dev;
    int card = 0;
    int port = 0;

    if (adev->out_devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
        card = CARD_HDMI;
        port = PORT_HDMI;
    } else {
        card = CARD_CODEC;
        port = PORT_CODEC;
    }
    out->pcm = pcm_open(card, port, PCM_OUT | PCM_MMAP | PCM_NOIRQ | PCM_MONOTONIC, &out->config);
    if (!pcm_is_ready(out->pcm)) {
        ALOGE("cannot open pcm_out driver: %s", pcm_get_error(out->pcm));
        pcm_close(out->pcm);
        adev->active_output = NULL;
        return -ENODEV;
    } else {
        adev->active_output = out;
    }
    select_device(adev);
    return 0;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    return PLAYBACK_SAMPLE_RATE;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    /* return the closest majoring multiple of 16 frames, as
     * audioflinger expects audio buffers to be a multiple of 16 frames */
    size_t size = PLAYBACK_PERIOD_SIZE;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_out_frame_size((struct audio_stream_out *)stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    struct ffhal_stream_out *out = (struct ffhal_stream_out *)stream;
    if (out->config.channels == 1) {
        return AUDIO_CHANNEL_OUT_MONO;
    } else {
        return AUDIO_CHANNEL_OUT_STEREO;
    }
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

static int do_output_standby(struct ffhal_stream_out *out)
{
    struct ffhal_audio_device *adev = out->dev;

    if (!out->standby) {
        pcm_close(out->pcm);
        out->standby        = 1;
        out->pcm            = NULL;
        adev->active_output = NULL;
        adev->out_devices   = AUDIO_DEVICE_NONE;
        select_device(adev);
    }
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct ffhal_stream_out *out = (struct ffhal_stream_out *)stream;
    int    status;

//  pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out);
    pthread_mutex_unlock(&out->lock);
//  pthread_mutex_unlock(&out->dev->lock);

    return status;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct ffhal_stream_out   *out  = (struct ffhal_stream_out *)stream;
    struct ffhal_audio_device *adev = out->dev;
    struct str_parms *parms;
    char  *str;
    char   value[32];
    int    ret, val = 0;

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        pthread_mutex_lock(&adev->lock);
        pthread_mutex_lock(&out->lock);
        if (((adev->out_devices & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0)) {
            if (out == adev->active_output) {
                if ( (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) ^ (adev->out_devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) ) {
                    do_output_standby(out);
                }
            }
            adev->out_devices &= ~AUDIO_DEVICE_OUT_ALL;
            adev->out_devices |= val;
//          select_device(adev);
        }
        pthread_mutex_unlock(&out->lock);
        pthread_mutex_unlock(&adev->lock);
    }

    str_parms_destroy(parms);
    return ret;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct ffhal_stream_out *out = (struct ffhal_stream_out *)stream;
    return (PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT * 1000) / out->config.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{
    int    ret;
    struct ffhal_stream_out   *out  = (struct ffhal_stream_out *)stream;
    struct ffhal_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_out_frame_size(stream);
    size_t out_frames = bytes / frame_size;
    int    kernel_frames;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            ALOGD("failed to start output stream !");
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        } else {
            out->standby = 0;
        }
    }
    pthread_mutex_unlock(&adev->lock);

    ret = pcm_mmap_write(out->pcm, buffer, out_frames * frame_size);
    if (ret == 0) {
        out->written += out_frames;
    }

exit:
    pthread_mutex_unlock(&out->lock);
    if (ret != 0) {
        usleep((int64_t)bytes * 1000000 / audio_stream_out_frame_size(stream) / out_get_sample_rate(&stream->common));
    }

    return bytes;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                   uint64_t *frames, struct timespec *timestamp)
{
    struct ffhal_stream_out *out = (struct ffhal_stream_out *)stream;
    int ret = -1;

    if (out->pcm) {
        unsigned int avail;
        if (pcm_get_htimestamp(out->pcm, &avail, timestamp) == 0) {
            size_t kernel_buffer_size = out->config.period_size * out->config.period_count;
            int64_t signed_frames = out->written - kernel_buffer_size + avail;
            if (signed_frames >= 0) {
                *frames = signed_frames;
                ret = 0;
            }
        }
    }

    return ret;
}

static int out_get_render_position(const struct audio_stream_out *stream, uint32_t *dsp_frames)
{
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream, int64_t *timestamp)
{
    return -EINVAL;
}

/** audio_stream_in implementation **/
static int start_input_stream(struct ffhal_stream_in *in)
{
    struct ffhal_audio_device *adev = in->dev;

    in->pcm = pcm_open(0, PORT_CODEC, PCM_IN, &in->config);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        adev->active_input = NULL;
        return -ENOMEM;
    } else {
        adev->active_input = in;
    }

    select_device(adev);
    return 0;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    return CAPTURE_SAMPLE_RATE;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    /* return the closest majoring multiple of 16 frames, as
     * audioflinger expects audio buffers to be a multiple of 16 frames */
    size_t size = CAPTURE_PERIOD_SIZE;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_in_frame_size((struct audio_stream_in *)stream);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    struct ffhal_stream_in *in = (struct ffhal_stream_in *)stream;
    if (in->config.channels == 1) {
        return AUDIO_CHANNEL_IN_MONO;
    } else {
        return AUDIO_CHANNEL_IN_STEREO;
    }
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

/* must be called with hw device and input stream mutexes locked */
static int do_input_standby(struct ffhal_stream_in *in)
{
    struct ffhal_audio_device *adev = in->dev;

    if (!in->standby) {
        pcm_close(in->pcm);
        in->standby        = 1;
        in->pcm            = NULL;
        adev->active_input = NULL;
        adev->in_devices   = AUDIO_DEVICE_NONE;
        select_device(adev);
    }
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct ffhal_stream_in *in = (struct ffhal_stream_in *)stream;
    int    status;

//  pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    status = do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
//  pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    return -ENOSYS;
}

static char* in_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    return -ENOSYS;
}

static ssize_t in_read(struct audio_stream_in *stream, void *buffer, size_t bytes)
{
    int ret = 0;
    struct ffhal_stream_in    *in   = (struct ffhal_stream_in *)stream;
    struct ffhal_audio_device *adev = in->dev;

    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        ret = start_input_stream(in);
        if (ret != 0) {
            ALOGD("failed to start input stream !");
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        } else {
            in->standby = 0;
        }
    }
    pthread_mutex_unlock(&adev->lock);

    ret = pcm_read(in->pcm, buffer, bytes);
    if (adev->mic_mute) {
        memset(buffer, 0, bytes);
    }

exit:
    pthread_mutex_unlock(&in->lock);
    if (ret < 0) {
        usleep((int64_t)bytes * 1000000 / audio_stream_in_frame_size(stream) /
                in_get_sample_rate(&stream->common));
    }
    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
        audio_io_handle_t handle,
        audio_devices_t devices,
        audio_output_flags_t flags,
        struct audio_config *config,
        struct audio_stream_out **stream_out,
        const char *address __unused)
{
    struct ffhal_audio_device *adev = (struct ffhal_audio_device *)dev;
    struct ffhal_stream_out   *out;

    out = (struct ffhal_stream_out *)calloc(1, sizeof(struct ffhal_stream_out));
    if (!out) {
        return -ENOMEM;
    }

    out->config.channels     = PLAYBACK_CHANNEL_NUM;
    out->config.rate         = PLAYBACK_SAMPLE_RATE;
    out->config.format       = PCM_FORMAT_S16_LE;
    out->config.period_size  = PLAYBACK_PERIOD_SIZE;
    out->config.period_count = PLAYBACK_PERIOD_COUNT;

    out->stream.common.get_sample_rate      = out_get_sample_rate;
    out->stream.common.set_sample_rate      = out_set_sample_rate;
    out->stream.common.get_buffer_size      = out_get_buffer_size;
    out->stream.common.get_channels         = out_get_channels;
    out->stream.common.get_format           = out_get_format;
    out->stream.common.set_format           = out_set_format;
    out->stream.common.standby              = out_standby;
    out->stream.common.dump                 = out_dump;
    out->stream.common.set_parameters       = out_set_parameters;
    out->stream.common.get_parameters       = out_get_parameters;
    out->stream.common.add_audio_effect     = out_add_audio_effect;
    out->stream.common.remove_audio_effect  = out_remove_audio_effect;
    out->stream.get_latency                 = out_get_latency;
    out->stream.set_volume                  = out_set_volume;
    out->stream.write                       = out_write;
    out->stream.get_render_position         = out_get_render_position;
    out->stream.get_next_write_timestamp    = out_get_next_write_timestamp;
    out->stream.get_presentation_position   = out_get_presentation_position;

    out->dev             = adev;
    out->standby         = 1;

    config->format       = out_get_format(&out->stream.common);
    config->channel_mask = out_get_channels(&out->stream.common);
    config->sample_rate  = out_get_sample_rate(&out->stream.common);

    *stream_out = &out->stream;

    return 0;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
        struct audio_stream_out *stream)
{
    struct ffhal_stream_out *out = (struct ffhal_stream_out *)stream;
    struct ffhal_audio_device *adev = out->dev;
    out_standby(&stream->common);
    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    return -ENOSYS;
}

static char * adev_get_parameters(const struct audio_hw_device *dev, const char *keys)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev, float *volume)
{
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev, bool muted)
{
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev, bool *muted)
{
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct ffhal_audio_device *adev = (struct ffhal_audio_device *)dev;
    adev->mic_mute = state;
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct ffhal_audio_device *adev = (struct ffhal_audio_device *)dev;
    *state = adev->mic_mute;
    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev, const struct audio_config *config)
{
    return CAPTURE_PERIOD_SIZE * CAPTURE_CHANNEL_NUM * sizeof(int16_t);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
        audio_io_handle_t handle,
        audio_devices_t devices,
        struct audio_config *config,
        struct audio_stream_in **stream_in,
        audio_input_flags_t flags __unused,
        const char *address __unused,
        audio_source_t source __unused)
{
    struct ffhal_audio_device *adev = (struct ffhal_audio_device *)dev;
    struct ffhal_stream_in    *in;
    int    ret;

    in = (struct ffhal_stream_in *)calloc(1, sizeof(struct ffhal_stream_in));
    if (!in) {
        return -ENOMEM;
    }

    in->config.channels     = CAPTURE_CHANNEL_NUM;
    in->config.rate         = CAPTURE_SAMPLE_RATE;
    in->config.format       = PCM_FORMAT_S16_LE;
    in->config.period_size  = CAPTURE_PERIOD_SIZE;
    in->config.period_count = CAPTURE_PERIOD_COUNT;

    in->stream.common.get_sample_rate     = in_get_sample_rate;
    in->stream.common.set_sample_rate     = in_set_sample_rate;
    in->stream.common.get_buffer_size     = in_get_buffer_size;
    in->stream.common.get_channels        = in_get_channels;
    in->stream.common.get_format          = in_get_format;
    in->stream.common.set_format          = in_set_format;
    in->stream.common.standby             = in_standby;
    in->stream.common.dump                = in_dump;
    in->stream.common.set_parameters      = in_set_parameters;
    in->stream.common.get_parameters      = in_get_parameters;
    in->stream.common.add_audio_effect    = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain                   = in_set_gain;
    in->stream.read                       = in_read;
    in->stream.get_input_frames_lost      = in_get_input_frames_lost;

    in->dev     = adev;
    in->standby = 1;

    *stream_in = &in->stream;
    return 0;
}

static void adev_close_input_stream(struct audio_hw_device *dev, struct audio_stream_in *stream)
{
    in_standby(&stream->common);
    free(stream);
    return;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct ffhal_audio_device *adev = (struct ffhal_audio_device *)device;
    audio_route_free(adev->aroute);
    free(device);
    return 0;
}

static int adev_open(const hw_module_t* module, const char* name, hw_device_t** device)
{
    struct ffhal_audio_device *adev;
    int    ret;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(struct ffhal_audio_device));
    if (!adev) {
        return -ENOMEM;
    }

    adev->hw_device.common.tag              = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version          = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_device.common.module           = (struct hw_module_t *) module;
    adev->hw_device.common.close            = adev_close;
    adev->hw_device.init_check              = adev_init_check;
    adev->hw_device.set_voice_volume        = adev_set_voice_volume;
//  adev->hw_device.set_master_volume       = adev_set_master_volume;
//  adev->hw_device.get_master_volume       = adev_get_master_volume;
//  adev->hw_device.set_master_mute         = adev_set_master_mute;
//  adev->hw_device.get_master_mute         = adev_get_master_mute;
    adev->hw_device.set_mode                = adev_set_mode;
    adev->hw_device.set_mic_mute            = adev_set_mic_mute;
    adev->hw_device.get_mic_mute            = adev_get_mic_mute;
    adev->hw_device.set_parameters          = adev_set_parameters;
    adev->hw_device.get_parameters          = adev_get_parameters;
    adev->hw_device.get_input_buffer_size   = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream      = adev_open_output_stream;
    adev->hw_device.close_output_stream     = adev_close_output_stream;
    adev->hw_device.open_input_stream       = adev_open_input_stream;
    adev->hw_device.close_input_stream      = adev_close_input_stream;
    adev->hw_device.dump                    = adev_dump;

    adev->out_devices = AUDIO_DEVICE_NONE;
    adev->in_devices  = AUDIO_DEVICE_NONE;

    adev->aroute = audio_route_init(CARD_MIXER, AUDIO_PATH_XML);

    *device = &adev->hw_device.common;
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "ffhal audio HW HAL",
        .author = "rockcarry",
        .methods = &hal_module_methods,
    },
};
