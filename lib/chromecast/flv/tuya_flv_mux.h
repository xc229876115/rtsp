#ifndef __FLVMUX_H__
#define __FLVMUX_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef struct tuya_flv_packet_callback_ {
	void* priv;

	void (*flv_packet_data)(const void *context, const u_char *data,
							const int size, const int pretagsize);
} tuya_flv_packet_callback_s;


typedef struct tuya_flv_mux_params_ {
	char audio;
	char video;
	char file;
} tuya_flv_mux_params_s;


int tuya_flv_mux_create(void** pphdl, tuya_flv_mux_params_s* pparam,
						tuya_flv_packet_callback_s* pcbk);


int tuya_flv_mux_destroy(void** pphdl);


int tuya_flv_mux_convert_aac(void* phdl, u_char *pAAC, int nAACFrameSize, uint32_t nTimeStamp);


int tuya_flv_mux_convert_h264(void* phdl, u_char *pNalu, int nNaluSize,
								uint32_t nTimeStamp, uint32_t CompositionTime);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif