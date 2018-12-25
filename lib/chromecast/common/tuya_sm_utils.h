#ifndef __TUYA_FLV_COMMON_H__
#define __TUYA_FLV_COMMON_H__

#include <string.h>
#include <stdio.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


uint64_t tuya_flv_time_utc_sec();


uint64_t tuya_time_utc_ms();


unsigned char* get_one_nalu(unsigned char *pBufIn, int nInSize, int* nNaluSize);


void tuya_log_print(const char* params, const char *fmt, ...);


#define LOG_BUFFER_LEN	256
#define __FILENAME__ (strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/') + 1):__FILE__)

#define TUYA_LOG(fmt, args...)	\
	do {	\
		char buf[LOG_BUFFER_LEN] = {0};	\
		snprintf(buf, LOG_BUFFER_LEN, "%s %s(%d)", \
					__FILENAME__, __FUNCTION__, __LINE__);	\
		tuya_log_print(buf, fmt, ##args);	\
	} while(0)


//#define DEBUG(fmt, args...)		TUYA_LOG(fmt, ##args)


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif