#include <sys/time.h>
#include <sys/prctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

#include "rtmp.h"
#include "common/list.h"
#include "common/mempool.h"
#include "common/tuya_sm_utils.h"
#include "tuya_rtmp_client.h"



#define RTMP_BUFFER_TOTAL_SIZE (2 * 1024 * 1024)
#define RTMP_BUFFER_BLOCK_SIZE	1024

typedef struct data_ {
	void* ptr;
	int   size;
} rtmp_data_s;


typedef struct rtmp_stream_ {
	char 						url[64];
	RTMP*  						prtmp;
	TUYA_STREAM_TRANSMIT_MODE_E mode;

	char 						start;
	char                        exist;
	pthread_t 					tid;
	void* 						phandle;
	list_t*						plist;
	pthread_mutex_t 			lock;
    volatile char                        disconnect;
} rtmp_stream_s;

int rtmp_get_disconnect_status(void** pphdl)
{
    int rtmp_disconnect = 0;
    rtmp_stream_s* prhdl = (rtmp_stream_s*)*pphdl;
    rtmp_disconnect = prhdl->disconnect;
    //   rtmp_disconnect = (!RTMP_IsConnected(prhdl->prtmp));
    return rtmp_disconnect;
}

static int tuya_rtmp_once_connect(const char* url, void** pphdl)
{
	int ret = -1, connect = -1;
	RTMP *rtmp = NULL;
	rtmp_stream_s* prhdl = (rtmp_stream_s*)malloc(sizeof(rtmp_stream_s));
	if(prhdl == NULL) {
		return -1;
	}

	TUYA_LOG("begin___\n");

    memset(prhdl, 0, sizeof(rtmp_stream_s));

	do {
		rtmp = RTMP_Alloc();
		if(rtmp == NULL) {
			break;
		}

		RTMP_Init(rtmp);

		rtmp->Link.timeout = 5;

		if(!RTMP_SetupURL(rtmp, (char *)url)) {
			RTMP_Free(rtmp);
			free(prhdl);
			return -1;
		}

		RTMP_EnableWrite(rtmp);

		if(!RTMP_Connect(rtmp, NULL)) {
			RTMP_Free(rtmp);
			free(prhdl);
			return -1;
		}

		connect = 1;

		if(!RTMP_ConnectStream(rtmp, 0)) {
			break;
		}

		RTMP_SetBufferMS(rtmp, 3600 * 1000); // 1hour
		prhdl->prtmp = rtmp;
		ret = 0;
	} while(0);

	if(ret == 0) {
		strncpy(prhdl->url, url, sizeof(prhdl->url));
		*pphdl = prhdl;
	} else {
		if(connect) {
			RTMP_Close(rtmp);
		}
		
		if(rtmp != NULL) {
			RTMP_Free(rtmp);
		}
		
		if(prhdl != NULL) {
			free(prhdl);
		}
	}

	TUYA_LOG("end___\n");

	return ret;
}

void tuya_rtmp_free(void* pcontext, void *val)
{
	if(pcontext == NULL || val == NULL) {
		return;
	}

	rtmp_stream_s* prhdl = (rtmp_stream_s*)pcontext;
	rtmp_data_s* value = (rtmp_data_s*)val;

	mempool_free(prhdl->phandle, value->ptr);
	free(value);
	value = NULL;

	return;
}

void *tuya_rtmp_proc(void *arg)
{
    rtmp_stream_s* prhdl = (rtmp_stream_s*)arg;

    TUYA_LOG("begin___\n");

    prctl(PR_SET_NAME, "rtmp");

    prhdl->start = 1;
    prhdl->exist = 1;

	while(prhdl->start) {
		pthread_mutex_lock(&prhdl->lock);
		rtmp_data_s* value = (rtmp_data_s*)list_lpop(prhdl->plist);
		pthread_mutex_unlock(&prhdl->lock);
		if(value) {
			if(!RTMP_IsConnected(prhdl->prtmp)) {
				TUYA_LOG("rtmp not connect!\n");
				prhdl->disconnect = 1;
				break;
			}

			if(!RTMP_Write(prhdl->prtmp, value->ptr, value->size)) {
				TUYA_LOG("rtmp write data error!\n");
				continue;
			}

			pthread_mutex_lock(&prhdl->lock);
			list_remove(prhdl->plist, value);
			pthread_mutex_unlock(&prhdl->lock);
		} else {
			// TUYA_LOG("can not get data from list\n");
			usleep(20 * 1000);
		}
	}

	prhdl->exist = 0;

	TUYA_LOG("end___\n");

	return NULL;
}

