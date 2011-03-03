/*
 * Type information and function prototypes for common AD-Holdings demuxer code
 * Copyright (c) 2006-2010 AD-Holdings plc
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file 
 * Type information and function prototypes for common AD-Holdings demuxer code
 */

#ifndef AVFORMAT_ADPIC_H
#define AVFORMAT_ADPIC_H

#include "avformat.h"
#include "ds_exports.h"


#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/// These are the data types that are supported by the DS2 video servers
enum data_type { DATA_JPEG, DATA_JFIF,
                 DATA_MPEG4I, DATA_MPEG4P,
                 DATA_AUDIO_ADPCM, DATA_AUDIO_RAW,
                 DATA_MINIMAL_MPEG4, DATA_MINIMAL_AUDIO_ADPCM,
                 DATA_LAYOUT, DATA_INFO,
                 DATA_H264I, DATA_H264P,
                 DATA_XML_INFO,
                 MAX_DATA_TYPE
               };


int ad_read_header(AVFormatContext *s, AVFormatParameters *ap, int *utcOffset);
void ad_network2host(NetVuImageData *pic, uint8_t *data);
int ad_new_packet(AVPacket *pkt, int size);
int ad_get_buffer(ByteIOContext *s, uint8_t *buf, int size);
int initADData(int data_type, ADFrameType *frameType,
               NetVuImageData **vidDat, NetVuAudioData **audDat);
int ad_read_jpeg(AVFormatContext *s, ByteIOContext *pb,
                 AVPacket *pkt,
                 NetVuImageData *video_data, char **text_data);
int ad_read_jfif(AVFormatContext *s, ByteIOContext *pb,
                 AVPacket *pkt, int manual_size, int size,
                 NetVuImageData *video_data, char **text_data);
int ad_read_info(AVFormatContext *s, ByteIOContext *pb,
                 AVPacket *pkt, int size);
int ad_read_layout(AVFormatContext *s, ByteIOContext *pb,
                   AVPacket *pkt, int size);
int ad_read_packet(AVFormatContext *s, ByteIOContext *pb, AVPacket *pkt,
                   ADFrameType currentFrameType, void *data, char *text_data);
AVStream * ad_get_stream(AVFormatContext *s, uint16_t w, uint16_t h, 
                         uint8_t cam, int format, const char *title);
AVStream * ad_get_audio_stream(AVFormatContext *s, NetVuAudioData* audioHeader);
void audiodata_network2host(uint8_t *data, int size);


#define PIC_REVISION 1
#define MIN_PIC_VERSION 0xDECADE10
#define MAX_PIC_VERSION (MIN_PIC_VERSION + PIC_REVISION)
#define PIC_VERSION (MIN_PIC_VERSION + PIC_REVISION)
#define pic_version_valid(v) ( ( (v)>=MIN_PIC_VERSION ) && ( (v)<=MAX_PIC_VERSION ) )

#define AUD_VERSION 0x00ABCDEF

#define PIC_MODE_JPEG_422        0
#define PIC_MODE_JPEG_411        1
#define PIC_MODE_MPEG4_411       2
#define PIC_MODE_MPEG4_411_I     3
#define PIC_MODE_MPEG4_411_GOV_P 4
#define PIC_MODE_MPEG4_411_GOV_I 5
#define PIC_MODE_H264I           6
#define PIC_MODE_H264P           7
#define PIC_MODE_H264J           8


//AD pic error codes
#define ADPIC_NO_ERROR                              0
#define ADPIC_ERROR                                 -200            //used to offset ad pic errors so that adpic can be identified as the origon of the error

#define ADPIC_UNKNOWN_ERROR                         ADPIC_ERROR + -1 //AVERROR_UNKNOWN     // (-200 + -1) unknown error 
#define ADPIC_READ_6_BYTE_SEPARATOR_ERROR           ADPIC_ERROR + -2
#define ADPIC_NEW_PACKET_ERROR                      ADPIC_ERROR + -3
#define ADPIC_PARSE_MIME_HEADER_ERROR               ADPIC_ERROR + -4
#define ADPIC_NETVU_IMAGE_DATA_ERROR                ADPIC_ERROR + -5
#define ADPIC_NETVU_AUDIO_DATA_ERROR                ADPIC_ERROR + -6

