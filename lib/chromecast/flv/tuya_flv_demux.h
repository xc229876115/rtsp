#ifndef __FLVDEMUX_H__
#define __FLVDEMUX_H__

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef struct tuya_flv_av_callback_ {
	void* priv;

	void (*sps_pps)(const void *context, const char *sps, const int sps_size, 
					const char *pps, const int pps_size);

	void (*h264_nalu)(const void *context, const char *nalu, const int size,
						const uint32_t timestamp, const uint32_t compositiontime);

	void (*audio_specific)(const void *context, const int profile,
							const int sampleRateIndex, const int channel);

	void (*aac_packet)(const void *context, const char *aac,
						const int size, const uint32_t timestamp);
} tuya_flv_av_callback_s;


int tuya_flv_demux_create(void** pphdl, tuya_flv_av_callback_s* pcbk);


void tuya_flv_demux_destroy(void** pphdl);


int tuya_flv_demux_parser(void** phdl, u_char *pBuf, int nBufSize, int* nUsedLen);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
