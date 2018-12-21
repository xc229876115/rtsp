INCLUDE=-I./include/
LIBS=./lib/libmpi.a ./lib/libVoiceEngine.a ./lib/libaec.a ./lib/libresampler.a ./lib/libanr.a ./lib/libisp.a ./lib/libsns_imx122.a -lpthread -lm
all:rtsp
rtsp:
	arm-hisiv100nptl-linux-gcc -o rtsp -Dhi3518 -DHICHIP=0x35180100 -DSENSOR_TYPE=SONY_IMX122_DC_1080P_30FPS -DHI_DEBUG -DHI_XXXX main.c ringfifo.c rtputils.c rtspservice.c rtsputils.c loadbmp.c sample_comm_audio.c sample_comm_isp.c sample_comm_sys.c sample_comm_vda.c sample_comm_venc.c sample_comm_vi.c sample_comm_vo.c sample_comm_vpss.c sample_venc.c $(INCLUDE) $(LIBS)
clean:
	rm -rfv rtsp