static int tuya_rtmp_init_dispatch(rtmp_stream_s* prhdl)
{
	pthread_attr_t attr;
	int size = 2*1024*1024;

	TUYA_LOG("begin___\n");

	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, size);

	mempool_param_s param;
	prhdl->plist = list_create(prhdl, tuya_rtmp_free);

	param.size = RTMP_BUFFER_TOTAL_SIZE;
	param.block_size = RTMP_BUFFER_BLOCK_SIZE;

	prhdl->phandle = mempool_create(&param);

	pthread_mutex_init(&prhdl->lock, NULL);

	pthread_create(&prhdl->tid, &attr, tuya_rtmp_proc, prhdl);
	pthread_detach(prhdl->tid);
	pthread_attr_destroy(&attr);

	TUYA_LOG("end___\n");

	return 0;
}

int tuya_rtmp_create(const char* url, TUYA_STREAM_TRANSMIT_MODE_E mode, void** pphdl)
{
	int ret = -1;
	int count = 5;

	TUYA_LOG("begin___\n");

	do {
		ret = tuya_rtmp_once_connect(url, pphdl);
		if(ret < 0) {
			usleep(500 * 1000);
		}
	} while(ret == -1 && count-- > 0);

	if(ret == 0) {
		rtmp_stream_s* prhdl = (rtmp_stream_s*)*pphdl;

		prhdl->mode = mode;

		if(mode == TUYA_STREAM_TRANSMIT_MODE_ASYNC) {
			tuya_rtmp_init_dispatch(prhdl);
		}
	}

	TUYA_LOG("end___, ret: %d, mode: %s\n", ret, mode == 0 ? "async":"sync");

	return ret;
}

static int tuya_rtmp_uninit_dispatch(rtmp_stream_s* prhdl)
{
	TUYA_LOG("begin___\n");

	prhdl->start = 0;
	// pthread_join(prhdl->tid, NULL);

	int count = 20;

	do {
		usleep(500 * 1000);
	} while(prhdl->exist == 1 && count-- > 0);

	list_destroy(&prhdl->plist);
	mempool_destroy(prhdl->phandle);
	pthread_mutex_destroy(&prhdl->lock);

	TUYA_LOG("end___, exist: %d, count: %d\n", prhdl->exist, count);

	return 0;
}

int tuya_rtmp_destroy(void **pphdl)
{
	rtmp_stream_s* prhdl = (rtmp_stream_s*)*pphdl;
	RTMP *rtmp = prhdl->prtmp;

	TUYA_LOG("begin___\n");

	if(prhdl->mode == TUYA_STREAM_TRANSMIT_MODE_ASYNC) {
		tuya_rtmp_uninit_dispatch(prhdl);
	}

	if (rtmp != NULL) {
		RTMP_Close(rtmp);
		RTMP_Free(rtmp);
	}

	free(prhdl);
	prhdl = NULL;

	TUYA_LOG("end___\n");

	return 0;
}

int tuya_rtmp_send_stream(void *phdl, char *buf, uint32_t size)
{
	rtmp_stream_s* prtmphdl = (rtmp_stream_s*)phdl;

	if(prtmphdl == NULL) {
		return -1;
	}

	if(prtmphdl->mode == TUYA_STREAM_TRANSMIT_MODE_ASYNC) {
		// if(prtmphdl->start == 0) {
		// 	return -1;
		// }

		rtmp_data_s* pdata = (rtmp_data_s*)malloc(sizeof(rtmp_data_s));
		if(pdata == NULL) {
			return -1;
		}

		pthread_mutex_lock(&prtmphdl->lock);

		pdata->size = size;
		pdata->ptr = mempool_malloc(prtmphdl->phandle, pdata->size);

		if(pdata->ptr == NULL) {
			free(pdata);
			pdata = NULL;
			pthread_mutex_unlock(&prtmphdl->lock);
			return -1;
		}

		memcpy(pdata->ptr, buf, size);

		list_rpush(prtmphdl->plist, pdata);

		pthread_mutex_unlock(&prtmphdl->lock);
	} else if (prtmphdl->mode == TUYA_STREAM_TRANSMIT_MODE_SYNC) {
		do {
			if(!RTMP_IsConnected(prtmphdl->prtmp)) {
				TUYA_LOG("rtmp not connect!\n");
                prtmphdl->disconnect = 1;
				break;
			}

			if(!RTMP_Write(prtmphdl->prtmp, buf, size)) {
				TUYA_LOG("rtmp write data error!\n");
				break;
			}
		} while(0);
	}

	return 0;
}

