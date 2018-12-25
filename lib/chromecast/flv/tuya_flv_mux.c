#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "tuya_flv.h"
#include "tuya_flv_mux.h"



typedef struct tuya_flv_mux_handle_ {
	char 	m_bWriteAACSeqHeader;
	char 	m_bWriteAVCSeqHeader;

	u_char*	m_pSPS;
	int 	m_nSPSSize;
	u_char*	m_pPPS;
	int		m_nPPSSize;

	int 	m_nVideoTimeStamp;

	tuya_flv_mux_params_s param;
	tuya_flv_packet_callback_s cbk;
} tuya_flv_mux_handle_s;


static void tuya_flv_mux_u4(unsigned char* _u, unsigned int i)
{
	_u[0] = i >> 24;
	_u[1] = (i >> 16) & 0xff;
	_u[2] = (i >> 8) & 0xff;
	_u[3] = i & 0xff;

	return;
}

static void tuya_flv_mux_u3(unsigned char* _u, unsigned int i)
{
	_u[0] = i >> 16;
	_u[1] = (i >> 8) & 0xff;
	_u[2] = i & 0xff;

	return;
}

static void tuya_flv_mux_u2(unsigned char* _u, unsigned int i)
{
	_u[0] = i >> 8;
	_u[1] = i & 0xff;

	return;
}

static void tuya_flv_mux_setup_tag_header(int type, int datasize, uint32_t nTimeStamp,
											int streamid, TagHeader* pheader, u_char *ptag)
{
	pheader->nType = type;
	pheader->nDataSize = datasize;
	pheader->nTimeStamp = nTimeStamp & 0x0fff;
	pheader->nTSEx = nTimeStamp >> 24;
	pheader->nStreamID = 0;
	pheader->nTotalTS = nTimeStamp;

	ptag[0] = type;

	unsigned char datasize_u3[3] = {0};
	tuya_flv_mux_u3(datasize_u3, datasize);
	memcpy(ptag + 1, datasize_u3, 3);

	unsigned char tt_u3[3] = {0};
	tuya_flv_mux_u3(tt_u3, nTimeStamp);
	memcpy(ptag + 4, tt_u3, 3);

	ptag[7] = nTimeStamp >> 24;

	unsigned char sid_u3[3] = {0};
	tuya_flv_mux_u3(sid_u3, streamid);
	memcpy(ptag + 8, sid_u3, 3);

	return;
}

static int tuya_flv_mux_start(void* phdl)
{
	u_char FlvHeader[FLV_HEADER_SIZE] = {0};

	tuya_flv_mux_handle_s* pmuxhdl = (tuya_flv_mux_handle_s*)phdl;
	if(pmuxhdl == NULL) {
		return -1;
	}

	if(pmuxhdl->param.file != 1) {
		return 0;
	}

	FlvHeader[0] = 'F';
	FlvHeader[1] = 'L';
	FlvHeader[2] = 'V';
	FlvHeader[3] = 1;
	FlvHeader[4] = 0x0;

	if (pmuxhdl->param.video != 0) {
		FlvHeader[4] |= 0x01;
	}

	if (pmuxhdl->param.audio != 0) {
		FlvHeader[4] |= 0x04;
	}

	unsigned char size_u4[4] = {0};
	unsigned int size = FLV_HEADER_SIZE;
	tuya_flv_mux_u4(size_u4, size);

	memcpy(FlvHeader + 5, size_u4, sizeof(unsigned int));
	
	if(pmuxhdl->cbk.flv_packet_data != NULL) {
		pmuxhdl->cbk.flv_packet_data(pmuxhdl->cbk.priv, FlvHeader, FLV_HEADER_SIZE, 0);
	}

	return 0;
}

