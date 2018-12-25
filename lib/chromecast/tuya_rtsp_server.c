#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "uni_log.h"
#include "uni_thread.h"
#include "adapter_platform.h"

#include "tuya_ipc_rtsp_server.h"
#include "common/tuya_sm_utils.h"
#include "tuya_ipc_media.h"
#include "rtsp_server.h"
#include "tuya_ring_buffer.h"
#include "media_codec/tuya_g711_utils.h"
#include "cloud_operation.h"
#include "tuya_stream_av.h"


#define AUDIO_RAW_LEN           1600
#define RTSP_MAX_CLIENT_NUM     5

typedef struct tuya_stream_handle_ {
    int                     get_sps_pps;
    char                    rawbuf[3500];
    int                     rawlen;
    sm_es_av_handle_s       vbufhdl;
    sm_es_av_handle_s       abufhdl;
} tuya_stream_handle_s;

typedef struct tuya_echoshow_handle_ {
    int                     vchannel;
    int                     start;
    int                     task_exit;
    tuya_aiot_av_codec_t    acodec;
    tuya_aiot_av_codec_t    vcodec;
    THRD_HANDLE             pthrdhdl;

    int                     sflag;
    pthread_mutex_t         lock;
    tuya_stream_handle_s    shdl[RTSP_MAX_CLIENT_NUM];
} tuya_rtsp_handle_s;


static tuya_rtsp_handle_s* get_rtsp_thread_handle()
{
    static tuya_rtsp_handle_s eshdl = {0};

    return &eshdl;
}

static int init(void* thiz, int* pid)
{
    if(thiz == NULL || pid == NULL) {
        TUYA_LOG("param error\n");
        return -1;
    }

    TUYA_LOG("begin___\n");

    int ret = -1;
    int i = 0;
    tuya_rtsp_handle_s* peshdl = (tuya_rtsp_handle_s*)thiz;

    pthread_mutex_lock(&(peshdl->lock));

    for(i = 0; i < RTSP_MAX_CLIENT_NUM; ++i) {
        if((peshdl->sflag & (1 << i)) == 0) {
            peshdl->sflag |= (1 << i);
            *pid = i;
            ret = 0;
            break;
        }
    }

    pthread_mutex_unlock(&(peshdl->lock));

    TUYA_LOG("end___, ret(%d), id(%d), flag(0x%x)\n", ret, *pid, peshdl->sflag);

    return 0;
}

static int uninit(void* thiz, int id)
{
    if(thiz == NULL || id >= RTSP_MAX_CLIENT_NUM) {
        TUYA_LOG("param error\n");
        return -1;
    }

    tuya_rtsp_handle_s* peshdl = (tuya_rtsp_handle_s*)thiz;

    TUYA_LOG("begin___\n");

    pthread_mutex_lock(&(peshdl->lock));

    peshdl->sflag &= ~(1 << id);

    pthread_mutex_unlock(&(peshdl->lock));

    TUYA_LOG("end___, flag(0x%x)\n", peshdl->sflag);

    return 0;
}

static int start(void* thiz, rtsp_stream_identify_s* pidentify)
{
    if(thiz == NULL || pidentify == NULL) {
        return -1;
    }

    TUYA_LOG("begin___\n");

    tuya_rtsp_handle_s* peshdl = (tuya_rtsp_handle_s*)thiz;
    int id = pidentify->stream_id;

    if(pidentify->type == RTSP_STREAM_TYPE_AUDIO) {
        peshdl->shdl[id].rawlen = 0;
        memset(&peshdl->shdl[id].abufhdl, 0, sizeof(sm_es_av_handle_s));
    } else if (pidentify->type == RTSP_STREAM_TYPE_VIDEO) {
        peshdl->shdl[id].get_sps_pps = 0;
        memset(&peshdl->shdl[id].vbufhdl, 0, sizeof(sm_es_av_handle_s));
    }

    TUYA_LOG("end___\n");

    return 0;
}

