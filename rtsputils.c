#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <sys/time.h>
#include <ifaddrs.h>  
#include <fcntl.h>
#include <errno.h>

#include "rtspservice.h"
#include "rtsputils.h"
#include "rtputils.h"
#include "ringfifo.h"
extern int g_s32DoPlay;
//===============add===================

//=====================================
int getlocaladdr(char *addr)
{
	struct sockaddr_in *sin = NULL;
	struct ifaddrs *ifa = NULL, *ifList;

	if (getifaddrs(&ifList) < 0)
	{
		return -1;
	}

	for (ifa = ifList; ifa != NULL; ifa = ifa->ifa_next)
	{
		if(ifa->ifa_addr->sa_family == AF_INET)
		{
//			printf("\n>>> interfaceName: %s\n", ifa->ifa_name);

			sin = (struct sockaddr_in *)ifa->ifa_addr;
//			printf(">>> ipAddress: %s\n", inet_ntoa(sin->sin_addr));
			if(strcmp(ifa->ifa_name,"lo") != 0)
				strcpy(addr,inet_ntoa(sin->sin_addr));

			sin = (struct sockaddr_in *)ifa->ifa_dstaddr;
//			printf(">>> broadcast: %s\n", inet_ntoa(sin->sin_addr));

			sin = (struct sockaddr_in *)ifa->ifa_netmask;
//			printf(">>> subnetMask: %s\n", inet_ntoa(sin->sin_addr));

		}
	}

	freeifaddrs(ifList);

	return 0;
}

char *sock_ntop_host(const struct sockaddr *sa, socklen_t salen, char *str, size_t len)
{
    switch(sa->sa_family)
    {
        case AF_INET:
        {
            struct sockaddr_in  *sin = (struct sockaddr_in *) sa;

            if(inet_ntop(AF_INET, &sin->sin_addr, str, len) == NULL)
                return(NULL);
            return(str);
        }

        default:
            snprintf(str, len, "sock_ntop_host: unknown AF_xxx: %d, len %d",
                     sa->sa_family, salen);
            return(str);
    }
    return (NULL);
}

int tcp_accept(int fd , struct sockaddr *addr)
{
    int f;
    socklen_t addrlen = sizeof(struct sockaddr);

    memset(addr,0,addrlen);

    /*接收连接，创建一个新的socket,返回其描述符*/
    f = accept(fd, addr, &addrlen);
	if(f){
	    char addr_str[128];
	    memset(addr_str,0,sizeof(addr_str));
	    sock_ntop_host(addr,addrlen,addr_str,sizeof(addr_str));
	    printf("New Client %s fd = %d is connected !\n",addr_str,f);
	}

    return f;
}

void tcp_close(int s)
{
	if(s)
	    close(s);
}

int tcp_connect(unsigned short port, char *addr)
{
    int f;
    int on=1;
    int one = 1;/*used to set SO_KEEPALIVE*/

    struct sockaddr_in s;
    int v = 1;
    if((f = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))<0)
    {
        fprintf(stderr, "socket() error in tcp_connect.\n");
        return -1;
    }
    setsockopt(f, SOL_SOCKET, SO_REUSEADDR, (char *) &v, sizeof(int));
    s.sin_family = AF_INET;
    s.sin_addr.s_addr = inet_addr(addr);//htonl(addr);
    s.sin_port = htons(port);
    // set to non-blocking
    if(ioctl(f, FIONBIO, &on) < 0)
    {
        fprintf(stderr,"ioctl() error in tcp_connect.\n");
        return -1;
    }
    if(connect(f,(struct sockaddr*)&s, sizeof(s)) < 0)
    {
        fprintf(stderr,"connect() error in tcp_connect.\n");
        return -1;
    }
    if(setsockopt(f, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one))<0)
    {
        fprintf(stderr,"setsockopt() SO_KEEPALIVE error in tcp_connect.\n");
        return -1;
    }
    return f;
}

