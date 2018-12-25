#ifndef __MEMPOOL_H__
#define __MEMPOOL_H__

#include <unistd.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef struct mempool_param_ {
	size_t size;
	int block_size;
} mempool_param_s;


void* mempool_create(mempool_param_s* pparam);


void mempool_destroy(void* phandle);


void* mempool_malloc(void* phandle, size_t size);


void mempool_free(void* phandle, void* ptr);


#ifdef __cplusplus
}
#endif

#endif