#define ADPIC_JPEG_IMAGE_DATA_READ_ERROR            ADPIC_ERROR + -7
#define ADPIC_JPEG_PIC_VERSION_ERROR                ADPIC_ERROR + -8
#define ADPIC_JPEG_ALOCATE_TEXT_BLOCK_ERROR         ADPIC_ERROR + -9
#define ADPIC_JPEG_READ_TEXT_BLOCK_ERROR            ADPIC_ERROR + -10
#define ADPIC_JPEG_HEADER_ERROR                     ADPIC_ERROR + -11
#define ADPIC_JPEG_NEW_PACKET_ERROR                 ADPIC_ERROR + -12
#define ADPIC_JPEG_READ_BODY_ERROR                  ADPIC_ERROR + -13

#define ADPIC_JFIF_NEW_PACKET_ERROR                 ADPIC_ERROR + -14
#define ADPIC_JFIF_GET_BUFFER_ERROR                 ADPIC_ERROR + -15
#define ADPIC_JFIF_MANUAL_SIZE_ERROR                ADPIC_ERROR + -16

#define ADPIC_MPEG4_MIME_NEW_PACKET_ERROR           ADPIC_ERROR + -17
#define ADPIC_MPEG4_MIME_GET_BUFFER_ERROR           ADPIC_ERROR + -18
#define ADPIC_MPEG4_MIME_PARSE_HEADER_ERROR         ADPIC_ERROR + -19
#define ADPIC_MPEG4_MIME_PARSE_TEXT_DATA_ERROR      ADPIC_ERROR + -20
#define ADPIC_MPEG4_MIME_GET_TEXT_BUFFER_ERROR      ADPIC_ERROR + -21
#define ADPIC_MPEG4_MIME_ALOCATE_TEXT_BUFFER_ERROR  ADPIC_ERROR + -22

#define ADPIC_MPEG4_GET_BUFFER_ERROR                ADPIC_ERROR + -23
#define ADPIC_MPEG4_PIC_VERSION_VALID_ERROR         ADPIC_ERROR + -24
#define ADPIC_MPEG4_ALOCATE_TEXT_BUFFER_ERROR       ADPIC_ERROR + -25
#define ADPIC_MPEG4_GET_TEXT_BUFFER_ERROR           ADPIC_ERROR + -26
#define ADPIC_MPEG4_NEW_PACKET_ERROR                ADPIC_ERROR + -27
#define ADPIC_MPEG4_PIC_BODY_ERROR                  ADPIC_ERROR + -28

#define ADPIC_MPEG4_MINIMAL_GET_BUFFER_ERROR        ADPIC_ERROR + -29
#define ADPIC_MPEG4_MINIMAL_NEW_PACKET_ERROR        ADPIC_ERROR + -30
#define ADPIC_MPEG4_MINIMAL_NEW_PACKET_ERROR2       ADPIC_ERROR + -31

#define ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR  ADPIC_ERROR + -32
#define ADPIC_MINIMAL_AUDIO_ADPCM_NEW_PACKET_ERROR  ADPIC_ERROR + -33
#define ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR2 ADPIC_ERROR + -34

#define ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR          ADPIC_ERROR + -35
#define ADPIC_AUDIO_ADPCM_ALOCATE_ADITIONAL_ERROR   ADPIC_ERROR + -36
#define ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR2         ADPIC_ERROR + -37

#define ADPIC_AUDIO_ADPCM_MIME_NEW_PACKET_ERROR     ADPIC_ERROR + -38
#define ADPIC_AUDIO_ADPCM_MIME_GET_BUFFER_ERROR     ADPIC_ERROR + -39

#define ADPIC_INFO_NEW_PACKET_ERROR                 ADPIC_ERROR + -40
#define ADPIC_INFO_GET_BUFFER_ERROR                 ADPIC_ERROR + -41

#define ADPIC_LAYOUT_NEW_PACKET_ERROR               ADPIC_ERROR + -42
#define ADPIC_LAYOUT_GET_BUFFER_ERROR               ADPIC_ERROR + -43

#define ADPIC_DEFAULT_ERROR                         ADPIC_ERROR + -44

#define ADPIC_GET_STREAM_ERROR                      ADPIC_ERROR + -45
#define ADPIC_GET_AUDIO_STREAM_ERROR                ADPIC_ERROR + -46
#define ADPIC_GET_INFO_LAYOUT_STREAM_ERROR          ADPIC_ERROR + -47

#define ADPIC_END_OF_STREAM                         ADPIC_ERROR + -48
#define ADPIC_FAILED_TO_PARSE_INFOLIST              ADPIC_ERROR + -49

#endif
