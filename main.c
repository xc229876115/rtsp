
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

        int frameRate = AUDIO_FPS;
        int sleepTick = 1000000/frameRate;
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
        int sleepTick = 1000000/frameRate;
        usleep(sleepTick);
    }

    pthread_exit(0);
}

/**************************************************************************************************
**
**
**
**************************************************************************************************/
int main(void)
{
	int s32MainFd;

	//申请缓冲区
	ringmalloc(720*576);
	printf("RTSP server START\n");

	//rtsp server 初始化
	PrefsInit();
	
	printf("listen for client connecting...\n");
	signal(SIGINT, IntHandl);

	//创建TCP 监听套接字
	s32MainFd = tcp_listen(SERVER_RTSP_PORT_DEFAULT);

	/*创建后台线程schedule_do 用于处理客户数据 */
	if (ScheduleInit() == ERR_FATAL)
	{
		fprintf(stderr,"Fatal: Can't start scheduler %s, %i \nServer is aborting.\n", __FILE__, __LINE__);
		return 0;
	}

	//rtp 端口初始化
	RTP_port_pool_init(RTP_DEFAULT_PORT);

	strcpy(s_raw_path,"./resource/");
    pthread_t h264_output_thread;
    pthread_create(&h264_output_thread, NULL, thread_live_video, NULL);
    pthread_detach(h264_output_thread);

    pthread_t pcm_output_thread;
    pthread_create(&pcm_output_thread, NULL, thread_live_audio, NULL);
    pthread_detach(pcm_output_thread);


	printf("start EventLoop\n");
	//用于管理客户端接入
	EventLoop(s32MainFd);	//等待事件

	sleep(1);
	ringfree();
	printf("The Server quit!\n");

	return 0;
}