static int stop(void* thiz, rtsp_stream_identify_s* pidentify)
{
    if(thiz == NULL) {
        return -1;
    }

    TUYA_LOG("begin___\n");

    // tuya_rtsp_handle_s* peshdl = (tuya_rtsp_handle_s*)thiz;

    TUYA_LOG("end___\n");

    return 0;
}

static int get_sdp(void* thiz, rtsp_stream_identify_s* pidentify, char* buf, int* size)
{
    if(thiz == NULL) {
        return -1;
    }

    if(pidentify->type == RTSP_STREAM_TYPE_AUDIO) {
        strcpy(buf, "m=audio 0 RTP/AVP 97\r\n"
               "c=IN IP4 0.0.0.0\r\n"
               "a=rtpmap:97 PCMU/8000/1\r\n"
               "a=control:audio\r\n"
               "a=range:npt=now-\r\n");
    } else if (pidentify->type == RTSP_STREAM_TYPE_VIDEO) {
        strcpy(buf, "m=video 0 RTP/AVP 96\r\n"
                    "c=IN IP4 0.0.0.0\r\n"
                    "b=AS:96\r\n"
                    "a=rtpmap:96 H264/90000\r\n"
                    "a=cliprect:0,0,640,360\r\n"
                    "a=framesize:96 640-360\r\n"
                    "a=fmtp:96 packetization-mode=1;profile-level-id=42801E\r\n"
                    "a=control:video\r\n"
                    "a=range:npt=now-\r\n");
    }

    TUYA_LOG("\n");

    return 0;
}

static int get_max_frame_size(void* thiz, rtsp_stream_identify_s* pidentify)
{
    if(thiz == NULL) {
        return -1;
    }

    int size = 0;

    if(pidentify->type == RTSP_STREAM_TYPE_AUDIO) {
        size = 4096;
    } else if(pidentify->type == RTSP_STREAM_TYPE_VIDEO) {
        size = 200 * 1024;
    }

    return size;
}

static int get_next_frame(void* thiz, rtsp_stream_identify_s* pidentify, rtsp_stream_info_s* psvif)
{
    if(thiz == NULL || pidentify == NULL) {
        return -1;
    }

    int ret = -1;
    Ring_Buffer_Node_S *node = NULL;
    tuya_rtsp_handle_s* peshdl = (tuya_rtsp_handle_s*)thiz;
    unsigned int id = pidentify->stream_id;
    int buserid = E_USER_RTSP + id;

    if(id >= RTSP_MAX_CLIENT_NUM) {
        return -1;
    }

    if(pidentify->type == RTSP_STREAM_TYPE_AUDIO && peshdl->shdl[id].get_sps_pps) {
        node = tuya_ipc_ring_buffer_get_audio_frame(E_CHANNEL_AUDIO, buserid, 0);
        if(node == NULL || node->size <= 0) {
            return -1;
        }

        if(peshdl->shdl[id].abufhdl.base_ts == 0) {
            peshdl->shdl[id].abufhdl.base_ts = node->timestamp;
        }

        memcpy(peshdl->shdl[id].rawbuf + peshdl->shdl[id].rawlen, node->rawData, node->size);
        peshdl->shdl[id].rawlen += node->size;

        if(peshdl->shdl[id].rawlen < AUDIO_RAW_LEN) {
            return 1;
        }

        unsigned char g711_buffer[1600] = {0};
        unsigned char* ptr = peshdl->shdl[id].rawbuf;
        unsigned int nBytesRead = peshdl->shdl[id].rawlen;

        if(peshdl->acodec.codec == TUYA_CODEC_AUDIO_PCM) {
            tuya_g711_encode(TUYA_G711_MU_LAW, (unsigned short *)peshdl->shdl[id].rawbuf,
                             AUDIO_RAW_LEN, g711_buffer, &nBytesRead);
            ptr = g711_buffer;
        }

        if(nBytesRead > 0) {
            memcpy(psvif->buf, ptr, nBytesRead);

            psvif->ts = peshdl->shdl[id].abufhdl.pts;
            psvif->size = nBytesRead;
            peshdl->shdl[id].abufhdl.pts = (node->timestamp - peshdl->shdl[id].abufhdl.base_ts) * (8000 / 1000);

            if(peshdl->acodec.codec == TUYA_CODEC_AUDIO_PCM) {
                char temp[1024] = {0};
                peshdl->shdl[id].rawlen -= AUDIO_RAW_LEN;
                memcpy(temp, peshdl->shdl[id].rawbuf + AUDIO_RAW_LEN, peshdl->shdl[id].rawlen);
                memcpy(peshdl->shdl[id].rawbuf, temp, peshdl->shdl[id].rawlen);
            } else {
                peshdl->shdl[id].rawlen = 0;
            }
        }

        ret = 0;
    } else if(pidentify->type == RTSP_STREAM_TYPE_VIDEO) {
        node = tuya_ipc_ring_buffer_get_video_frame(peshdl->vchannel, buserid, 0);
        if(node == NULL || node->size <= 0) {
            return -1;
        }

        ret = 0;

        u_char* ptr = (u_char *)node->rawData;
        int nNaluType = ptr[4] & 0x1f;

        if(peshdl->shdl[id].get_sps_pps != 1) {
            if(nNaluType == 0x07) {
                peshdl->shdl[id].get_sps_pps = 1;
                TUYA_LOG("video get sps pps info...\n");
            } else {
                return 0;
            }
        }

        if(peshdl->shdl[id].vbufhdl.base_ts == 0) {
            peshdl->shdl[id].vbufhdl.base_ts = node->timestamp;
        }

        memcpy(psvif->buf, node->rawData, node->size);

        psvif->ts = peshdl->shdl[id].vbufhdl.pts;
        psvif->size = node->size;
        peshdl->shdl[id].vbufhdl.pts = (node->timestamp - peshdl->shdl[id].vbufhdl.base_ts) * (90000 / 1000);
    }

    return ret;
}

