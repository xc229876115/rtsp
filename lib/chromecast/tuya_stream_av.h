#ifndef __TUYA_STREAM_AV_H__
#define __TUYA_STREAM_AV_H__

#include <stdint.h>

#include "tuya_ipc_media.h"



#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum TUYA_AIOT_AV_MEDIA_TYPE {
    TUYA_AIOT_AV_MEDIA_TYPE_AUDIO,  ///< 音频
    TUYA_AIOT_AV_MEDIA_TYPE_VIDEO,  ///< 视频
};

enum TUYA_AIOT_AV_MEDIA_SOURCE {
    TUYA_AIOT_AV_MEDIA_SOURCE_SRC,
    TUYA_AIOT_AV_MEDIA_SOURCE_DEST,
};

typedef struct tuya_aiot_av_codec_ {
    TUYA_CODEC_ID    codec;         ///< 码流编码类型
    unsigned short   step;

    union {
        struct {
            int8_t          fps;        ///< 帧率
            unsigned short  width;      ///< 宽度
            unsigned short  height;     ///< 高度
            unsigned long   freq;       ///< 频率
        } video;

        struct {
            unsigned short  channel;            ///< 声道个数
            unsigned short  bit_per_sample;     ///< 每个采样的位数
            unsigned long   sample_rate;        ///< 采样率
        } audio;
    };
} tuya_aiot_av_codec_t;


typedef struct sm_es_av_handle_ {
    uint32_t        pts;
    uint32_t        ts;
    uint64_t        base_ts;
} sm_es_av_handle_s;


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif