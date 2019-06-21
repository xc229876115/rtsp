#ifndef _RTSP_H
#define _RTSP_H
#include "rtsputils.h"

#define RTSP_DEBUG 1
#define RTP_DEFAULT_PORT 5004

typedef enum {
    AV_VIDEO = 0,
    AV_AUDIO,
}AV_TYPE;

typedef struct rtsp_server_param_ {
	unsigned short 			port;

	int (*start)(void* thiz);//开始播放

	int (*stop)(void* thiz);//结束播放

	int (*get_sdp)(void* thiz, char* buf, int* size);//获取sdp信息

	int (*get_next_frame)(void* thiz, AV_TYPE type, char *buf , int *size);//获取媒体流

} rtsp_server_param_s;
int rtspInit(rtsp_server_param_s *param);
int rtspStart();

void PrefsInit();
void RTP_port_pool_init(int port);
int EventLoop(int s32MainFd);

void CallBackNotifyRtspExit(char s8IsExit);
void *ThreadRtsp(void *pArgs);
int rtsp_server(RTSP_client *rtsp);
void IntHandl(int i);
void UpdateSps(unsigned char *data,int len);
void UpdatePps(unsigned char *data,int len);

int PutPCMDataToBuffer(char *data , int size );

#endif /* _RTSP_H */
