
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>

#include "rtspservice.h"
#include "rtputils.h"
#include "ringfifo.h"

extern int g_s32Quit ;

static char s_raw_path[128] = {0};

/*
 * 示例代码采用文件读写方式来模拟音视频请求，文件在rawfiles.tar.gz中
 */
#define AUDIO_FRAME_SIZE 640
#define AUDIO_FPS 25

#define VIDEO_BUF_SIZE	(1024 * 400)


/*
使用读写文件的方式来模拟音频输出。
*/
void *thread_live_audio(void *arg)
{
    char fullpath[128] = {0};
    sprintf(fullpath, "%s/rawfiles/jupiter_8k_16bit_mono.raw", s_raw_path);

    FILE *aFp = fopen(fullpath, "rb");
    if(aFp == NULL)
    {
        printf("can't read live audio files\n");
        pthread_exit(0);
    }
    char audioBuf[AUDIO_FRAME_SIZE];

    while(1)
    {
        int size = fread(audioBuf, 1, AUDIO_FRAME_SIZE, aFp);
        if(size < AUDIO_FRAME_SIZE)
        {
            rewind(aFp);
            continue;
        }

#if 0
        TUYA_APP_Put_Frame(E_CHANNEL_AUDIO,&pcm_frame);
#endif 
    
		PutPCMDataToBuffer(audioBuf,size);
        int frameRate = AUDIO_FPS;
        int sleepTick = (int)(1000000.0/frameRate);
        usleep(sleepTick);
    }

    pthread_exit(0);
}

/* 使用读写文件的方式来模拟直播视频。*/
void *thread_live_video(void *arg)
{
    char raw_fullpath[128] = {0};
    char info_fullpath[128] = {0};

    sprintf(raw_fullpath, "%s/rawfiles/video_multi/beethoven_240.multi/frames.bin", s_raw_path);
    sprintf(info_fullpath, "%s/rawfiles/video_multi/beethoven_240.multi/frames.info", s_raw_path);

    FILE *streamBin_fp = fopen(raw_fullpath, "rb");
    FILE *streamInfo_fp = fopen(info_fullpath, "rb");
    if((streamBin_fp == NULL)||(streamInfo_fp == NULL))
    {
        printf("can't read live video files\n");
        pthread_exit(0);
    }

    char line[128] = {0}, *read = NULL;
    int fps = 30;
    read = fgets(line, sizeof(line), streamInfo_fp);
    sscanf(line, "FPS %d\n", &fps);

    unsigned char videoBuf[VIDEO_BUF_SIZE];

//    MEDIA_FRAME_S h264_frame = {0};
    while(1)
    {
        read = fgets(line, sizeof(line), streamInfo_fp);
        if(read == NULL)
        {
        	printf("rewind video file\n");
            rewind(streamBin_fp);
            rewind(streamInfo_fp);
            read = fgets(line, sizeof(line), streamInfo_fp);

            continue;
        }

        char frame_type[2] = {0};
        int frame_pos = 0, frame_size = 0, nRet = 0;
        sscanf(line, "%c %d %d\n", frame_type, &frame_pos, &frame_size);

        fseek(streamBin_fp, frame_pos*sizeof(char), SEEK_SET);
        nRet = fread(videoBuf, 1, frame_size, streamBin_fp);
        if(nRet < frame_size)
        {
        	printf("rewind video file\n");
            rewind(streamBin_fp);
            rewind(streamInfo_fp);
            read = fgets(line, sizeof(line), streamInfo_fp);
            continue;
        }

        //注意：部分编码器出I帧时SPS/PPS/SEI/IDR分开上传，需要合并为一个连续帧后传入，且不能删除NALU的分割符
//        h264_frame.type = (strcmp(frame_type, "I") == 0 ? E_VIDEO_I_FRAME: E_VIDEO_PB_FRAME);
//        h264_frame.p_buf = videoBuf;
//        h264_frame.size = nRet;
//        h264_frame.pts = 0;
		int iframe = (strcmp(frame_type, "I") == 0 ? 1: 0);
//		printf("video size = %d , iframe = %d\n",nRet,iframe);
		PutH264DataToBuffer(videoBuf,nRet,iframe);
		static FILE *fp = NULL;
		if(NULL == fp)
			fp = fopen("save.264","wb");
		fwrite(videoBuf,1,nRet,fp);
		fflush(fp);
			
#if 0
        /* 将高清视频数据送入SDK */
        TUYA_APP_Put_Frame(E_CHANNEL_VIDEO_MAIN, &h264_frame);
        /* 将标清视频数据送入SDK */
        TUYA_APP_Put_Frame(E_CHANNEL_VIDEO_SUB, &h264_frame);
#endif

        int frameRate = fps;
        int sleepTick = (int)(1000000.0/frameRate);
        usleep(sleepTick);
    }

    pthread_exit(0);
}