static void tuya_flv_mux_write_h264_end_seq(tuya_flv_mux_handle_s* pmuxhdl)
{
	TagHeader header;
	int nDataSize = 1 + 1 + 3;
	u_char *data = (u_char*)malloc(nDataSize + 11);

	tuya_flv_mux_setup_tag_header(0x09, nDataSize, pmuxhdl->m_nVideoTimeStamp, 0, &header, data);

	data[11] = 0x27;
	data[12] = 0x02;

	unsigned char com_time_u3[3] = {0};
	tuya_flv_mux_u3(com_time_u3, 0);
	memcpy(data + 13, com_time_u3, 3);

	if(pmuxhdl->cbk.flv_packet_data != NULL) {
		pmuxhdl->cbk.flv_packet_data(pmuxhdl->cbk.priv, data, header.nDataSize + 11, header.nDataSize + 11 + 7);
	}

	free(data);

	return;
}

static int tuya_flv_mux_stop(void* phdl)
{
	tuya_flv_mux_handle_s* pmuxhdl = (tuya_flv_mux_handle_s*)phdl;
	if(pmuxhdl == NULL) {
		return -1;
	}

	if (pmuxhdl->m_pSPS != NULL) {
		free(pmuxhdl->m_pSPS);
		pmuxhdl->m_pSPS = NULL;
	}

	if (pmuxhdl->m_pPPS != NULL) {
		free(pmuxhdl->m_pPPS);
		pmuxhdl->m_pPPS = NULL;
	}

	if (pmuxhdl->m_nVideoTimeStamp != 0) {
		tuya_flv_mux_write_h264_end_seq(pmuxhdl);
	}

	return 0;
}

static int tuya_flv_mux_write_aac_header(tuya_flv_mux_handle_s* pmuxhdl, u_char *pAAC, uint32_t nTimeStamp)
{
	TagHeader header;
	int nDataSize = 1 + 1 + 2;
	u_char *data = (u_char*)malloc(nDataSize + 11);

	tuya_flv_mux_setup_tag_header(0x08, nDataSize, nTimeStamp, 0, &header, data);

	u_char cAudioParam = 0xAF;
	memcpy(data + 11, &cAudioParam, 1);

	u_char cAACPacketType = 0;	/* seq header */
	memcpy(data + 12, &cAACPacketType, 1);

	u_char AudioSpecificConfig[2];
	u_char *p = (u_char *)pAAC;

	char aacProfile = (p[2] >> 6) + 1;
	char sampleRateIndex = (p[2] >> 2) & 0x0f;
	char channelConfig = ((p[2] & 0x01) << 2) + (p[3] >> 6);

	AudioSpecificConfig[0] = (aacProfile << 3) + (sampleRateIndex >> 1);
	AudioSpecificConfig[1] = ((sampleRateIndex & 0x01) << 7) + (channelConfig << 3);

	memcpy(data + 13, (char *)AudioSpecificConfig, 2);

	if(pmuxhdl->cbk.flv_packet_data != NULL) {
		pmuxhdl->cbk.flv_packet_data(pmuxhdl->cbk.priv, data, header.nDataSize + 11, header.nDataSize + 11 + 7);
	}

	pmuxhdl->m_bWriteAACSeqHeader = 1;
	free(data);

	return 0;
}

static void tuya_flv_mux_write_aac_frame(tuya_flv_mux_handle_s* pmuxhdl, u_char *pFrame, int nFrameSize, uint32_t nTimeStamp)
{
	TagHeader header;
	int nDataSize = 1 + 1 + (nFrameSize - 7);
	u_char *data = (u_char*)malloc(nDataSize + 11);

	tuya_flv_mux_setup_tag_header(0x08, nDataSize, nTimeStamp, 0, &header, data);

	u_char cAudioParam = 0xAF;
	memcpy(data + 11, &cAudioParam, 1);

	u_char cAACPacketType = 1;	/* AAC raw data */
	memcpy(data + 12, &cAACPacketType, 1);

	memcpy(data + 13, pFrame + 7, nFrameSize - 7);

	if(pmuxhdl->cbk.flv_packet_data != NULL) {
		pmuxhdl->cbk.flv_packet_data(pmuxhdl->cbk.priv, data, header.nDataSize + 11, header.nDataSize + 11 + 7);
	}

	free(data);

	return;
}