STATIC VOID rtsp_server_task(PVOID_T pArg)
{
    void* phdl = NULL;
    rtsp_server_param_s param;
    tuya_rtsp_handle_s* peshdl = get_rtsp_thread_handle();

    TUYA_LOG("begin___\n");

    peshdl->task_exit = 1;

    memset(&param, 0, sizeof(rtsp_server_param_s));

    param.port = 8554;
    param.mode = RTSP_STREAM_MODE_SERVER;
    param.stream_src.priv = peshdl;
    param.stream_src.init = init;
    param.stream_src.uninit = uninit;
    param.stream_src.start = start;
    param.stream_src.stop = stop;
    param.stream_src.get_sdp = get_sdp;
    param.stream_src.get_max_frame_size = get_max_frame_size;
    param.stream_src.get_next_frame = get_next_frame;

    memset(peshdl->shdl, 0, RTSP_MAX_CLIENT_NUM * sizeof(tuya_stream_handle_s));

    int ret = rtsp_server_start(&phdl, &param);
    if(ret < 0) {
        TUYA_LOG("start rtsp server fail!ret:%d\n",ret);
        peshdl->task_exit = 0;
        return;
    }

    peshdl->start = 1;

    while(peshdl->start) {
        if(rtsp_server_get_es_status(phdl) == 0) {
            TUYA_LOG("no echoshow devecie connecting, stop it!\n");
            break;
        }

        usleep(50 * 1000);
    }

    rtsp_server_stop(&phdl);
    if(ret < 0) {
        TUYA_LOG("stop rtsp server fail!\n");
    }

    peshdl->task_exit = 0;

    TUYA_LOG("end___\n");

    return;
}

static void rtst_server_stop_task(tuya_rtsp_handle_s* peshdl)
{
    TUYA_LOG("begin___\n");

    if(peshdl->pthrdhdl != NULL) {
        peshdl->start = 0;
        DeleteThrdHandle(peshdl->pthrdhdl);
        peshdl->pthrdhdl = NULL;
    }

    int count = 20;

    do {
        usleep(300 * 1000);
    } while(peshdl->task_exit == 1 && count-- > 0);

    TUYA_LOG("end___, task_exit(%d), count(%d)\n", peshdl->task_exit, count);

    return;
}