//监听套接字
int tcp_listen(unsigned short port)
{
    int f;
    int on=1;

    struct sockaddr_in s;
    int v = 1;

    /*创建套接字*/
    if((f = socket(AF_INET, SOCK_STREAM, 0))<0)
    {
        fprintf(stderr, "socket() error in tcp_listen.\n");
        return -1;
    }

    /*设置socket的可选参数*/
    setsockopt(f, SOL_SOCKET, SO_REUSEADDR, (char *) &v, sizeof(int));

    s.sin_family = AF_INET;
    s.sin_addr.s_addr = htonl(INADDR_ANY);
    s.sin_port = htons(port);

    /*绑定socket*/
    if(bind(f, (struct sockaddr *)&s, sizeof(s)))
    {
        fprintf(stderr, "bind() error in tcp_listen");
        return -1;
    }

    //设置为非阻塞方式
    if(ioctl(f, FIONBIO, &on) < 0)
    {
        fprintf(stderr, "ioctl() error in tcp_listen.\n");
        return -1;
    }

    /*监听*/
    if(listen(f, MAX_CONNECTION) < 0)
    {
        fprintf(stderr, "listen() error in tcp_listen.\n");
        return -1;
    }

    return f;
}

int tcp_read(int fd, void *buffer, int nbytes )
{
    int n;
    n=recv(fd, buffer, nbytes, 0);

    return n;
}

/*
int tcp_write(int fd, void *buffer, int nbytes)
{
    int n;
    n = write(fd, buffer, nbytes);

    return n;
}
*/

int tcp_write(int connectSocketId, char *dataBuf, int dataSize)
{
    int     actDataSize;

    //发送数据
    while(dataSize > 0)
    {
        actDataSize = send(connectSocketId, dataBuf, dataSize, 0);

        if(actDataSize<=0)
            break;

        dataBuf  += actDataSize;
        dataSize -= actDataSize;
    }

    if(dataSize > 0)
    {
        printf("Send Data error\n");
        return -1;
    }

    return 0;
}

/*      schedule 相关     */
static stScheList gSched[MAX_CONNECTION];
static pthread_mutex_t sc_lock ;

int stop_schedule = 0;//是否退出schedule
int num_conn = 2;    /*连接个数*/



/*负责buffer 数据流发送*/
static void *schedule_do(void *arg)
{
    int i=0;
    struct timeval now;
    uint64_t mnow;
    struct timespec ts = {0,33333}; //30fps
    int s32FindNal = 0;
    int ringbuflen=0;
    struct ringbuf ringinfo;
    
    prctl(PR_SET_NAME, "schedule_do");
//=====================
#ifdef RTSP_DEBUG
    printf("The pthread %s start\n", __FUNCTION__);
#endif

    do
    {
        nanosleep(&ts, NULL);
//      trace_point();

        s32FindNal = 0;

        //如果有客户端连接，则g_s32DoPlay大于零
        if(g_s32DoPlay>0)
        {
            ringbuflen = ringget(&ringinfo);
            if(ringbuflen ==0)
                continue ;
        }
        s32FindNal = 1;
        for(i=0; i<MAX_CONNECTION; ++i)
        {
        
    		pthread_mutex_lock(&sc_lock);
            stScheList *sched = &gSched[i];
            pthread_mutex_unlock(&sc_lock);
            if(sched->valid)
            {
                if(!sched->rtp_session->pause)
                {
                    //计算时间戳
                    gettimeofday(&now,NULL);
                    mnow = (now.tv_sec*1000 + now.tv_usec/1000);//毫秒
#ifdef DEBUG
//	                printf("hndRtp is  %p , s32FindNal= %d \n",sched[i].rtp_session->hndRtp,s32FindNal);
#endif
                    if((sched->rtp_session->hndRtp)&&(s32FindNal))
                    {
                        if(sched->rtp_session->hndRtp->emPayload & ringinfo.frame_type)
		                {
#ifdef DEBUG
						if(ringinfo.frame_type ==FRAME_TYPE_I)
						printf("send frame type %d, frame,length:%d,pointer:%p,timestamp:%lu\n",ringinfo.frame_type,ringinfo.size,ringinfo.buffer,mnow);
#endif
                        if(ringinfo.frame_type ==FRAME_TYPE_I)
                            sched[i].BeginFrame=1;
                        //if(sched[i].BeginFrame== 1)
                            int ret = sched->play_action((sched[i].rtp_session), (char *)ringinfo.buffer, ringinfo.size, mnow);
							if(ret < 0){
								printf("play action is %d\n",ret);
							}
							}
                    }
                }
            }

        }

    }
    while(!stop_schedule);


    //free(pDataBuf);
    //close(s32FileId);

#ifdef RTSP_DEBUG
    printf("The pthread %s end\n", __FUNCTION__);
#endif
    return ERR_NOERROR;
}

//把RTP会话添加进schedule中，错误返回-1,正常返回schedule队列号
int schedule_add(RTP_session *rtp_session)
{
    int i;
    int ret = ERR_GENERIC;
    return ret;
    printf("schedule add rtp session is %p\n",rtp_session);
    pthread_mutex_lock(&sc_lock);
    for(i=0; i<MAX_CONNECTION; ++i)
    {
        /*需是还没有被加入到调度队列中的会话*/
        if(!gSched[i].valid)
        {
            gSched[i].valid=1;
            gSched[i].rtp_session=rtp_session;

            //设置播放动作
            gSched[i].play_action=RtpSend;
            printf("**adding a schedule object action %s,%d**\n", __FILE__, __LINE__);
            printf("schedule add rtp session is %p\n",gSched[i].rtp_session);
            ret = i;
            break;
        }
    }
    
    pthread_mutex_unlock(&sc_lock);
    return ;
}

int ScheduleInit()
{
    int i;
    pthread_t thread=0;
    
	//初始化锁
	pthread_mutex_init(&sc_lock,NULL);
	
    /*初始化数据*/
    for(i=0; i<MAX_CONNECTION; ++i)
    {
        gSched[i].rtp_session=NULL;
        gSched[i].play_action=NULL;
        gSched[i].valid=0;
        gSched[i].BeginFrame=0;
    }

    /*创建处理主线程*/
    int ret = pthread_create(&thread,NULL,schedule_do,NULL);
    if(ret < 0){
        printf("pthread create err : %s\n",strerror(errno));
        return -1;
    }

    return 0;
}

int schedule_start(int id,stPlayArgs *args)
{
    printf("id is %d\n",id);
    if(id < 0 || id >= MAX_CONNECTION)
        return -1;
    /*    struct timeval now;
        double mnow;
        gettimeofday(&now,NULL);
        mnow=(double)now.tv_sec*1000+(double)now.tv_usec/1000;
    */
    gSched[id].rtp_session->pause=0;
    gSched[id].rtp_session->started=1;

    //播放状态,大于零则表示有客户端播放文件
    g_s32DoPlay++;

    return ERR_NOERROR;
}

void schedule_stop(int id)
{
//    RTCP_send_packet(sched[id].rtp_session,SR);
//    RTCP_send_packet(sched[id].rtp_session,BYE);
}

int schedule_remove(int id)
{
    gSched[id].valid=0;
    gSched[id].BeginFrame=0;
    return ERR_NOERROR;
}


