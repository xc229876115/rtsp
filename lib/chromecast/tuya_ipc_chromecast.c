#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#include "uni_log.h"
#include "uni_thread.h"
#include "gw_intf.h"
#include "tuya_ring_buffer.h"
#include "tuya_cloud_error_code.h"
#include "tuya_cloud_types.h"

#include "tuya_ipc_chromecast.h"
#include "flv/tuya_flv_mux.h"
#include "tuya_rtmp_client.h"
#include "common/tuya_sm_utils.h"
#include "tuya_ipc_mqt_proccess.h"



// #define DUMP_RAW_TO_FILE

#define CHROMECAST_PUSH_DURATION    (5 * 60)
#define CHROMECAST_DROP_FRAME_TIME  3500

typedef struct tuya_av_time_info_ {
    char     start;
    char     drop_frame;
    uint32_t count;
    uint64_t base_pts;
    uint64_t duration;
    uint64_t utcms;
} tuya_av_info_s;

typedef struct tuya_chromecast_handle_ {
    void*                       pfmuxhdl;
    void*                       prtmphdl;
    THRD_HANDLE                 pthrdhdl;
    char                        url[256];
    char                        get_sps_pps;
    uint64_t                    mqtt_msg_time;

    tuya_av_info_s              videoti;
    tuya_av_info_s              audioti;

    int                         afps;
    int                         vfps;
    CHANNEL_E                   audioChannel;
    CHANNEL_E                   videoChannel;
    TUYA_STREAM_SOURCE_E        src;
    TUYA_STREAM_TRANSMIT_MODE_E mode;
    TUYA_CHROMECAST_CALLBACK    cbk;
    volatile char                    taskstart;
    volatile char                   taskstop;

#ifdef DUMP_RAW_TO_FILE
    int         flvfd;
#endif
} tuya_chromecast_handle_s;


static tuya_chromecast_handle_s* get_cc_thread_handle()
{
    static tuya_chromecast_handle_s cchdl = {0};

    return &cchdl;
}

void flv_packet_data(const void *context, const u_char *data, const int size, const int pretagsize)
{
    tuya_chromecast_handle_s* phdl = (tuya_chromecast_handle_s*)context;
    if(phdl == NULL) {
        TUYA_LOG("context is null\n");
        return;
    }

    if(size > 11) {
        if(tuya_rtmp_send_stream(phdl->prtmphdl, (char*)data, size) < 0) {
            TUYA_LOG("set data to rtmp module error!\n");
        }
    }

#ifdef DUMP_RAW_TO_FILE
    write(phdl->flvfd, (char *)data, size);
    write(phdl->flvfd, (char *)&pretagsize, sizeof(int));
#endif

    return;
}