int tuya_flv_mux_convert_aac(void* phdl, u_char *pAAC, int nAACFrameSize, uint32_t nTimeStamp)
{
	tuya_flv_mux_handle_s* pmuxhdl = (tuya_flv_mux_handle_s*)phdl;
	if(pmuxhdl == NULL) {
		return -1;
	}

	if (pAAC == NULL || nAACFrameSize <= 7) {
		return -1;
	}

	if(!pmuxhdl->m_bWriteAACSeqHeader) {
		tuya_flv_mux_write_aac_header(pmuxhdl, pAAC, 0);
	}

	if(pmuxhdl->m_bWriteAACSeqHeader) {
		tuya_flv_mux_write_aac_frame(pmuxhdl, pAAC, nAACFrameSize, nTimeStamp);
	}

	return 0;
}

static void tuya_flv_mux_write_h264_header(tuya_flv_mux_handle_s* pmuxhdl, uint32_t nTimeStamp)
{
	TagHeader header;
	int nDataSize = 1 + 1 + 3 + 6 + 2 + (pmuxhdl->m_nSPSSize - 4) + 1 + 2 + (pmuxhdl->m_nPPSSize - 4);
	u_char *data = (u_char*)malloc(nDataSize + 11);

	tuya_flv_mux_setup_tag_header(0x09, nDataSize, nTimeStamp, 0, &header, data);

	u_char cVideoParam = 0x17;
	memcpy(data + 11, &cVideoParam, 1);

	u_char cAVCPacketType = 0;	/* seq header */
	memcpy(data + 12, &cAVCPacketType, 1);

	unsigned char CompositionTime_u3[3] = {0};
	tuya_flv_mux_u3(CompositionTime_u3, 0);
	memcpy(data + 13, CompositionTime_u3, 3);

	data[16] = 1;
	data[17] = pmuxhdl->m_pSPS[5];
	data[18] = pmuxhdl->m_pSPS[6];
	data[19] = pmuxhdl->m_pSPS[7];
	data[20] = 0xff;
	data[21] = 0xE1;

	unsigned char spssize_u2[2] = {0};
	tuya_flv_mux_u2(spssize_u2, pmuxhdl->m_nSPSSize - 4);
	memcpy(data + 22, spssize_u2, 2);

	memcpy(data + 24, (pmuxhdl->m_pSPS + 4), pmuxhdl->m_nSPSSize - 4);

	data[24 + pmuxhdl->m_nSPSSize - 4] = 0x01;

	unsigned char ppssize_u2[2] = {0};
	tuya_flv_mux_u2(ppssize_u2, pmuxhdl->m_nPPSSize - 4);
	memcpy(data + 24 + pmuxhdl->m_nSPSSize - 4 + 1, ppssize_u2, 2);
	memcpy(data + 24 + pmuxhdl->m_nSPSSize - 4 + 1 + 2, (pmuxhdl->m_pPPS + 4), pmuxhdl->m_nPPSSize - 4);

	if(pmuxhdl->cbk.flv_packet_data != NULL) {
		pmuxhdl->cbk.flv_packet_data(pmuxhdl->cbk.priv, data, header.nDataSize + 11, header.nDataSize + 11 + 7);
	}

	free(data);

	return;
}

static void tuya_flv_mux_write_h264_frame(tuya_flv_mux_handle_s* pmuxhdl, u_char *pNalu, int nNaluSize,
											uint32_t nTimeStamp, uint32_t CompositionTime)
{
	TagHeader header;
	int nNaluType = pNalu[4] & 0x1f;
	int nDataSize = 1 + 1 + 3 + 4 + (nNaluSize - 4);
	u_char *data = (u_char*)malloc(nDataSize + 11);

	tuya_flv_mux_setup_tag_header(0x09, nDataSize, nTimeStamp, 0, &header, data);

	if (nNaluType == 5) {
		data[11] = 0x17;
	} else {
		data[11] = 0x27;
	}

	data[12] = 1;

	unsigned char com_time_u3[3] = {0};
	tuya_flv_mux_u3(com_time_u3, CompositionTime);
	memcpy(data + 13, com_time_u3, 3);

	unsigned char nalusize_u4[4] = {0};
	tuya_flv_mux_u4(nalusize_u4, nNaluSize - 4);
	memcpy(data + 16, nalusize_u4, 4);

	memcpy(data + 20, pNalu + 4, nNaluSize - 4);

	if(pmuxhdl->cbk.flv_packet_data != NULL) {
		pmuxhdl->cbk.flv_packet_data(pmuxhdl->cbk.priv, data, header.nDataSize + 11, header.nDataSize + 11 + 7);
	}

	free(data);

	return;
}

