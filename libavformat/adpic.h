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


#ifdef AD_SIDEDATA_IN_PRIV
int ad_new_packet(AVPacket *pkt, int size);
#else
#define ad_new_packet av_new_packet
#endif


#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/// These are the data types that are supported by the DS2 video servers
enum ff_ad_data_type {  AD_DATATYPE_JPEG = 0,
                        AD_DATATYPE_JFIF,
                        AD_DATATYPE_MPEG4I,
                        AD_DATATYPE_MPEG4P,
                        AD_DATATYPE_AUDIO_ADPCM,
                        AD_DATATYPE_AUDIO_RAW,
                        AD_DATATYPE_MINIMAL_MPEG4,
                        AD_DATATYPE_MINIMAL_AUDIO_ADPCM,
                        AD_DATATYPE_LAYOUT,
                        AD_DATATYPE_INFO,
                        AD_DATATYPE_H264I,
                        AD_DATATYPE_H264P,
                        AD_DATATYPE_XML_INFO,
                        AD_DATATYPE_BMP,
                        AD_DATATYPE_PBM,
                        AD_DATATYPE_SVARS_INFO,
                        AD_DATATYPE_MAX
                      };

typedef struct {
    int64_t lastVideoPTS;
    int     utc_offset;     ///< Only used in minimal video case
    int     metadataSet;
    enum ff_ad_data_type streamDatatype;
} AdContext;


int ad_read_header(AVFormatContext *s, int *utcOffset);
void ad_network2host(struct NetVuImageData *pic, uint8_t *data);
int initADData(int data_type, enum AVMediaType *media, enum AVCodecID *codecId, void **payload);
int ad_read_jpeg(AVFormatContext *s, AVPacket *pkt, struct NetVuImageData *vid, char **txt);
int ad_read_jfif(AVFormatContext *s, AVPacket *pkt, int manual_size, int size,
                 struct NetVuImageData *video_data, char **text_data);
int ad_read_info(AVFormatContext *s, AVPacket *pkt, int size);
int ad_read_layout(AVFormatContext *s, AVPacket *pkt, int size);
int ad_read_overlay(AVFormatContext *s, AVPacket *pkt, int channel, int size, char **text_data);
int ad_read_packet(AVFormatContext *s, AVPacket *pkt, int channel,
                   enum AVMediaType mediaType, enum AVCodecID codecId, 
                   void *data, char *text_data);
AVStream * ad_get_vstream(AVFormatContext *s, uint16_t w, uint16_t h,
                          uint8_t cam, int format, const char *title);
AVStream * ad_get_audio_stream(AVFormatContext *s, struct NetVuAudioData* audioHeader);
void audiodata_network2host(uint8_t *data, const uint8_t *src, int size);
int ad_adFormatToCodecId(AVFormatContext *s, int32_t adFormat);
int mpegOrH264(unsigned int startCode);
int ad_pbmDecompress(char **comment, uint8_t **src, int size, AVPacket *pkt, int *width, int *height);


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

#endif