static int chromecast_create_env(tuya_chromecast_handle_s* pchrhdl)
{
    int ret = -1;
    tuya_flv_mux_params_s param;
    tuya_flv_packet_callback_s callback;

    TUYA_LOG("begin___\n");

    memset(&param, 0, sizeof(tuya_flv_mux_params_s));
    memset(&callback, 0, sizeof(tuya_flv_packet_callback_s));
    memset(&pchrhdl->videoti, 0, sizeof(tuya_av_info_s));
    memset(&pchrhdl->audioti, 0, sizeof(tuya_av_info_s));

    pchrhdl->get_sps_pps = 0;

    param.audio = 1;
    param.video = 1;

    if(pchrhdl->cbk.start  != NULL) {
        ret = pchrhdl->cbk.start(pchrhdl->cbk.pcontext, NULL);
        if(ret != 0) {
            TUYA_LOG("end___, no need to connect rtmp server\n");
            return ret;
        }
    }

#ifdef DUMP_RAW_TO_FILE
    param.file = 1;
    pchrhdl->flvfd = open("/mnt/mmc01/test.flv", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
#endif

    callback.priv = pchrhdl;
    callback.flv_packet_data = flv_packet_data;

    ret = tuya_rtmp_create(pchrhdl->url, pchrhdl->mode, &pchrhdl->prtmphdl);
    if(ret < 0) {
        TUYA_LOG("can not create rtmp connect!\n");
        return ret;
    }

    tuya_flv_mux_create(&pchrhdl->pfmuxhdl, &param, &callback);

    ret = 0;

    TUYA_LOG("end___, ret: %d\n", ret);

    return ret;
}

static int chromecast_destroy_env(tuya_chromecast_handle_s* pchrhdl)
{
    TUYA_LOG("begin___\n");

    pchrhdl->get_sps_pps = 0;

    if(pchrhdl->cbk.stop != NULL) {
        pchrhdl->cbk.stop(pchrhdl->cbk.pcontext, NULL);
    }

#ifdef DUMP_RAW_TO_FILE
    close(pchrhdl->flvfd);
#endif

    if(pchrhdl->pfmuxhdl != NULL) {
        tuya_flv_mux_destroy(&pchrhdl->pfmuxhdl);
        pchrhdl->pfmuxhdl = NULL;
    }

    if(pchrhdl->prtmphdl != NULL) {
        tuya_rtmp_destroy(&pchrhdl->prtmphdl);
        pchrhdl->prtmphdl = NULL;
    }

    TUYA_LOG("end___\n");

    return 0;
}

static int chromecast_av_drop_audio_frame(tuya_chromecast_handle_s* pchrhdl)
{
    int ret = -1;
    Ring_Buffer_Node_S *node = NULL;

    do {
        node = tuya_ipc_ring_buffer_get_audio_frame(pchrhdl->audioChannel, E_USER_CHROMECAST, 0);
        if(node == NULL || node->size <= 0) {
            break;
        }

        pchrhdl->audioti.utcms = node->timestamp;
    } while(pchrhdl->videoti.utcms > pchrhdl->audioti.utcms);

    if(pchrhdl->videoti.utcms < pchrhdl->audioti.utcms) {
        pchrhdl->audioti.duration = pchrhdl->videoti.duration;
        pchrhdl->videoti.drop_frame = 0;
        ret = 0;

        TUYA_LOG("audio duration: %lld, video duration: %lld\n",
                pchrhdl->audioti.duration, pchrhdl->videoti.duration);
    }

    // if(pchrhdl->audioLen > 0) {
    //     memset(pchrhdl->pcmBuffer, 0, pchrhdl->audioLen);
    //     pchrhdl->audioLen = 0;
    // }

    return ret;
}

static int chromecast_pack_audio_data(tuya_chromecast_handle_s* pchrhdl)
{
    Ring_Buffer_Node_S *node = NULL;

    if(pchrhdl->videoti.drop_frame) {
        if(chromecast_av_drop_audio_frame(pchrhdl) < 0) {
            TUYA_LOG("continue to drop audio frame...\n");
            return -1;
        }
    }

    if(pchrhdl->audioti.start == 0) {
        node = tuya_ipc_ring_buffer_get_pre_audio_frame(pchrhdl->audioChannel, E_USER_CHROMECAST, 4 * pchrhdl->afps);
        pchrhdl->audioti.start = 1;
    } else {
        node = tuya_ipc_ring_buffer_get_audio_frame(pchrhdl->audioChannel, E_USER_CHROMECAST, 0);
    }

    if(node == NULL || node->size <= 0) {
        return -1;
    }

    if(pchrhdl->audioti.utcms != 0) {
        if(node->timestamp - pchrhdl->audioti.utcms >= CHROMECAST_DROP_FRAME_TIME) {
            pchrhdl->audioti.drop_frame = 1;
            TUYA_LOG("buffer drop audio, vcnt(%d) acnt(%d), need to drop video frame...\n",
                    pchrhdl->videoti.count, pchrhdl->audioti.count);
            pchrhdl->audioti.duration += node->timestamp - pchrhdl->audioti.utcms;
        }
    }

    if(pchrhdl->audioti.base_pts == 0) {
        pchrhdl->audioti.base_pts = node->pts;
    }

    pchrhdl->audioti.duration = (node->pts - pchrhdl->audioti.base_pts)/1000;

    tuya_flv_mux_convert_aac(pchrhdl->pfmuxhdl, (u_char *)node->rawData, node->size, pchrhdl->audioti.duration);

    pchrhdl->audioti.utcms = node->timestamp;
    pchrhdl->audioti.count++;

    // TUYA_LOG("ts(%llu) duration(%llu) count(%d)\n",
    //       pchrhdl->audioti.utcms, pchrhdl->audioti.duration, pchrhdl->audioti.count);

    return 0;
}

static int chromecast_av_drop_video_frame(tuya_chromecast_handle_s* pchrhdl)
{
    int ret = -1;
    Ring_Buffer_Node_S *node = NULL;

    do {
        node = tuya_ipc_ring_buffer_get_video_frame(pchrhdl->videoChannel, E_USER_CHROMECAST, 0);
        if(node == NULL || node->size <= 0) {
            break;
        }

        pchrhdl->videoti.utcms = node->timestamp;
    } while(pchrhdl->audioti.utcms > pchrhdl->videoti.utcms);

    if(pchrhdl->videoti.utcms >= pchrhdl->audioti.utcms) {
        pchrhdl->videoti.duration = pchrhdl->audioti.duration;
        pchrhdl->audioti.drop_frame = 0;
        ret = 0;

        TUYA_LOG("audio duration: %lld, video duration: %lld\n",
                pchrhdl->audioti.duration, pchrhdl->videoti.duration);
    }

    return ret;
}

static int chromecast_pack_video_data(tuya_chromecast_handle_s* pchrhdl)
{
    Ring_Buffer_Node_S *node = NULL;

    if(pchrhdl->audioti.drop_frame) {
        if(chromecast_av_drop_video_frame(pchrhdl) < 0) {
            TUYA_LOG("continue to drop video frame...\n");
            return -1;
        }
    }

    if(pchrhdl->videoti.start == 0) {
        node = tuya_ipc_ring_buffer_get_pre_video_frame(pchrhdl->videoChannel, E_USER_CHROMECAST, 4 * pchrhdl->vfps);
        pchrhdl->videoti.start = 1;
    } else {
        node = tuya_ipc_ring_buffer_get_video_frame(pchrhdl->videoChannel, E_USER_CHROMECAST, 0);
    }
    if(node == NULL || node->size <= 0) {
        return -1;
    }

    char succ = 1;
    int nInSize = node->size;
    u_char* ptr = (u_char *)node->rawData;
    int nNaluSize = 0;

    while((ptr = get_one_nalu(ptr, nInSize, &nNaluSize)) != NULL) {
        if(pchrhdl->get_sps_pps != 1) {
            int nNaluType = ptr[4] & 0x1f;

            if(nNaluType == 0x07) {
                pchrhdl->get_sps_pps = 1;
                TUYA_LOG("video get sps pps info, start to mux flv packet...\n");
            } else {
                succ = 0;
                break;
            }
        }

        if(pchrhdl->videoti.base_pts == 0) {
            pchrhdl->videoti.base_pts = node->pts;
        }

        pchrhdl->videoti.duration = (node->pts - pchrhdl->videoti.base_pts)/1000;

        tuya_flv_mux_convert_h264(pchrhdl->pfmuxhdl, ptr, nNaluSize,
                                    pchrhdl->videoti.duration, 0);

        ptr += nNaluSize;
        nInSize -= nNaluSize;
    }

    if(succ) {
        if(pchrhdl->videoti.utcms != 0) {
            if(node->timestamp - pchrhdl->videoti.utcms >= CHROMECAST_DROP_FRAME_TIME) {
                pchrhdl->videoti.drop_frame = 1;
                TUYA_LOG("buffer drop video, vcnt(%d) acnt(%d), need to drop audio frame...\n",
                    pchrhdl->videoti.count, pchrhdl->audioti.count);
            }
        }

        pchrhdl->videoti.utcms = node->timestamp;
        pchrhdl->videoti.count++;

        // TUYA_LOG("ts(%llu) duration(%llu) count(%d)\n",
        //     pchrhdl->videoti.utcms, pchrhdl->videoti.duration, pchrhdl->videoti.count);
    }

    return 0;
}

static int chromecast_pack_audio_frame_from_customer(tuya_chromecast_handle_s* pchrhdl)
{
    int ret = -1;
    TUYA_STREAM_FRAME_S frame;

    ret = pchrhdl->cbk.get_frame(pchrhdl->cbk.pcontext, TUYA_STREAM_TYPE_AUDIO, &frame);
    if(ret < 0) {
        return -1;
    }

    if(pchrhdl->audioti.base_pts == 0) {
        pchrhdl->audioti.base_pts = frame.pts;
    }

    pchrhdl->audioti.duration = (frame.pts - pchrhdl->audioti.base_pts)/1000;

    tuya_flv_mux_convert_aac(pchrhdl->pfmuxhdl, frame.pbuff, frame.length, pchrhdl->audioti.duration);

    pchrhdl->audioti.utcms = frame.utcms;
    pchrhdl->audioti.count++;

    if(pchrhdl->cbk.get_frame_release != NULL) {
        pchrhdl->cbk.get_frame_release(pchrhdl->cbk.pcontext, TUYA_STREAM_TYPE_AUDIO, &frame);
    }

    // TUYA_LOG("ts(%llu) duration(%llu) count(%d)\n",
    //     pchrhdl->audioti.utcms, pchrhdl->audioti.duration, pchrhdl->audioti.count);

    return 0;
}

static int chromecast_pack_video_frame_from_customer(tuya_chromecast_handle_s* pchrhdl)
{
    int ret = -1;
    TUYA_STREAM_FRAME_S frame;

    ret = pchrhdl->cbk.get_frame(pchrhdl->cbk.pcontext, TUYA_STREAM_TYPE_VIDEO, &frame);
    if(ret < 0) {
        return -1;
    }

    char succ = 1;
    int nInSize = frame.length;
    u_char* ptr = (u_char *)frame.pbuff;
    int nNaluSize = 0;

    while((ptr = get_one_nalu(ptr, nInSize, &nNaluSize)) != NULL) {
        if(pchrhdl->get_sps_pps != 1) {
            int nNaluType = ptr[4] & 0x1f;

            if(nNaluType == 0x07) {
                pchrhdl->get_sps_pps = 1;
                TUYA_LOG("video get sps pps info, start to mux flv packet...\n");
            } else {
                succ = 0;
                break;
            }
        }

        if(pchrhdl->videoti.base_pts == 0) {
            pchrhdl->videoti.base_pts = frame.pts;
        }

        pchrhdl->videoti.duration = (frame.pts - pchrhdl->videoti.base_pts)/1000;

        tuya_flv_mux_convert_h264(pchrhdl->pfmuxhdl, ptr, nNaluSize,
                                    pchrhdl->videoti.duration, 0);

        ptr += nNaluSize;
        nInSize -= nNaluSize;
    }

    if(succ) {
        pchrhdl->videoti.utcms = frame.utcms;
        pchrhdl->videoti.count++;

        // TUYA_LOG("ts(%llu) duration(%llu) count(%d)\n",
        //     pchrhdl->videoti.utcms, pchrhdl->videoti.duration, pchrhdl->videoti.count);
    }

    if(pchrhdl->cbk.get_frame_release != NULL) {
        pchrhdl->cbk.get_frame_release(pchrhdl->cbk.pcontext, TUYA_STREAM_TYPE_VIDEO, &frame);
    }

    return 0;
}

int chromecast_get_frame_from_customer(tuya_chromecast_handle_s* pchrhdl)
{
    int ret = -1;

    if(pchrhdl->cbk.get_frame == NULL) {
        return -1;
    }

    if(!pchrhdl->get_sps_pps
        || pchrhdl->videoti.duration <= pchrhdl->audioti.duration) {
        ret = chromecast_pack_video_frame_from_customer(pchrhdl);
    }

    if(pchrhdl->get_sps_pps
        && pchrhdl->audioti.duration <= pchrhdl->videoti.duration) {
        ret = chromecast_pack_audio_frame_from_customer(pchrhdl);
    }

    return ret;
}

int chromecast_get_frame_from_ringbuffer(tuya_chromecast_handle_s* pchrhdl)
{
    int ret = -1;

    if(pchrhdl->get_sps_pps
        && pchrhdl->audioti.duration <= pchrhdl->videoti.duration) {
        ret = chromecast_pack_audio_data(pchrhdl);
    }

    if(!pchrhdl->get_sps_pps
        || pchrhdl->videoti.duration <= pchrhdl->audioti.duration) {
        ret = chromecast_pack_video_data(pchrhdl);
    }

    return ret;
}

STATIC VOID chromecast_task(IN PVOID_T pArg)
{
    int ret = -1;
    int sleepms = 0;
    tuya_chromecast_handle_s* pchrhdl = get_cc_thread_handle();
    uint64_t start_time = 0;

    TUYA_LOG("begin___\n");

    prctl(PR_SET_NAME, "chromecast");

    if(chromecast_create_env(pchrhdl) < 0) {
        chromecast_destroy_env(pchrhdl);
        return;
    }

    start_time = tuya_flv_time_utc_sec();
    pchrhdl->taskstop = 0;
    pchrhdl->taskstart = 1;

    while(pchrhdl->taskstart) {
        switch(pchrhdl->src) {
        case TUYA_STREAM_SOURCE_RINGBUFFER:
            ret = chromecast_get_frame_from_ringbuffer(pchrhdl);
            break;

        case TUYA_STREAM_SOURCE_CUSTOMER:
            ret = chromecast_get_frame_from_customer(pchrhdl);
            break;

        default:
            break;
        }
        if(rtmp_get_disconnect_status(&pchrhdl->prtmphdl))
        {
            TUYA_LOG("no chromecast devecie connecting, stop it!\n");
            break;
        }
/*
        uint64_t now = tuya_flv_time_utc_sec();
        if(now - start_time >= CHROMECAST_PUSH_DURATION) {
            TUYA_LOG("push stream out of time, exit...\n");
            break;
        }
*/
        if(ret < 0) {
            sleepms = 5 * 1000;
        } else {
            sleepms = 0;
        }

        usleep(sleepms);
    }

    chromecast_destroy_env(pchrhdl);
    pchrhdl->taskstop = 1;
    TUYA_LOG("end___\n");

    return;
}

void chromecast_stop()
{
    tuya_chromecast_handle_s* pchrhdl = get_cc_thread_handle();
    int count = 8;
    if(pchrhdl->pthrdhdl != NULL) {
        pchrhdl->taskstart = 0;
        DeleteThrdHandle(pchrhdl->pthrdhdl);
    
        pchrhdl->pthrdhdl = NULL;

        do 
        {
            usleep(500 * 1000);
        } while(pchrhdl->taskstop == 0 && count-- > 0);
    }

    return;
}

VOID chromecast_start(IN CHAR_T *url)
{
    tuya_chromecast_handle_s* pchrhdl = get_cc_thread_handle();
    THRD_PARAM_S thrd_param = {1024, TRD_PRIO_1, "chromecast_task"};

    if(url == NULL || strlen(url) <= 0) {
        TUYA_LOG("not set url!\n");
        return;
    }

    uint64_t now = tuya_flv_time_utc_sec();
    if(now - pchrhdl->mqtt_msg_time <= 3) {
        TUYA_LOG("recv more request in short time...\n");
        return;
    }

    TUYA_LOG("begin___\n");

    pchrhdl->mqtt_msg_time = now;

    chromecast_stop();  // stop first

    strncpy(pchrhdl->url, url, sizeof(pchrhdl->url));

    OPERATE_RET op_ret = CreateAndStart(&(pchrhdl->pthrdhdl), NULL, NULL, \
                            chromecast_task, NULL, &thrd_param);
    if(op_ret != OPRT_OK) {
        TUYA_LOG("create chromecast task fail!\n");
    }

    TUYA_LOG("end___\n");

    return;
}

OPERATE_RET tuya_ipc_chromecast_stop()
{
    TUYA_LOG("begin___\n");

    chromecast_stop();

    TUYA_LOG("end___\n");

    return OPRT_OK;
}

OPERATE_RET tuya_ipc_chromecast_init(TUYA_CHROMECAST_PARAM_S* param)
{
    tuya_ipc_mqt_ChromeCastCb(chromecast_start);
    // chromecast_start("rtmp://p2p.tuyacn.com/hls/mystream");

    if(param != NULL) {
        tuya_chromecast_handle_s* pchrhdl = get_cc_thread_handle();

        pchrhdl->src = param->src;
        pchrhdl->mode =  param->mode;
        pchrhdl->audioChannel = param->audio_channel;
        pchrhdl->videoChannel = param->video_channel;
      //  pchrhdl->afps = param->pminfo->audio_fps[param->audio_channel];
     //   pchrhdl->vfps = param->pminfo->video_fps[param->video_channel];
	    pchrhdl->afps = param->pminfo->audio_fps;
	    pchrhdl->vfps = param->pminfo->video_fps[param->video_channel];

        memcpy(&pchrhdl->cbk, &param->cbk, sizeof(TUYA_CHROMECAST_CALLBACK));

        TUYA_LOG("video: chl(%d), fps(%d) audio: chl(%d), fps(%d)\n",
              pchrhdl->videoChannel, pchrhdl->vfps, pchrhdl->audioChannel, pchrhdl->afps);
    }

    return OPRT_OK;
}

OPERATE_RET tuya_ipc_chromecast_deinit(VOID)
{
    tuya_ipc_mqt_ChromeCastCb(NULL);

    return OPRT_OK;
}

