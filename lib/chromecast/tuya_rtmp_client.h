#ifndef __TUYA_RTMP_CLIENT_H__
#define __TUYA_RTMP_CLIENT_H__

#include "tuya_ipc_chromecast.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


int tuya_rtmp_create(const char* url, TUYA_STREAM_TRANSMIT_MODE_E mode, void** pphdl);


int tuya_rtmp_destroy(void **pphdl);


int tuya_rtmp_send_stream(void *phdl, char *buf, uint32_t size);

int rtmp_get_disconnect_status(void** pphdl);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
