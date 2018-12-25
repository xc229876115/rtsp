#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "uni_log.h"
#include "uni_thread.h"
#include "adapter_platform.h"

#include "tuya_ipc_echo_show.h"
#include "common/tuya_sm_utils.h"
#include "tuya_ipc_media.h"
#include "rtsp_server.h"
#include "tuya_ring_buffer.h"
#include "media_codec/tuya_g711_utils.h"
#include "tuya_ipc_mqt_proccess.h"
#include "tuya_tls.h"
#include "cloud_operation.h"
#include "tuya_stream_av.h"



#define AUDIO_RAW_LEN   1600
// #define TY_ECHOSHOW_DUMP_FILE


typedef struct tuya_echoshow_handle_ {
    int                     vchannel;
    int                     get_sps_pps;
    int                     start;
    int                     task_exit;
    char                    url[256];
    char                    rawbuf[3500];
    int                     rawlen;
    tuya_aiot_av_codec_t    acodec;
    tuya_aiot_av_codec_t    vcodec;
    THRD_HANDLE             pthrdhdl;
    TUYA_ECHOSHOW_CALLBACK  cbk;
    sm_es_av_handle_s       vbufhdl;
    sm_es_av_handle_s       abufhdl;
    int                     sockfd;
    tuya_tls_hander         tls_handler

#ifdef TY_ECHOSHOW_DUMP_FILE
    int                     pcmfd;
    int                     g711ufd;
#endif
} tuya_echoshow_handle_s;

typedef struct ty_rtsp_url_info_ {
    char tls;
    int port;
    char domain[64];
    char username[64];
    char password[64];
    char path[128];
} ty_rtsp_url_info_s;



static tuya_echoshow_handle_s* get_es_thread_handle()
{
    static tuya_echoshow_handle_s eshdl = {0};

    return &eshdl;
}

static int tuya_parse_domain_ip_address(const char *doname, char* ipAddr)
{
    char** pptr = NULL;
    struct hostent *hptr = NULL;
    char   str[32] = {0};

    if((hptr = gethostbyname(doname)) == NULL) {
        TUYA_LOG("host: %s\n", doname);
        return -1;
    }

    switch(hptr->h_addrtype) {
    case AF_INET:
    case AF_INET6:
        pptr = hptr->h_addr_list;
        for(; *pptr != NULL; pptr++) {
            strcpy(ipAddr, inet_ntop(hptr->h_addrtype, *pptr, str, sizeof(str)));
            return 0;
        }
        break;

    default:
        break;
    }

    return -1;
}

