#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "tuya_flv.h"
#include "tuya_flv_demux.h"



#define FLV_INFO_DEBUG 1
#define TAG_HEADER_SIZE	11
#define CheckBuffer(x) { if ((nBufSize-nOffset)<(x)) { nUsedLen = nOffset; return 0;} }


#ifdef FLV_INFO_DEBUG

const char *frame_types[] = {
    "not defined by standard",
    "keyframe (for AVC, a seekable frame)",
    "inter frame (for AVC, a non-seekable frame)",
    "disposable inter frame (H.263 only)",
    "generated keyframe (reserved for server use only)",
    "video info/command frame"
};

const char *codec_ids[] = {
    "not defined by standard",
    "JPEG (currently unused)",
    "Sorenson H.263",
    "Screen video",
    "On2 VP6",
    "On2 VP6 with alpha channel",
    "Screen video version 2",
    "AVC"
};

const char *avc_packet_types[] = {
    "AVC sequence header",
    "AVC NALU",
    "AVC end of sequence (lower level NALU sequence ender is not required or supported)"
};

const char *sound_formats[] = {
    "Linear PCM, platform endian",
    "ADPCM",
    "MP3",
    "Linear PCM, little endian",
    "Nellymoser 16-kHz mono",
    "Nellymoser 8-kHz mono",
    "Nellymoser",
    "G.711 A-law logarithmic PCM",
    "G.711 mu-law logarithmic PCM",
    "not defined by standard",
    "AAC",
    "Speex",
    "not defined by standard",
    "not defined by standard",
    "MP3 8-Khz",
    "Device-specific sound"
};

const char *sound_rates[] = {
    "5.5-Khz",
    "11-Khz",
    "22-Khz",
    "44-Khz"
};

const char *sound_sizes[] = {
    "8 bit",
    "16 bit"
};

const char *sound_types[] = {
    "Mono",
    "Stereo"
};

void PrintFlvHeader(FlvHeader header)
{
	printf("FLV file version %u\n", header.nVersion);

    printf("  Contains audio tags: ");

    if (header.bHaveAudio) {
        printf("Yes\n");
    } else {
        printf("No\n");
    }

    printf("  Contains video tags: ");

    if (header.bHaveVideo) {
        printf("Yes\n");
    } else {
        printf("No\n");
    }

    printf("  Data offset: %lu\n", (unsigned long)header.nHeadSize);

    return;
}

void PrintTagHeaderGeneral(TagHeader header)
{
	printf("\nTag type: %u - ", header.nType);
            
    if(header.nType == 0x08) {
    	printf("Audio data\n");
    } else if (header.nType == 0x09) {
    	printf("Video data\n");
    } else if (header.nType == 0x12) {
    	printf("Script data object\n");
    }

    printf("  Data size: %lu\n", (unsigned long)header.nDataSize);
    printf("  Timestamp: %lu\n", (unsigned long)header.nTimeStamp);
    printf("  Timestamp extended: %u\n", header.nTSEx);
    printf("  StreamID: %lu\n", (unsigned long)header.nStreamID);

    return;
}

void PrintVideoTag(VideoTag videoTag)
{
	printf("  Video tag:\n");
    printf("    Frame type: %u - %s\n", videoTag.nFrameType, frame_types[videoTag.nFrameType]);
    printf("    Codec ID: %u - %s\n", videoTag.nCodecID, codec_ids[videoTag.nCodecID]);

    if(videoTag.nCodecID == 7) {
    	printf("    AVC video tag:\n");
	    printf("      AVC packet type: %u - %s\n", videoTag.nAVCPacketType, avc_packet_types[videoTag.nAVCPacketType]);
	    printf("      AVC composition time: %i\n", videoTag.nCompositionTime);
    }

	return;
}

void PrintAudioTag(AudioTag audioTag)
{
	printf("  Audio tag:\n");
    printf("    Sound format: %u - %s\n", audioTag.nSoundFormat, sound_formats[audioTag.nSoundFormat]);
    printf("    Sound rate: %u - %s\n", audioTag.nSoundRate, sound_rates[audioTag.nSoundRate]);

    printf("    Sound size: %u - %s\n", audioTag.nSoundSize, sound_sizes[audioTag.nSoundSize]);
    printf("    Sound type: %u - %s\n", audioTag.nSoundType, sound_types[audioTag.nSoundType]);
}

#endif

static unsigned int ShowU32(u_char *pBuf) 
{
	return (pBuf[0] << 24) | (pBuf[1] << 16) | (pBuf[2] << 8) | pBuf[3];
}

