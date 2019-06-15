#ifndef _RINGFIFO_H_
#define _RINGFIFO_H_


struct ringbuf {
    unsigned char *buffer;
	int frame_type;
    int size;
};
int ringget(struct ringbuf *getinfo);
void ringput(unsigned char *buffer,int size,int encode_type);
void ringfree();
int ringmalloc(int size);
void ringreset();
int PutH264DataToBuffer(unsigned char *data , int size , int iframe);

#endif
