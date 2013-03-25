FATE_AMIX += fate-filter-amix-simple
fate-filter-amix-simple: CMD = ffmpeg -filter_complex amix -i $(SRC) -ss 3 -i $(SRC1) -f f32le -
fate-filter-amix-simple: REF = $(SAMPLES)/filter/amix_simple.pcm

FATE_AMIX += fate-filter-amix-first
fate-filter-amix-first: CMD = ffmpeg -filter_complex amix=duration=first -ss 4 -i $(SRC) -i $(SRC1) -f f32le -
fate-filter-amix-first: REF = $(SAMPLES)/filter/amix_first.pcm

FATE_AMIX += fate-filter-amix-transition
fate-filter-amix-transition: tests/data/asynth-44100-2-3.wav
fate-filter-amix-transition: SRC2 = $(TARGET_PATH)/tests/data/asynth-44100-2-3.wav
fate-filter-amix-transition: CMD = ffmpeg -filter_complex amix=inputs=3:dropout_transition=0.5 -i $(SRC) -ss 2 -i $(SRC1) -ss 4 -i $(SRC2) -f f32le -
fate-filter-amix-transition: REF = $(SAMPLES)/filter/amix_transition.pcm

$(FATE_AMIX): tests/data/asynth-44100-2.wav tests/data/asynth-44100-2-2.wav
$(FATE_AMIX): SRC  = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
$(FATE_AMIX): SRC1 = $(TARGET_PATH)/tests/data/asynth-44100-2-2.wav
$(FATE_AMIX): CMP  = oneoff
$(FATE_AMIX): CMP_UNIT = f32

FATE_FILTER-$(CONFIG_AMIX_FILTER) += $(FATE_AMIX)

FATE_FILTER-$(CONFIG_ASYNCTS_FILTER) += fate-filter-asyncts
fate-filter-asyncts: SRC = $(SAMPLES)/nellymoser/nellymoser-discont.flv
fate-filter-asyncts: CMD = pcm -analyzeduration 10000000 -i $(SRC) -af asyncts
fate-filter-asyncts: CMP = oneoff
fate-filter-asyncts: REF = $(SAMPLES)/nellymoser/nellymoser-discont-async-v2.pcm

FATE_FILTER-$(CONFIG_ARESAMPLE_FILTER) += fate-filter-aresample
fate-filter-aresample: SRC = $(SAMPLES)/nellymoser/nellymoser-discont.flv
fate-filter-aresample: CMD = pcm -i $(SRC) -af aresample=min_comp=0.001:min_hard_comp=0.1:first_pts=0
fate-filter-aresample: CMP = oneoff
fate-filter-aresample: REF = $(SAMPLES)/nellymoser/nellymoser-discont.pcm

fate-filter-delogo: CMD = framecrc -i $(SAMPLES)/real/rv30.rm -vf perms=random,delogo=show=0:x=290:y=25:w=26:h=16 -an

FATE_FILTER-$(call ALLYES, PERMS_FILTER DELOGO_FILTER) += fate-filter-delogo

FATE_YADIF += fate-filter-yadif-mode0
fate-filter-yadif-mode0: CMD = framecrc -flags bitexact -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -vframes 30 -vf yadif=0

FATE_YADIF += fate-filter-yadif-mode1
fate-filter-yadif-mode1: CMD = framecrc -flags bitexact -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -vframes 59 -vf yadif=1

FATE_FILTER-$(CONFIG_YADIF_FILTER) += $(FATE_YADIF)

FATE_HQDN3D += fate-filter-hqdn3d
fate-filter-hqdn3d: CMD = framecrc -idct simple -i $(SAMPLES)/smjpeg/scenwin.mjpg -vf perms=random,hqdn3d -an
FATE_FILTER-$(call ALLYES, SMJPEG_DEMUXER MJPEG_DECODER PERMS_FILTER HQDN3D_FILTER) += $(FATE_HQDN3D)