void GetSdpDescr(RTSP_client * pRtsp, char *pDescr, char *s8Str)
{
/*/=====================================
	char const* const SdpPrefixFmt =
			"v=0\r\n"	//版本信息
			"o=- %s %s IN IP4 %s\r\n" //<用户名><会话id><版本>//<网络类型><地址类型><地址>
			"c=IN IP4 %s\r\n"		//c=<网络信息><地址信息><连接地址>对ip4为0.0.0.0  here！
			"s=RTSP Session\r\n"		//会话名session id
			"i=N/A\r\n"		//会话信息
			"t=0 0\r\n"		//<开始时间><结束时间>
			"a=recvonly\r\n"
			"m=video %s RTP/AVP 96\r\n\r\n";	//<媒体格式><端口><传送><格式列表,即媒体净荷类型> m=video 5004 RTP/AVP 96
			
	struct ifreq stIfr;
	char pSdpId[128];

	//获取本机地址
	strcpy(stIfr.ifr_name, "eth0");
	if(ioctl(pRtsp->fd, SIOCGIFADDR, &stIfr) < 0)
	{
		//printf("Failed to get host eth0 ip\n");
		strcpy(stIfr.ifr_name, "wlan0");
		if(ioctl(pRtsp->fd, SIOCGIFADDR, &stIfr) < 0)
		{
			printf("Failed to get host eth0 or wlan0 ip\n");
		}
	}

	sock_ntop_host(&stIfr.ifr_addr, sizeof(struct sockaddr), s8Str, 128);

	GetSdpId(pSdpId);

	sprintf(pDescr,  SdpPrefixFmt,  pSdpId,  pSdpId,  s8Str,  inet_ntoa(((struct sockaddr_in *)(&pRtsp->stClientAddr))->sin_addr), "5006", "H264");
			"b=RR:0\r\n"
			 //按spydroid改
			"a=rtpmap:96 %s/90000\r\n"		//a=rtpmap:<净荷类型><编码名>/<时钟速率> 	a=rtpmap:96 H264/90000
			"a=fmtp:96 packetization-mode=1;profile-level-id=1EE042;sprop-parameter-sets=QuAe2gLASRA=,zjCkgA==\r\n"
			"a=control:trackID=0\r\n";
	
#ifdef RTSP_DEBUG
//			printf("SDP:\n%s\n", pDescr);
#endif
*/
	char pSdpId[128];
	char rtp_port[5];

	getlocaladdr(s8Str);

	GetSdpId(pSdpId);
	memset(pSdpId,0,sizeof(pSdpId));
	strcpy(pSdpId,"3603282");
	strcpy(pDescr, "v=0\r\n");	
	strcat(pDescr, "o=-");
	strcat(pDescr, pSdpId);
	strcat(pDescr," ");
	strcat(pDescr, pSdpId);
	strcat(pDescr," IN IP4 ");
	strcat(pDescr, s8Str);

	strcat(pDescr, "\r\n");
	strcat(pDescr, "s=xucheng rtsp server\r\n");
	strcat(pDescr, "i=N/A\r\n");

   	strcat(pDescr, "c=");
   	strcat(pDescr, "IN ");		/* Network type: Internet. */
   	strcat(pDescr, "IP4 ");		/* Address type: IP4. */
	//strcat(pDescr, get_address());
	strcat(pDescr, inet_ntoa(((struct sockaddr_in *)(&pRtsp->stClientAddr))->sin_addr));
	strcat(pDescr, "\r\n");
	
   	strcat(pDescr, "t=0 0\r\n");	
	strcat(pDescr, "a=range:npt=0-\r\n");
	/**** media specific ****/
	strcat(pDescr,"m=");
	strcat(pDescr,"video ");
	sprintf(rtp_port,"%d",0);
	strcat(pDescr, rtp_port);
	strcat(pDescr," RTP/AVP "); /* Use UDP */
	strcat(pDescr,"96\r\n");
	//strcat(pDescr, "\r\n");
	strcat(pDescr,"b=RR:0\r\n");
		/**** Dynamically defined payload ****/
		strcat(pDescr,"a=rtpmap:96");
		strcat(pDescr," ");	
		strcat(pDescr,"H264/90000");
		strcat(pDescr, "\r\n");
		strcat(pDescr,"a=fmtp:96 packetization-mode=1;");
//		strcat(pDescr,"profile-level-id=");
//		strcat(pDescr,psp.base64profileid);
//		strcat(pDescr,";sprop-parameter-sets=");
//		strcat(pDescr,psp.base64sps);
//		strcat(pDescr,",");
//		strcat(pDescr,psp.base64pps);
//		strcat(pDescr,";");
		strcat(pDescr, "\r\n");
		strcat(pDescr,"a=control:video");
		strcat(pDescr, "\r\n");

//printf("\n\n%s,%d===>psp.base64profileid=%s,psp.base64sps=%s,psp.base64pps=%s\n\n",__FUNCTION__,__LINE__,psp.base64profileid,psp.base64sps,psp.base64pps);

//        strcpy(pDescr, "m=audio 0 RTP/AVP 97\r\n"
//           "c=IN IP4 0.0.0.0\r\n"
//           "a=rtpmap:97 PCMU/8000/1\r\n"
//           "a=control:audio\r\n"
//           "a=range:npt=now-\r\n");	


		//strcat(pDescr, "\r\n");
		//printf("0\r\n");
}


int main()
{
	printf("listen for client connecting...\n");
	signal(SIGINT, IntHandl);

    rtsp_server_param_s info;
    info.get_sdp = GetSdpDescr;
    rtspInit(&info);

//    strcpy(s_raw_path,"./resource/");
//    pthread_t h264_output_thread;
//    pthread_create(&h264_output_thread, NULL, thread_live_video, NULL);
//    pthread_detach(h264_output_thread);

//    pthread_t pcm_output_thread;
//    pthread_create(&pcm_output_thread, NULL, thread_live_audio, NULL);
//    pthread_detach(pcm_output_thread);
    
    rtspStart();
    return 0;
}

