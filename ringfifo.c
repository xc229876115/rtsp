/*ringbuf .c*/

#include<stdio.h>
#include<ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ringfifo.h"
#include "rtputils.h"
#include "rtspservice.h"
#define NMAX 32

typedef struct {
    char init;
    pthread_mutex_t lock ;
    int iput; /* 环形缓冲区的当前放入位置 */
    int iget; /* 缓冲区的当前取出位置 */
    int n; /* 环形缓冲区中的元素总数量 */
}Ring_t;

Ring_t gRing = {0};
struct ringbuf ringfifo[NMAX];
extern int UpdateSpsOrPps(unsigned char *data,int frame_type,int len);
/* 环形缓冲区的地址编号计算函数，如果到达唤醒缓冲区的尾部，将绕回到头部。
环形缓冲区的有效地址编号为：0到(NMAX-1)
*/
int ringmalloc(int bufSize)
{
    int i;

    if(gRing.init)
        return 0;

    memset(&gRing,0,sizeof(Ring_t));
	pthread_mutex_init(&gRing.lock,NULL);	//初始化锁
	
    for(i =0; i<NMAX; i++)
    {
        ringfifo[i].size = 0;
        ringfifo[i].frame_type = 0;
        ringfifo[i].buffer = malloc(bufSize);
        if(ringfifo[i].buffer == NULL)
        {
            fprintf(stderr,"[%s:%d] malloc error : %s\n",__FUNCTION__,__LINE__,strerror(errno));
            goto err;
        }
    }

    gRing.iput = 0; /* 环形缓冲区的当前放入位置 */
    gRing.iget = 0; /* 缓冲区的当前取出位置 */
    gRing.n = 0;    /* 环形缓冲区中的元素总数量 */
    gRing.init = 1;

    return 0;
err:
    for(i =0; i<NMAX; i++)
    {

        if(ringfifo[i].buffer)
        {
            free(ringfifo[i].buffer);
            ringfifo[i].buffer = NULL;
        }
    }

    return -1;
}
/**************************************************************************************************
**
**
**
**************************************************************************************************/
void ringreset()
{
    pthread_mutex_lock(&gRing.lock);
    gRing.iput = 0; /* 环形缓冲区的当前放入位置 */
    gRing.iget = 0; /* 缓冲区的当前取出位置 */
    gRing.n    = 0; /* 环形缓冲区中的元素总数量 */
    pthread_mutex_unlock(&gRing.lock);
    return;
}
/**************************************************************************************************
**
**
**
**************************************************************************************************/
void ringfree(void)
{
    int i;
    if(gRing.init)
    {
        pthread_mutex_lock(&gRing.lock);
        for(i =0; i<NMAX; i++)
        {
            if(ringfifo[i].buffer)
            {
                free(ringfifo[i].buffer);
                ringfifo[i].buffer = NULL;
                ringfifo[i].size = 0;
            }
        }
        pthread_mutex_unlock(&gRing.lock);
    }
    return;
}
/**************************************************************************************************
**
**
**
**************************************************************************************************/
static int addring(int i)
{
    return (i+1) == NMAX ? 0 : i+1;
}

/**************************************************************************************************
**
**
**
**************************************************************************************************/
/* 从环形缓冲区中取一个元素 */
int ringget(struct ringbuf *getinfo)
{
    int Pos;
    int size = 0;
    pthread_mutex_lock(&gRing.lock);
    if(gRing.n>0 && gRing.init)
    {
        Pos = gRing.iget;
        gRing.iget = addring(gRing.iget);
        gRing.n--;
        getinfo->buffer = (ringfifo[Pos].buffer);
        getinfo->frame_type = ringfifo[Pos].frame_type;
        getinfo->size = ringfifo[Pos].size;
        size = ringfifo[Pos].size;
    }
    else
    {
#ifdef DEBUG
        //printf("Buffer is empty\n");
#endif
    }    
    pthread_mutex_unlock(&gRing.lock);
    
    return size;
}
/**************************************************************************************************
**
**
**
**************************************************************************************************/
/* 向环形缓冲区中放入一个元素*/
void ringput(unsigned char *buffer,int size,int encode_type)
{
    pthread_mutex_lock(&gRing.lock);
    if(gRing.n<NMAX && gRing.init)
    {
        int iput = gRing.iput;
        memcpy(ringfifo[iput].buffer,buffer,size);
        ringfifo[iput].size= size;
        ringfifo[iput].frame_type = encode_type;
        gRing.iput = addring(iput);
        gRing.n++;
    }
    pthread_mutex_unlock(&gRing.lock);
    return;
}