//把需要发送的信息放入rtsp.out_buffer中
int bwrite(char *buffer, unsigned short len, RTSP_client * rtsp)
{
    
    /*检查是否有缓冲溢出*/
    if((rtsp->out_size + len) > sizeof(rtsp->out_buffer))
    {
        fprintf(stderr,"bwrite(): not enough free space in out message buffer.\n");
        return ERR_ALLOC;
    }
    
    /*填充数据*/
    memcpy(&(rtsp->out_buffer[rtsp->out_size]), buffer, len);
    rtsp->out_buffer[rtsp->out_size + len] = '\0';
    rtsp->out_size += len;

#ifdef RTSP_DEBUG
    printf("<<<<<<<<<<<<<<<<SEND(%d)\n%s\n\n",len,rtsp->out_buffer);
#endif
    return ERR_NOERROR;
}

/*
返回响应
err: 错误码
addon : 附件字符串

RTSP版本 状态码 解释 CR LF 
消息头 CR LF
CR LF 
消息体 CR LF 

*/
int send_reply(int err, char *addon, RTSP_client * rtsp)
{
    unsigned int len;
    char *buf;
    int res;

    if(addon != NULL)
    {
        len = 256 + strlen(addon);
    }
    else
    {
        len = 256;
    }

    /*分配空间*/
    buf = (char *) malloc(len);
    if(buf == NULL)
    {
        fprintf(stderr,"send_reply(): memory allocation error.\n");
        return ERR_ALLOC;
    }
    memset(buf, 0, len);
    /*按照协议格式填充数据*/
    snprintf(buf,len, "%s %d %s"RTSP_EL"CSeq: %d"RTSP_EL, RTSP_VER, err, get_stat(err), rtsp->rtsp_cseq);
    strcat(buf, RTSP_EL);

    /*将数据写入到缓冲区中*/
    res = bwrite(buf, (unsigned short) strlen(buf), rtsp);
    //释放空间
    free(buf);

    return res;
}


//由错误码返回错误信息
const char *get_stat(int err)
{
    struct
    {
        const char *token;
        int code;
    } status[] =
    {
        {
            "Continue", 100
        }, {
            "OK", 200
        }, {
            "Created", 201
        }, {
            "Accepted", 202
        }, {
            "Non-Authoritative Information", 203
        }, {
            "No Content", 204
        }, {
            "Reset Content", 205
        }, {
            "Partial Content", 206
        }, {
            "Multiple Choices", 300
        }, {
            "Moved Permanently", 301
        }, {
            "Moved Temporarily", 302
        }, {
            "Bad Request", 400
        }, {
            "Unauthorized", 401
        }, {
            "Payment Required", 402
        }, {
            "Forbidden", 403
        }, {
            "Not Found", 404
        }, {
            "Method Not Allowed", 405
        }, {
            "Not Acceptable", 406
        }, {
            "Proxy Authentication Required", 407
        }, {
            "Request Time-out", 408
        }, {
            "Conflict", 409
        }, {
            "Gone", 410
        }, {
            "Length Required", 411
        }, {
            "Precondition Failed", 412
        }, {
            "Request Entity Too Large", 413
        }, {
            "Request-URI Too Large", 414
        }, {
            "Unsupported Media Type", 415
        }, {
            "Bad Extension", 420
        }, {
            "Invalid Parameter", 450
        }, {
            "Parameter Not Understood", 451
        }, {
            "Conference Not Found", 452
        }, {
            "Not Enough Bandwidth", 453
        }, {
            "Session Not Found", 454
        }, {
            "Method Not Valid In This State", 455
        }, {
            "Header Field Not Valid for Resource", 456
        }, {
            "Invalid Range", 457
        }, {
            "Parameter Is Read-Only", 458
        }, {
            "Unsupported transport", 461
        }, {
            "Internal Server Error", 500
        }, {
            "Not Implemented", 501
        }, {
            "Bad Gateway", 502
        }, {
            "Service Unavailable", 503
        }, {
            "Gateway Time-out", 504
        }, {
            "RTSP Version Not Supported", 505
        }, {
            "Option not supported", 551
        }, {
            "Extended Error:", 911
        }, {
            NULL, -1
        }
    };

    int i;
    for(i = 0; status[i].code != err && status[i].code != -1; ++i);

    return status[i].token;
}