static unsigned int ShowU24(u_char *pBuf) 
{
	return (pBuf[0] << 16) | (pBuf[1] << 8) | (pBuf[2]);
}

static unsigned int ShowU16(u_char *pBuf) 
{
	return (pBuf[0] << 8) | (pBuf[1]);
}

static unsigned int ShowU8(u_char *pBuf) 
{
	return (pBuf[0]);
}


typedef struct tuya_flv_demux_handle_ {
	int 					bFlvHeader;
	int 					nNalUnitLength;
	tuya_flv_av_callback_s 	callback;
} tuya_flv_demux_handle_s;


int tuya_flv_parse_h264_configuration(tuya_flv_demux_handle_s* pdemuxhdl, u_char *pTagData)
{
	u_char *pd = pTagData;
	int sps_size, pps_size;

	pdemuxhdl->nNalUnitLength = (pd[9] & 0x03) + 1;

	sps_size = ShowU16(pd + TAG_HEADER_SIZE);
	pps_size = ShowU16(pd + TAG_HEADER_SIZE + (2 + sps_size) + 1);

	if(pdemuxhdl->callback.sps_pps != NULL) {
		pdemuxhdl->callback.sps_pps(pdemuxhdl->callback.priv, (const char *)(pd + TAG_HEADER_SIZE + 2), sps_size, 
							(const char *)(pd + TAG_HEADER_SIZE + 2 + sps_size + 2 + 1), pps_size);
	}

	return 0;
}

int tuya_flv_parse_nalu(tuya_flv_demux_handle_s* pdemuxhdl, TagHeader header, u_char *pTagData)
{
	u_char *pd = pTagData;
	int nOffset = 5;

	while (1) {
		if (nOffset >= header.nDataSize) {
			break;
		}

		int nNaluLen;

		switch (pdemuxhdl->nNalUnitLength) {
		case 4:
			nNaluLen = ShowU32(pd + nOffset);
			break;

		case 3:
			nNaluLen = ShowU24(pd + nOffset);
			break;

		case 2:
			nNaluLen = ShowU16(pd + nOffset);
			break;

		default:
			nNaluLen = ShowU8(pd + nOffset);
			break;
		}

		if(pdemuxhdl->callback.h264_nalu != NULL) {
			uint32_t CompositionTime = ShowU24(pd + 2);
			pdemuxhdl->callback.h264_nalu(pdemuxhdl->callback.priv, (const char *)(pd + nOffset + pdemuxhdl->nNalUnitLength), 
								nNaluLen, header.nTimeStamp, CompositionTime);
		}

		nOffset += (pdemuxhdl->nNalUnitLength + nNaluLen);
	}

	return 0;
}

void tuya_flv_parser_video_tag(tuya_flv_demux_handle_s* pdemuxhdl, TagHeader header, u_char *pBuf)
{
	VideoTag videoTag;
	u_char *pd = pBuf + TAG_HEADER_SIZE;

	memset(&videoTag, 0, sizeof(VideoTag));

	videoTag.nFrameType = (pd[0] & 0xf0) >> 4;
	videoTag.nCodecID = pd[0] & 0x0f;

	if (videoTag.nCodecID == 7) {
		videoTag.nAVCPacketType = pd[1];
		videoTag.nCompositionTime = ShowU24(pd + 2);

		if (videoTag.nAVCPacketType == 0) {
			tuya_flv_parse_h264_configuration(pdemuxhdl, pd);
		} else if (videoTag.nAVCPacketType == 1) {
			tuya_flv_parse_nalu(pdemuxhdl, header, pd);
		} else if (videoTag.nAVCPacketType == 2) {

		}
	}

#ifdef FLV_INFO_DEBUG
	PrintVideoTag(videoTag);
#endif

	return;
}