fate-filter-curves: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgb_median.avi -vf "perms=random,curves=r=0/0.11@.42/.51@1/0.95:g=0.50/0.48:b=0/0.22@.49/.44@1/0.8"
FATE_FILTER-$(call ALLYES, UTVIDEO_DECODER AVI_DEMUXER PERMS_FILTER CURVES_FILTER) += fate-filter-curves

FATE_GRADFUN += fate-filter-gradfun
fate-filter-gradfun: CMD = framecrc -i $(SAMPLES)/vmd/12.vmd -vf "sws_flags=+accurate_rnd+bitexact;format=gray,perms=random,gradfun=10:8" -an -frames:v 20
FATE_FILTER-$(call ALLYES, VMD_DEMUXER VMDVIDEO_DECODER FORMAT_FILTER PERMS_FILTER GRADFUN_FILTER) += $(FATE_GRADFUN)

fate-filter-concat: CMD = framecrc -lavfi "testsrc=r=5:n=1:d=2[v1];sine=440:b=2:d=1[a1];testsrc=r=5:n=1:d=1[v2];sine=622:b=2:d=2[a2];testsrc=r=5:n=1:d=1[v3];sine=880:b=2:d=1[a3];[v1][a1][v2][a2][v3][a3]concat=v=1:a=1:n=3"
FATE_FILTER-$(call ALLYES, TESTSRC_FILTER SINE_FILTER CONCAT_FILTER) += fate-filter-concat

FATE_SAMPLES_AVCONV += $(FATE_FILTER-yes)

#
# Metadata tests
#
FILTER_METADATA_COMMAND = ffprobe$(EXESUF) -of compact=p=0 -show_entries frame=pkt_pts:frame_tags -bitexact -f lavfi

SCENEDETECT_DEPS = FFPROBE LAVFI_INDEV MOVIE_FILTER SELECT_FILTER SCALE_FILTER \
                   AVCODEC AVDEVICE MOV_DEMUXER SVQ3_DECODER ZLIB
FATE_METADATA_FILTER-$(call ALLYES, $(SCENEDETECT_DEPS)) += fate-filter-metadata-scenedetect
fate-filter-metadata-scenedetect: SRC = $(SAMPLES)/svq3/Vertical400kbit.sorenson3.mov
fate-filter-metadata-scenedetect: CMD = run $(FILTER_METADATA_COMMAND) "sws_flags=+accurate_rnd+bitexact;movie='$(SRC)',select=gt(scene\,.4)"

SILENCEDETECT_DEPS = FFPROBE AVDEVICE LAVFI_INDEV AMOVIE_FILTER AMR_DEMUXER AMRWB_DECODER SILENCEDETECT_FILTER
FATE_METADATA_FILTER-$(call ALLYES, $(SILENCEDETECT_DEPS)) += fate-filter-metadata-silencedetect
fate-filter-metadata-silencedetect: SRC = $(SAMPLES)/amrwb/seed-12k65.awb
fate-filter-metadata-silencedetect: CMD = run $(FILTER_METADATA_COMMAND) "amovie='$(SRC)',silencedetect=d=-20dB"

EBUR128_METADATA_DEPS = FFPROBE AVDEVICE LAVFI_INDEV AMOVIE_FILTER FLAC_DEMUXER FLAC_DECODER EBUR128_FILTER
FATE_METADATA_FILTER-$(call ALLYES, $(EBUR128_METADATA_DEPS)) += fate-filter-metadata-ebur128
fate-filter-metadata-ebur128: SRC = $(SAMPLES)/filter/seq-3341-7_seq-3342-5-24bit.flac
fate-filter-metadata-ebur128: CMD = run $(FILTER_METADATA_COMMAND) "amovie='$(SRC)',ebur128=metadata=1"

FATE_SAMPLES_FFPROBE += $(FATE_METADATA_FILTER-yes)

fate-filter: $(FATE_FILTER-yes) $(FATE_METADATA_FILTER-yes)