/**************************************************************************************************
**
**
**
**************************************************************************************************/
#if 0
HI_S32 HisiPutH264DataToBuffer(VENC_STREAM_S *pstStream)
{
	HI_S32 i,j;
	HI_S32 len=0,off=0,len2=2;
	unsigned char *pstr;
	int iframe=0;
	for (i = 0; i < pstStream->u32PackCount; i++)
	{
		len+=pstStream->pstPack[i].u32Len[0];

		if (pstStream->pstPack[i].u32Len[1] > 0)
		{
			len+=pstStream->pstPack[i].u32Len[1];
		}
	}


    if(n<NMAX)
    {
		for (i = 0; i < pstStream->u32PackCount; i++)
		{
			memcpy(ringfifo[iput].buffer+off,pstStream->pstPack[i].pu8Addr[0],pstStream->pstPack[i].u32Len[0]);
			off+=pstStream->pstPack[i].u32Len[0];
			pstr=pstStream->pstPack[i].pu8Addr[0];

			if(pstr[4]==0x67)
			{
				UpdateSps(ringfifo[iput].buffer+off,9);
				iframe=1;
			}
			if(pstr[4]==0x68)
			{
				UpdatePps(ringfifo[iput].buffer+off,4);
			}

			if (pstStream->pstPack[i].u32Len[1] > 0)
			{
				memcpy(ringfifo[iput].buffer+off,pstStream->pstPack[i].pu8Addr[1], pstStream->pstPack[i].u32Len[1]);
				off+=pstStream->pstPack[i].u32Len[1];
			}
		}

        ringfifo[iput].size= len;
		if(iframe)
		{
			ringfifo[iput].frame_type = FRAME_TYPE_I;
		}
        	
		else
			ringfifo[iput].frame_type = FRAME_TYPE_P;
        iput = addring(iput);
        n++;
    }

	return HI_SUCCESS;
}
#endif

void hexdump(uint8_t *data , int size)
{
	int i;
	for(i=0;i<size;i++){
		printf("0x%02x ",data[i]);
	}
	printf("\n");
	return;
}

typedef struct _NaluUnit
{
	int type;
    int size;
	unsigned char *data;
}NaluUnit;


int ReadOneNaluFromBuf(NaluUnit *nalu,unsigned char *buf, unsigned int size)
{
	unsigned int  nalhead_pos = 0;
	int nal_offset=nalhead_pos;
	int one_nalu = 1; //本地读取buf是否包含多个nal
	
	if(size < 4)
	{
		printf("buf size is too small %d\n",size);
		return 0;
	}

	memset(nalu,0,sizeof(NaluUnit));
	while(nalhead_pos + 4 <size)
	{
		//search for nal header ，NALU 单元的开始, 必须是 "00 00 00 01" 或 "00 00 01",
				// find next nal header 00 00 00 01 to calu the length of last nal
		if(buf[nalhead_pos++] == 0x00 &&
			buf[nalhead_pos++] == 0x00 &&
				buf[nalhead_pos++] == 0x00 &&
					buf[nalhead_pos++] == 0x01)
		{
		}
		else
		{
			continue;
		}
		//search for nal tail which is also the head of next nal
		nal_offset = nalhead_pos;
		while (nal_offset + 4 < size)
		{
			// find next nal header 00 00 00 01 to calu the length of last nal
			if(buf[nal_offset++] == 0x00 &&
				buf[nal_offset++] == 0x00 &&
					buf[nal_offset++] == 0x00 &&
						buf[nal_offset++] == 0x01)
			{
				nalu->size = (nal_offset-4)-nalhead_pos; //nal头4个字节
				one_nalu = 0;
				break;
			}
			else
				continue;

		}
		if(one_nalu)
		{
			nal_offset = size;
			nalu->size = nal_offset-nalhead_pos; //nal头4个字节
		}
		nalu->type = buf[nalhead_pos]&0x1f; 	// 7-> pps , 8->sps
		if(nalu->size)
		{
			nalu->data= (unsigned char *)malloc(nalu->size);
			if(nalu->data)
				memcpy(nalu->data,buf+nalhead_pos,nalu->size);
			else
				printf("malloc for nal data err %s\n",strerror(errno));
		}

		nalhead_pos =(one_nalu==1) ? nal_offset:(nal_offset-4);
		break;
	}

	return nalhead_pos;
}

void extract_spspps(uint8_t *data , int size )
{
	NaluUnit naluUnit = {0};
	int offset = 0;
	int ret;

	static int update_flag = 0;
	if(update_flag == 2)
		return;
	
	while((ret = ReadOneNaluFromBuf(&naluUnit,data+offset,size-offset)))
	{
#ifdef DEBUG
		printf("Nal type -> %d\n",naluUnit.type);
#endif
		if(naluUnit.type == 7)  // 7-> pps , 8->sps
		{
			UpdatePps(naluUnit.data,naluUnit.size);
			update_flag ++;
//#ifdef DEBUG
			printf("pps frame info is :\n");
			hexdump(naluUnit.data,naluUnit.size);
//#endif
			printf("update pps done\n");
			
		}else if(naluUnit.type == 8)
		{	
			update_flag ++;
			UpdateSps(naluUnit.data,naluUnit.size);
//#ifdef DEBUG
			printf("sps frame info is :\n");
			hexdump(naluUnit.data,naluUnit.size);
//#endif
			printf("update sps done\n");
		}
		
		if(naluUnit.data)
			free(naluUnit.data);
		
		memset(&naluUnit,0,sizeof(NaluUnit));
		offset += ret;
		if(offset >= size)
			break;
	}

	return ;
}


