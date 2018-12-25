#ifndef __buddy_H__
#define __buddy_H__

#ifdef __cplusplus
extern "C" {
#endif

struct buddy;

struct buddy* buddy_create(int size);

void buddy_destroy(struct buddy* self);

int buddy_alloc(struct buddy* self, int size);

void buddy_free(struct buddy* self, int offset);

int buddy_size(struct buddy* self, int offset);

void buddy_dump(struct buddy* self);


#ifdef __cplusplus
}
#endif

#endif//__buddy_H__