int tuya_flv_mux_convert_h264(void* phdl, u_char *pNalu, int nNaluSize, uint32_t nTimeStamp, uint32_t CompositionTime)
{
	tuya_flv_mux_handle_s* pmuxhdl = (tuya_flv_mux_handle_s*)phdl;
	if(pmuxhdl == NULL) {
		return -1;
	}

	pmuxhdl->m_nVideoTimeStamp = nTimeStamp;

	if (pNalu == NULL || nNaluSize <= 4) {
		return -1;
	}

	int nNaluType = pNalu[4] & 0x1f;

	if(nNaluType == 0x06) {
		return -1;
	}

	if (pmuxhdl->m_pSPS == NULL && nNaluType == 0x07) {
		pmuxhdl->m_pSPS = (u_char*)malloc(nNaluSize);
		pmuxhdl->m_nSPSSize = nNaluSize;
		memcpy(pmuxhdl->m_pSPS, pNalu, nNaluSize);
	}

	if (pmuxhdl->m_pPPS == NULL && nNaluType == 0x08) {
		pmuxhdl->m_pPPS = (u_char*)malloc(nNaluSize);
		pmuxhdl->m_nPPSSize = nNaluSize;
		memcpy(pmuxhdl->m_pPPS, pNalu, nNaluSize);
	}

	if (pmuxhdl->m_pSPS != NULL && pmuxhdl->m_pPPS != NULL && !pmuxhdl->m_bWriteAVCSeqHeader) {
		tuya_flv_mux_write_h264_header(pmuxhdl, 0);
		pmuxhdl->m_bWriteAVCSeqHeader = 1;
		return 0;
	}

	if (!pmuxhdl->m_bWriteAVCSeqHeader) {
		return -1;
	}

	if(nNaluType == 0x07 || nNaluType == 0x08) {
		// printf("nalu type: %d\n", nNaluType);
		return -1;
	}

	tuya_flv_mux_write_h264_frame(pmuxhdl, pNalu, nNaluSize, nTimeStamp, CompositionTime);

	return 0;
}

int tuya_flv_mux_create(void** pphdl, tuya_flv_mux_params_s* pparam, tuya_flv_packet_callback_s* pcbk)
{
	tuya_flv_mux_handle_s* pmuxhdl = NULL;

	pmuxhdl = (tuya_flv_mux_handle_s*)malloc(sizeof(tuya_flv_mux_handle_s));
	if(pmuxhdl == NULL) {
		return -1;
	}

	memset(pmuxhdl, 0, sizeof(tuya_flv_mux_handle_s));

	if(pparam != NULL) {
		memcpy(&pmuxhdl->param, pparam, sizeof(tuya_flv_mux_params_s));
	}

	if(pcbk != NULL) {
		memcpy(&pmuxhdl->cbk, pcbk, sizeof(tuya_flv_packet_callback_s));
	}

	tuya_flv_mux_start(pmuxhdl);

	*pphdl = pmuxhdl;

	return 0;
}

int tuya_flv_mux_destroy(void** pphdl)
{
	tuya_flv_mux_handle_s* pmuxhdl = (tuya_flv_mux_handle_s*)*pphdl;
	if(pmuxhdl == NULL) {
		return -1;
	}

	tuya_flv_mux_stop(pmuxhdl);

	free(pmuxhdl);
	pmuxhdl = NULL;

	return 0;
}