// rtsp://rtsp.tuyacn.com:18554/v1/proxy/ipc/2812987377548101079
// rtsp://username:password@domain:port/v/xxx/xxx
// Parse the URL as "rtsp://[<username>[:<password>]@]<server-address-or-name>[:<port>][/<path>]"
static int tuya_parse_rtsp_url_info(char const* url, ty_rtsp_url_info_s* pruinfo)
{
    uint32_t prefixLength = 7;  

    if(strncmp("rtsps", url, strlen("rtsps")) == 0) {
        prefixLength = 8;
        pruinfo->tls = 1;
        TUYA_LOG("need tls...\n");
    }

    char const* from = &url[prefixLength];
    char const* tmpPos = NULL;

    if ((tmpPos = strchr(from, '@')) != NULL) {  
        // found <username> (and perhaps <password>).  
        char const* usernameStart = from;  
        char const* passwordStart = NULL;  
        char const* p = tmpPos;  
  
        if ((tmpPos = strchr(from, ':')) != NULL && tmpPos < p) {  
            passwordStart = tmpPos + 1;
            uint32_t passwordLen = p - passwordStart;  
            strncpy(pruinfo->password, passwordStart, passwordLen);
            pruinfo->password[passwordLen] = '\0'; // Set the ending character.  
        }  
              
        uint32_t usernameLen = 0;  
        if (passwordStart != NULL) {
            usernameLen = tmpPos - usernameStart;  
        } else {  
            usernameLen = p - usernameStart;      
        }         
        strncpy(pruinfo->username, usernameStart, usernameLen);
        pruinfo->username[usernameLen] = '\0';  // Set the ending character.  
  
        from = p + 1; // skip the '@'  
    }  
  
    const char* pathStart = NULL;  
    if ((tmpPos = strchr(from, '/')) != NULL) {  
        uint32_t pathLen = strlen(tmpPos + 1);  //Skip '/'  
        strncpy(pruinfo->path, tmpPos + 1, pathLen + 1);  
        pathStart = tmpPos;  
    }  
  
    // Next, will parse the address and port.  
    tmpPos = strchr(from, ':');  
    if (tmpPos == NULL) {  
        if (pathStart == NULL) {  
            uint32_t addressLen = strlen(from);  
            strncpy(pruinfo->domain, from, addressLen + 1);  //Already include '\0'  
        } else {  
            uint32_t addressLen = pathStart - from;  
            strncpy(pruinfo->domain, from, addressLen);  
            pruinfo->domain[addressLen] = '\0';   //Set the ending character.  
        }

        pruinfo->port = 554; // Has not the specified port, and will use the default value  
    } else if (tmpPos != NULL) {  
        uint32_t addressLen = tmpPos - from;
        strncpy(pruinfo->domain, from, addressLen);  
        pruinfo->domain[addressLen] = '\0';  //Set the ending character.  
        pruinfo->port = strtoul(tmpPos + 1, NULL, 10);   
    }

    if(strlen(pruinfo->domain) <= 0) {
        return -1;
    }

    return 0;
}

static void tuya_setup_rtsp_option(const char* rtspUrl, char* pOptionStr)
{
    char *tmp = NULL;

    tmp = strstr((char *)rtspUrl, "@");
    if(tmp != NULL) {
        strcpy(pOptionStr, "OPTIONS rtsp://");
        strcat(pOptionStr, tmp + 1);
    } else {
        strcpy(pOptionStr, "OPTIONS ");
        strcat(pOptionStr, rtspUrl);
    }

    strcat(pOptionStr, " RTSP/1.0\r\nCseq: 27\r\nUser-Agent: VLC Media Player (LIVE.COM Streaming Media v2004.11.11)\r\n\r\n");

    return;
}

int echoshow_connect_cloud(char* rtspUrl, char* ip, int port, int* pskfd)
{
    int sockfd = -1;
    fd_set rfds, wfds;
    struct timeval tv;
    struct sockaddr_in addr;
    struct timeval timeout = {5, 0};

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        return -1;
    }

    TUYA_LOG("begin___\n");

    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(struct timeval));
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(struct timeval));

    addr.sin_family = AF_INET;
    addr.sin_port = UNI_HTONS(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    int ret = connect(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr));
    if (0 == ret) {
        ret = 0;
    } else {
        if (errno == EINPROGRESS) {
            FD_ZERO(&rfds);
            FD_ZERO(&wfds);
            FD_SET(sockfd, &rfds);
            FD_SET(sockfd, &wfds);

            tv.tv_sec = 10;
            tv.tv_usec = 0;

            int selres = select(sockfd + 1, &rfds, &wfds, NULL, &tv);
            switch (selres) {
            case -1:
                PR_ERR("select error\n");
                ret = -1;
                break;

            case 0:
                PR_WARN("select time out\n");
                ret = -1;
                break;

            default:
                if (FD_ISSET(sockfd, &rfds) || FD_ISSET(sockfd, &wfds)) {
                    ret = 0;
                }
            }
        }
    }

    if(ret == -1) {
        PR_ERR("Connect %s:%d failure!\n", ip, port);
        return -1;
    }

    *pskfd = sockfd;

    TUYA_LOG("end___\n");

    return ret;
}

static int echoshow_send_option(tuya_echoshow_handle_s* peshdl,
                                int sockfd, ty_rtsp_url_info_s* pruinfo)
{
    int ret = 0;
    char option_str[512] = {0};

    tuya_setup_rtsp_option(peshdl->url, option_str);

    if(pruinfo->tls) {
        ret = tuya_tls_write(peshdl->tls_handler, (BYTE_T *)option_str, strlen(option_str));
    } else {
        // ret = sendto(sockfd, option_str, strlen(option_str), 0,
        //             (struct sockaddr*)&addr, sizeof(struct sockaddr));
        ret = send(sockfd, option_str, strlen(option_str), 0);
    }

    if(ret < 0) {
        PR_ERR("send options fail, ret: %d\n", ret);
    }

    TUYA_LOG("end___\n");

    return ret;
}

static int tuya_tls_stream_send_cb( void *ctx, const unsigned char *buf, size_t len )
{
    tuya_echoshow_handle_s *s = (tuya_echoshow_handle_s *)ctx;

    return unw_send(s->sockfd, buf, len);
}

static int tuya_tls_stream_recv_cb( void *ctx, unsigned char *buf, size_t len )
{
    tuya_echoshow_handle_s *s = (tuya_echoshow_handle_s *)ctx;

    int err_cnt = 0;
    while(err_cnt < 20) {
        int result = unw_recv(s->sockfd, buf, len);
        if(result < 0) {
            UNW_ERRNO_T err = unw_get_errno();
            if(err == UNW_ENOMEM) {
                err_cnt++;
                PR_TRACE("tls http recv no memory.len:%d ret:%d err:%d. try recv again %d", len, result, err, err_cnt);
                SystemSleep(100);
                continue;
            }else if(err == UNW_EAGAIN) {
                err_cnt++;
                PR_TRACE("tls http recv again.len:%d ret:%d err:%d. try recv again %d", len, result, err, err_cnt);
                SystemSleep(10);
                continue;
            } else
            {
                unw_set_block(s->sockfd, TRUE);
                PR_ERR("tls http recv fail.len:%d ret:%d err:%d", len, result, err);
                return result;
            }
        } else {
            unw_set_block(s->sockfd, TRUE);
            err_cnt = 0;
            return result;
        }
    }
    unw_set_block(s->sockfd, TRUE);
    UNW_ERRNO_T err = unw_get_errno();
    PR_ERR("tls http recv fail.len:%d err:%d", len, err);

    return -1;
}

static int echoshow_interactive_server(tuya_echoshow_handle_s* peshdl, int* pskfd,
                                        rtsp_server_tls_param_s* ptls_param)
{
    char ipAddr[32] = {0};
    ty_rtsp_url_info_s ruinfo;

    memset(&ruinfo, 0, sizeof(ty_rtsp_url_info_s));

    TUYA_LOG("begin___\n");

    if(tuya_parse_rtsp_url_info(peshdl->url, &ruinfo) == 0) {
        if(tuya_parse_domain_ip_address(ruinfo.domain, ipAddr) < 0) {
            TUYA_LOG("parse domain fail...\n");
            return -1;
        }

        TUYA_LOG("user(%s) pwd(%s) domain(%s) ip(%s) port(%d) path(%s)\n",
               ruinfo.username, ruinfo.password, ruinfo.domain, ipAddr, ruinfo.port, ruinfo.path);
    } else {
        TUYA_LOG("parse url fail...\n");
        return -1;
    }

    int ret = echoshow_connect_cloud(peshdl->url, ipAddr, ruinfo.port, pskfd);

    ret = cloud_require_new_ca(ruinfo.domain,CA_TYPE_ECHO_SHOW);
    if(OPRT_OK != ret)
    {
        PR_ERR("update echo show ca fail%d",ret);
        return ret;
    }

    if(ruinfo.tls == 1) {
        peshdl->sockfd = *pskfd;
        tuya_tls_connect(&peshdl->tls_handler, ruinfo.domain, ruinfo.port, 0,
                         peshdl, tuya_tls_stream_send_cb, tuya_tls_stream_recv_cb, *pskfd, 10);
    }

    ret = echoshow_send_option(peshdl, *pskfd, &ruinfo);

    ptls_param->tls = ruinfo.tls;
    ptls_param->pssl = peshdl->tls_handler;

    TUYA_LOG("end___, ret(%d)\n", ret);

    return ret;
}

int start(void* thiz, rtsp_stream_identify_s* pidentify)
{
    if(thiz == NULL) {
        return -1;
    }

    TUYA_LOG("begin___\n");

    tuya_echoshow_handle_s* peshdl = (tuya_echoshow_handle_s*)thiz;

    if(pidentify->type == RTSP_STREAM_TYPE_AUDIO) {
        peshdl->rawlen = 0;
        memset(&peshdl->abufhdl, 0, sizeof(sm_es_av_handle_s));
    } else if (pidentify->type == RTSP_STREAM_TYPE_VIDEO) {
        peshdl->get_sps_pps = 0;
        memset(&peshdl->vbufhdl, 0, sizeof(sm_es_av_handle_s));
    }

#ifdef TY_ECHOSHOW_DUMP_FILE
    if(pidentify->type == RTSP_STREAM_TYPE_AUDIO) {
        peshdl->pcmfd = open("/mnt/sdcard/echoshow.pcm", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    } else if (pidentify->type == RTSP_STREAM_TYPE_VIDEO) {
        peshdl->g711ufd = open("/mnt/sdcard/echoshow.g711u", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    }
#endif

    TUYA_LOG("end___\n");

    return 0;
}

int stop(void* thiz, rtsp_stream_identify_s* pidentify)
{
    if(thiz == NULL) {
        return -1;
    }

    TUYA_LOG("begin___\n");

    tuya_echoshow_handle_s* peshdl = (tuya_echoshow_handle_s*)thiz;

#ifdef TY_ECHOSHOW_DUMP_FILE
    if(pidentify->type == RTSP_STREAM_TYPE_AUDIO) {
        close(peshdl->pcmfd);
    } else if (pidentify->type == RTSP_STREAM_TYPE_VIDEO) {
        close(peshdl->g711ufd);
    }
#endif

    TUYA_LOG("end___\n");

    return 0;
}

int get_sdp(void* thiz, rtsp_stream_identify_s* pidentify, char* buf, int* size)
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

int get_max_frame_size(void* thiz, rtsp_stream_identify_s* pidentify)
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

int get_next_frame(void* thiz, rtsp_stream_identify_s* pidentify, rtsp_stream_info_s* psvif)
{
    if(thiz == NULL) {
        return -1;
    }

    int ret = -1;
    Ring_Buffer_Node_S *node = NULL;
    tuya_echoshow_handle_s* peshdl = (tuya_echoshow_handle_s*)thiz;

    uint64_t now = tuya_time_utc_ms();

    if(pidentify->type == RTSP_STREAM_TYPE_AUDIO && peshdl->get_sps_pps) {
        node = tuya_ipc_ring_buffer_get_audio_frame(E_CHANNEL_AUDIO, E_USER_ECHO_SHOW, 0);
        if(node == NULL || node->size <= 0) {
            return -1;
        }

#ifdef TY_ECHOSHOW_DUMP_FILE
        write(peshdl->pcmfd, node->rawData, node->size);
#endif

        if(peshdl->abufhdl.base_ts == 0) {
            peshdl->abufhdl.base_ts = node->timestamp;
        }

        memcpy(peshdl->rawbuf + peshdl->rawlen, node->rawData, node->size);
        peshdl->rawlen += node->size;

        if(peshdl->rawlen < AUDIO_RAW_LEN) {
            return 1;
        }

        unsigned char g711_buffer[1600] = {0};
        unsigned char* ptr = peshdl->rawbuf;
        unsigned int nBytesRead = peshdl->rawlen;

        if(peshdl->acodec.codec == TUYA_CODEC_AUDIO_PCM) {
            tuya_g711_encode(TUYA_G711_MU_LAW, (unsigned short *)peshdl->rawbuf,
                             AUDIO_RAW_LEN, g711_buffer, &nBytesRead);
            ptr = g711_buffer;
#ifdef TY_ECHOSHOW_DUMP_FILE
            write(peshdl->g711ufd, g711_buffer, nBytesRead);
#endif
        }

        if(nBytesRead > 0) {
            memcpy(psvif->buf, ptr, nBytesRead);

            psvif->ts = peshdl->abufhdl.pts;
            psvif->size = nBytesRead;
            peshdl->abufhdl.pts = (node->timestamp - peshdl->abufhdl.base_ts) * (8000 / 1000);

            if(peshdl->acodec.codec == TUYA_CODEC_AUDIO_PCM) {
                char temp[1024] = {0};
                peshdl->rawlen -= AUDIO_RAW_LEN;
                memcpy(temp, peshdl->rawbuf + AUDIO_RAW_LEN, peshdl->rawlen);
                memcpy(peshdl->rawbuf, temp, peshdl->rawlen);
            } else {
                peshdl->rawlen = 0;
            }
        }

        ret = 0;
    } else if(pidentify->type == RTSP_STREAM_TYPE_VIDEO) {
        node = tuya_ipc_ring_buffer_get_video_frame(peshdl->vchannel, E_USER_ECHO_SHOW, 0);
        if(node == NULL || node->size <= 0) {
            return -1;
        }

        ret = 0;

        u_char* ptr = (u_char *)node->rawData;
        int nNaluType = ptr[4] & 0x1f;

        if(peshdl->get_sps_pps != 1) {
            if(nNaluType == 0x07) {
                peshdl->get_sps_pps = 1;
                TUYA_LOG("video get sps pps info...\n");
            } else {
                return 0;
            }
        }

        if(peshdl->vbufhdl.base_ts == 0) {
            peshdl->vbufhdl.base_ts = node->timestamp;
        }

        memcpy(psvif->buf, node->rawData, node->size);

        psvif->ts = peshdl->vbufhdl.pts;
        psvif->size = node->size;
        peshdl->vbufhdl.pts = (node->timestamp - peshdl->vbufhdl.base_ts) * (90000 / 1000);
    }

    return ret;
}

STATIC VOID echoshow_task(PVOID_T pArg)
{
    int sockfd = -1;
    void* phdl = NULL;
    rtsp_server_param_s param;
    rtsp_server_tls_param_s tls_param;
    tuya_echoshow_handle_s* peshdl = get_es_thread_handle();

    TUYA_LOG("begin___\n");

    peshdl->task_exit = 1;

    if(peshdl->cbk.start  != NULL) {
        if(peshdl->cbk.start(peshdl->cbk.pcontext, NULL) != 0) {
            TUYA_LOG("end___, no need to start rtsp server!\n");
            peshdl->task_exit = 0;
            return;
        }
    }

    memset(&param, 0, sizeof(rtsp_server_param_s));
    memset(&tls_param, 0, sizeof(rtsp_server_tls_param_s));

    param.port = 8554;
    param.mode = RTSP_STREAM_MODE_SERVER;
    param.stream_src.priv = peshdl;
    param.stream_src.init = NULL;
    param.stream_src.uninit = NULL;
    param.stream_src.start = start;
    param.stream_src.stop = stop;
    param.stream_src.get_sdp = get_sdp;
    param.stream_src.get_max_frame_size = get_max_frame_size;
    param.stream_src.get_next_frame = get_next_frame;

    if(strlen(peshdl->url) > 0) {
        if(echoshow_interactive_server(peshdl, &sockfd, &tls_param) < 0) {
            TUYA_LOG("connect server error!\n");
            peshdl->task_exit = 0;
            return;
        }

        param.mode = RTSP_STREAM_MODE_ECHOSHOW;

        peshdl->abufhdl.pts = 0;
        peshdl->vbufhdl.pts = 0;
    }

    int ret = rtsp_server_start(&phdl, &param);
    if(ret < 0) {
        TUYA_LOG("start rtsp server fail!ret:%d\n",ret);
        peshdl->task_exit = 0;
        return;
    }

    if(sockfd > 0) {
        rtsp_server_start_according_sockfd(phdl, sockfd, &tls_param);
    }

    peshdl->start = 1;

    while(peshdl->start) {
        // usleep(500 * 1000);

        if(sockfd > 0 && rtsp_server_get_es_status(phdl) == 0) {
            TUYA_LOG("no echoshow devecie connecting, stop it!\n");
            break;
        }

        usleep(50 * 1000);
    }

    if(peshdl->cbk.stop  != NULL) {
        peshdl->cbk.stop(peshdl->cbk.pcontext, NULL);
    }

    rtsp_server_stop(&phdl);
    if(ret < 0) {
        TUYA_LOG("stop rtsp server fail!\n");
    }

    if(tls_param.tls) {
        tuya_tls_disconnect(peshdl->tls_handler);
    }

    if(sockfd > 0) {
        close(sockfd);
        sockfd = -1;
    }

    peshdl->task_exit = 0;

    TUYA_LOG("end___\n");

    return;
}

static void echoshow_stop_task(tuya_echoshow_handle_s* peshdl)
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

STATIC VOID echoshow_start_task(CONST CHAR_T *purl)
{
    OPERATE_RET op_ret;
    tuya_echoshow_handle_s* peshdl = get_es_thread_handle();
    THRD_PARAM_S thrd_param = {1024, TRD_PRIO_1, (CHAR_T *)"echoshow_task"};

    TUYA_LOG("begin___\n");

    echoshow_stop_task(peshdl);

    if(purl != NULL && strlen(purl) > 0) {
        strncpy(peshdl->url, purl, sizeof(peshdl->url));
    }

    op_ret = CreateAndStart(&peshdl->pthrdhdl, NULL, NULL,
                            echoshow_task, NULL, &thrd_param);
    if(OPRT_OK != op_ret) {
        TUYA_LOG("start thread fails %d!\n", op_ret);
    }

    TUYA_LOG("end___\n");

    return;
}

OPERATE_RET tuya_ipc_echoshow_stop()
{
    tuya_echoshow_handle_s* peshdl = get_es_thread_handle();

    TUYA_LOG("begin___\n");

    echoshow_stop_task(peshdl);

    TUYA_LOG("end___\n");

    return OPRT_OK; 
}

OPERATE_RET tuya_ipc_echoshow_init(TUYA_ECHOSHOW_PARAM_S* pparam)
{
    if(pparam == NULL) {
        return -1;
    }

    tuya_echoshow_handle_s* peshdl = get_es_thread_handle();

    TUYA_LOG("begin___, video channel: %d\n", pparam->vchannel);

	tuya_ipc_mqt_EchoShowCb(echoshow_start_task);

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

    memcpy(&peshdl->cbk, &pparam->cbk, sizeof(TUYA_ECHOSHOW_CALLBACK));

    if(pparam->mode == TUYA_ECHOSHOW_MODE_RTSP) {
        echoshow_start_task(NULL);
    }

    TUYA_LOG("end___, video step(%d), audio step(%d)\n", peshdl->vcodec.step, peshdl->acodec.step);

    return OPRT_OK;
}

OPERATE_RET tuya_ipc_echoshow_deinit(VOID)
{
    tuya_echoshow_handle_s* peshdl = get_es_thread_handle();

    TUYA_LOG("begin___\n");

    echoshow_stop_task(peshdl);

    TUYA_LOG("end___\n");

    return 0;
}