void tuya_flv_parser_audio_tag(tuya_flv_demux_handle_s* pdemuxhdl, TagHeader header, u_char *pBuf)
{
	AudioTag audioTag;
	u_char *pd = pBuf + TAG_HEADER_SIZE;

	memset(&audioTag, 0, sizeof(AudioTag));

	audioTag.nSoundFormat = (pd[0] & 0xf0) >> 4;
	audioTag.nSoundRate = (pd[0] & 0x0c) >> 2;
	audioTag.nSoundSize = (pd[0] & 0x02) >> 1;
	audioTag.nSoundType = (pd[0] & 0x01);

	if (audioTag.nSoundFormat == 10) {	// AAC
		audioTag.nAACPacketType = pd[1];

		if (audioTag.nAACPacketType == 0) {
			int aacProfile = ((pd[2] & 0xf8) >> 3) - 1;
			int sampleRateIndex = ((pd[2] & 0x07) << 1) | (pd[3] >> 7);
			int channelConfig = (pd[3] >> 3) & 0x0f;

			if(pdemuxhdl->callback.audio_specific != NULL) {
				pdemuxhdl->callback.audio_specific(pdemuxhdl->callback.priv, aacProfile, sampleRateIndex, channelConfig);
			}
		} else if (audioTag.nAACPacketType == 1) {
			if(pdemuxhdl->callback.aac_packet != NULL) {
				int dataSize = header.nDataSize - 2;
				pdemuxhdl->callback.aac_packet(pdemuxhdl->callback.priv, (const char *)pd + 2, dataSize, header.nTimeStamp);
			}
		}
	}

#ifdef FLV_INFO_DEBUG
	PrintAudioTag(audioTag);
#endif

	return;
}

int tuya_flv_parser_tag(tuya_flv_demux_handle_s* pdemuxhdl, u_char *pBuf, int nLeftLen)
{
	TagHeader header;

	header.nType = ShowU8(pBuf + 0);
	header.nDataSize = ShowU24(pBuf + 1);
	header.nTimeStamp = ShowU24(pBuf + 4);
	header.nTSEx = ShowU8(pBuf + 7);
	header.nStreamID = ShowU24(pBuf + 8);
	header.nTotalTS = (uint32_t)((header.nTSEx << 24)) + header.nTimeStamp;

	if((header.nDataSize + TAG_HEADER_SIZE) > nLeftLen) {
		return -1;
	}

#ifdef FLV_INFO_DEBUG
	PrintTagHeaderGeneral(header);
#endif

	switch (header.nType) {
	case 0x09:
		tuya_flv_parser_video_tag(pdemuxhdl, header, pBuf);
		break;

	case 0x08:
		tuya_flv_parser_audio_tag(pdemuxhdl, header, pBuf);
		break;

	case 0x12:
		printf("Script tag!\n");
		break;

	default:
		break;
	}

	return header.nDataSize;
}

int tuya_flv_demux_parser(void** phdl, u_char *pBuf, int nBufSize, int* nUsedLen)
{
	int nOffset = 0;
	tuya_flv_demux_handle_s* pdemuxhdl = (tuya_flv_demux_handle_s*)phdl;

	if (!pdemuxhdl->bFlvHeader) {
		CheckBuffer(FLV_HEADER_SIZE);

		FlvHeader Header;
		u_char *p = pBuf + nOffset;

		Header.nVersion = p[3];
		Header.bHaveAudio = (p[4] >> 2) & 0x01;
		Header.bHaveVideo = (p[4] >> 0) & 0x01;
		Header.nHeadSize = ShowU32(p + 5);

		nOffset += Header.nHeadSize;
		pdemuxhdl->bFlvHeader = 1;

#ifdef FLV_INFO_DEBUG
		PrintFlvHeader(Header);
#endif
	}

	while (1) {
		CheckBuffer(15);

		int nPrevSize = ShowU32(pBuf + nOffset);

		nOffset += 4;

		int datasize = tuya_flv_parser_tag(pdemuxhdl, pBuf + nOffset, nBufSize - nOffset);
		if (datasize < 0) {
			nOffset -= 4;
			break;
		}

		nOffset += (TAG_HEADER_SIZE + datasize);
	}

	*nUsedLen = nOffset;

	return 0;
}

int tuya_flv_demux_create(void** pphdl, tuya_flv_av_callback_s* pcbk)
{
	tuya_flv_demux_handle_s* pdemuxhdl = NULL;

	pdemuxhdl = (tuya_flv_demux_handle_s*)malloc(sizeof(tuya_flv_demux_handle_s));
	if(pdemuxhdl == NULL) {
		return -1;
	}

	memset(pdemuxhdl, 0, sizeof(tuya_flv_demux_handle_s));

	pdemuxhdl->bFlvHeader = 0;
	pdemuxhdl->nNalUnitLength = 4;

	if(pcbk != NULL) {
		memcpy(&pdemuxhdl->callback, pcbk, sizeof(tuya_flv_av_callback_s));
	}

	*pphdl = pdemuxhdl;

	return 0;
}

void tuya_flv_demux_destroy(void** pphdl)
{
	tuya_flv_demux_handle_s* pdemuxhdl = (tuya_flv_demux_handle_s*)*pphdl;

	if(pdemuxhdl != NULL) {
		free(pdemuxhdl);
		pdemuxhdl = NULL;
	}

	return;
}