STATIC VOID rtst_server_start_task(CONST CHAR_T *purl)
{
    OPERATE_RET op_ret;
    tuya_rtsp_handle_s* peshdl = get_rtsp_thread_handle();
    THRD_PARAM_S thrd_param = {4096, TRD_PRIO_1, (CHAR_T *)"rtsp_server_task"};

    TUYA_LOG("begin___\n");

    rtst_server_stop_task(peshdl);

    op_ret = CreateAndStart(&peshdl->pthrdhdl, NULL, NULL,
                            rtsp_server_task, NULL, &thrd_param);
    if(OPRT_OK != op_ret) {
        TUYA_LOG("start thread fails %d!\n", op_ret);
    }

    TUYA_LOG("end___\n");

    return;
}

OPERATE_RET tuya_ipc_rtsp_server_stop()
{
    tuya_rtsp_handle_s* peshdl = get_rtsp_thread_handle();

    TUYA_LOG("begin___\n");

    rtst_server_stop_task(peshdl);

    TUYA_LOG("end___\n");

    return OPRT_OK; 
}

OPERATE_RET tuya_ipc_rtsp_server_init(TUYA_RTSP_SERVER_PARAM_S* pparam)
{
    if(pparam == NULL) {
        return -1;
    }

    tuya_rtsp_handle_s* peshdl = get_rtsp_thread_handle();

    TUYA_LOG("begin___, video channel: %d\n", pparam->vchannel);

    peshdl->vchannel = pparam->vchannel;
    peshdl->vcodec.codec = pparam->pminfo->video_codec[peshdl->vchannel];
    peshdl->vcodec.video.fps = pparam->pminfo->video_fps[peshdl->vchannel];
    peshdl->vcodec.video.width = pparam->pminfo->video_width[peshdl->vchannel];
    peshdl->vcodec.video.height = pparam->pminfo->video_height[peshdl->vchannel];
    peshdl->vcodec.video.freq = pparam->pminfo->video_freq[peshdl->vchannel];
    peshdl->vcodec.step = peshdl->vcodec.video.freq/peshdl->vcodec.video.fps;

    peshdl->acodec.codec = pparam->pminfo->audio_codec[E_CHANNEL_AUDIO];

    if(pparam->pminfo->audio_channel[E_CHANNEL_AUDIO] == TUYA_AUDIO_CHANNEL_MONO) {
        peshdl->acodec.audio.channel = 1;
    } else if (pparam->pminfo->audio_channel[E_CHANNEL_AUDIO] == TUYA_AUDIO_CHANNEL_STERO) {
        peshdl->acodec.audio.channel = 2;
    }

    if(pparam->pminfo->audio_databits[E_CHANNEL_AUDIO] == TUYA_AUDIO_DATABITS_8) {
        peshdl->acodec.audio.bit_per_sample = 8;
    } else if (pparam->pminfo->audio_databits[E_CHANNEL_AUDIO] == TUYA_AUDIO_DATABITS_16) {
        peshdl->acodec.audio.bit_per_sample = 16;
    }

    if(pparam->pminfo->audio_sample[E_CHANNEL_AUDIO] == TUYA_AUDIO_SAMPLE_8K) {
        peshdl->acodec.audio.sample_rate = 8000;
    }

    peshdl->acodec.step = peshdl->acodec.audio.channel * (peshdl->acodec.audio.bit_per_sample/8);

    pthread_mutex_init(&peshdl->lock, NULL);

    rtst_server_start_task(NULL);

    TUYA_LOG("end___, video step(%d), audio step(%d)\n", peshdl->vcodec.step, peshdl->acodec.step);

    return OPRT_OK;
}

OPERATE_RET tuya_ipc_rtsp_server_deinit(VOID)
{
    tuya_rtsp_handle_s* peshdl = get_rtsp_thread_handle();

    TUYA_LOG("begin___\n");

    rtst_server_stop_task(peshdl);

    pthread_mutex_destroy(&peshdl->lock);

    TUYA_LOG("end___\n");

    return 0;
}

