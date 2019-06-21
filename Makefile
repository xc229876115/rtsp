HOST	:=
CC		?= $(HOST)gcc
STRIP	?=	$(HOST)strip

INCLUDE	=-I./include/
LIBS	=-lpthread -lm

CFLAGS	+= $(INCLUDE) -g -Wall 
CFLAGS	+=-DDEBUG
LDFLAGS	+=$(LIBS)

SRC	:=main.c ringfifo.c rtputils.c rtspservice.c rtsputils.c g711_utils.c
target := rtsp

all:$(target)



rtsp:$(SRC)
	$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@  

clean:
	rm -rfv rtsp



.PHONY: clean $(target) all

