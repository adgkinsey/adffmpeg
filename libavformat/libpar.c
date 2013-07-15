/*
 * AD-Holdings PAR file demuxer
 * Copyright (c) 2010 AD-Holdings plc
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
 * AD-Holdings PAR file demuxer
 */

#include <strings.h>
#include <parreader.h>

#include "avformat.h"
#include "internal.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"
#include "libpar.h"
#include "adpic.h"


typedef struct {
    ParDisplaySettings dispSet;
    ParFrameInfo frameInfo;
    int fileChanged;
    int frameCached;
    unsigned long seqStartAdded;
} PARDecContext;

struct PAREncStreamContext {
    int index;
    char name[64];
    int camera;
    int64_t startTime;
    int utc_offset;
    //struct PAREncStreamContext *next;
};

typedef struct {
    ParFrameInfo frameInfo;
    struct PAREncStreamContext master;
    struct PAREncStreamContext stream[20];
    //struct PAREncStreamContext *firstStream;
    int picHeaderSize;
} PAREncContext;

#ifdef AD_SIDEDATA_IN_PRIV
void libpar_packet_destroy(struct AVPacket *packet);
#endif


const unsigned int MAX_FRAMEBUFFER_SIZE = 512 * 1024;


static void importMetadata(const AVDictionaryEntry *tag, struct PAREncStreamContext *ps)
{
    if (av_strcasecmp(tag->key, "title") == 0)
        av_strlcpy(ps->name, tag->value, sizeof(ps->name));
    else if (av_strcasecmp(tag->key, "date") == 0)  {
        av_parse_time(&ps->startTime, tag->value, 0);
        ps->startTime *= 1000;
    }
    else if (av_strcasecmp(tag->key, "track") == 0)
        sscanf(tag->value, "%d", &(ps->camera));
    else if (av_strcasecmp(tag->key, "timezone") == 0)
        sscanf(tag->value, "%d", &(ps->utc_offset));
}

static void parreaderLogger(int level, const char *format, va_list args)
{
    int av_log_level = -1;
    switch(level)  {
        case (PARREADER_LOG_CRITICAL):
            av_log_level = AV_LOG_FATAL;
            break;
        case(PARREADER_LOG_ERROR):
            av_log_level = AV_LOG_ERROR;
            break;
        case(PARREADER_LOG_WARNING):
            av_log_level = AV_LOG_WARNING;
            break;
        case(PARREADER_LOG_INFO):
            av_log_level = AV_LOG_INFO;
            break;
        case(PARREADER_LOG_DEBUG):
            av_log_level = AV_LOG_DEBUG;
            break;
    }
    av_vlog(NULL, av_log_level, format, args);
}


static int par_write_header(AVFormatContext *avf)
{
    PAREncContext *p = avf->priv_data;
    AVDictionaryEntry *tag = NULL;
    int ii, result;
    char *fnameNoExt;

    p->picHeaderSize = parReader_getPicStructSize();
    do  {
        tag = av_dict_get(avf->metadata, "", tag, AV_DICT_IGNORE_SUFFIX);
        if (tag)
            importMetadata(tag, &(p->master));
    }
    while (tag);

    for (ii = 0; ii < avf->nb_streams; ii++)  {
        AVStream *st = avf->streams[ii];
        
        // Set timebase to 1 millisecond, and min frame rate to 1 / timebase
        avpriv_set_pts_info(st, 32, 1, 1000);
        st->r_frame_rate = (AVRational) { 1, 1 };
    }

    // parReader_initWritePartition will automatically append .par to filename
    // so strip it off name passed in to prevent file being called file.par.par
    ii = strlen(avf->filename);
    fnameNoExt = av_malloc(ii+1);
    strcpy(fnameNoExt, avf->filename);
    if (av_strcasecmp(avf->filename + ii - 4, ".par") == 0)
        fnameNoExt[ii - 4] = '\0';
    result = parReader_initWritePartition(&p->frameInfo, fnameNoExt, -1);
    av_free(fnameNoExt);

    if (result == 1)
        return 0;
    else
        return AVERROR(EIO);
}

static int par_write_packet(AVFormatContext *avf, AVPacket * pkt)
{
    PAREncContext *p = avf->priv_data;
    //struct PAREncStreamContext *ps = p->firstStream;
    struct PAREncStreamContext *ps = &(p->stream[pkt->stream_index]);
    AVDictionaryEntry *tag = NULL;
    int64_t parTime;
    AVStream *stream = avf->streams[pkt->stream_index];
    void *hdr;
    uint8_t *ptr;
    int parFrameFormat;
    int64_t srcTime = pkt->pts;
    //uint32_t pktTypeCheck;
    int written = 0;
    int isADformat = 0;

    // Metadata
    if (ps->camera < 1)  {
        // Copy over the values from the file data first
        *ps = p->master;

        // Now check if there are stream-specific values
        do  {
            tag = av_dict_get(stream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX);
            if (tag)
                importMetadata(tag, ps);
        }
        while (tag);

        if (ps->camera < 1)
            ps->camera = pkt->stream_index + 1;
        if (strlen(ps->name) == 0)
            snprintf(ps->name, sizeof(ps->name), "Camera %d", ps->camera);
    }

    isADformat = 0;
#ifdef AD_SIDEDATA
    av_packet_split_side_data(pkt);
    for (int ii = 0; ii < pkt->side_data_elems; ii++)  {
        if (pkt->side_data[ii].type == AV_PKT_DATA_AD_FRAME)
            isADformat = 1;
    }
#endif
    
    if (isADformat)  {
        uint8_t *combBuf = NULL, *combPtr = NULL;
        int combSize = 0;
        for (int ii = 0; ii < pkt->side_data_elems; ii++)  {
#ifdef AD_SIDEDATA
            if ( (pkt->side_data[ii].type == AV_PKT_DATA_AD_FRAME) || (pkt->side_data[ii].type == AV_PKT_DATA_AD_TEXT) )  {
                combBuf = av_realloc(combBuf, combSize + pkt->side_data[ii].size);
                combPtr = combBuf + combSize;
                memcpy(combPtr, pkt->side_data[ii].data, pkt->side_data[ii].size);
                combSize += pkt->side_data[ii].size;
            }
#endif
        }
        
        combBuf = av_realloc(combBuf, combSize + pkt->size);
        combPtr = combBuf + combSize;
        memcpy(combPtr, pkt->data, pkt->size);
        combSize += pkt->size;
        
        p->frameInfo.frameBuffer = combBuf;
        p->frameInfo.frameBufferSize = combSize;
        written = parReader_writePartition(&p->frameInfo);
        return written;
    }
    else  {
        // PAR files have timestamps that are in UTC, not elapsed time
        // So if the pts we have been given is the latter we need to
        // add an offset to convert it to the former
        
        if (srcTime < 0)
            srcTime = pkt->dts;
        if ( (srcTime == 0) && (ps->startTime == 0) )  {
            AVDictionaryEntry *ffstarttime = av_dict_get(avf->metadata, "creation_time", NULL, 0);
            int64_t ffstarttimeint = 0;        
            if (ffstarttime)
                sscanf(ffstarttime->value, "%"PRId64"", &ffstarttimeint);
            
            if (avf->start_time_realtime > 0)
                ps->startTime = avf->start_time_realtime / 1000;
            else if (&ffstarttimeint > 0)
                ps->startTime = ffstarttimeint * 1000;
            else
                ps->startTime = av_gettime() / 1000;
        }
        parTime = srcTime;
        if (parTime < ps->startTime)
            parTime = parTime + ps->startTime;

        if (stream->codec->codec_id == CODEC_ID_MJPEG)  {
            //p->frameInfo.frameBuffer = parReader_jpegToIMAGE(pkt->data, parTime, pkt->stream_index);
            if ((stream->codec->pix_fmt == PIX_FMT_YUV422P) || (stream->codec->pix_fmt == PIX_FMT_YUVJ422P) )
                parFrameFormat = FRAME_FORMAT_JPEG_422;
            else
                parFrameFormat = FRAME_FORMAT_JPEG_411;
        }
        else if (stream->codec->codec_id == CODEC_ID_MPEG4) {
            if (pkt->flags & AV_PKT_FLAG_KEY)
                parFrameFormat = FRAME_FORMAT_MPEG4_411_GOV_I;
            else
                parFrameFormat = FRAME_FORMAT_MPEG4_411_GOV_P;
        }
        else  {
            if (pkt->flags & AV_PKT_FLAG_KEY)
                parFrameFormat = FRAME_FORMAT_H264_I;
            else
                parFrameFormat = FRAME_FORMAT_H264_P;
        }

        hdr = parReader_generatePicHeader(ps->camera,
                                          parFrameFormat,
                                          pkt->size,
                                          parTime,
                                          ps->name,
                                          stream->codec->width,
                                          stream->codec->height,
                                          ps->utc_offset
                                         );
        p->frameInfo.frameBufferSize = pkt->size + p->picHeaderSize;
        ptr = av_malloc(p->frameInfo.frameBufferSize);
        if (ptr == NULL)
            return AVERROR(ENOMEM);
        p->frameInfo.frameBuffer = ptr;
        memcpy(ptr, hdr, p->picHeaderSize);
        memcpy(ptr + p->picHeaderSize, pkt->data, pkt->size);
        written = parReader_writePartition(&p->frameInfo);
        av_free(ptr);
        parReader_freePicHeader(hdr);

        return written;
    }
}

static int par_write_trailer(AVFormatContext *avf)
{
    PAREncContext *p = avf->priv_data;
    parReader_closeWritePartition(&p->frameInfo);
    return 0;
}

#ifdef AD_SIDEDATA_IN_PRIV
void libpar_packet_destroy(struct AVPacket *packet)
{
    LibparFrameExtra *fed = (LibparFrameExtra*)packet->priv;

    if (packet->data == NULL) {
        return;
    }

    if (fed)  {
        if (fed->frameInfo->frameBuffer)
            av_free(fed->frameInfo->frameBuffer);
        if (fed->frameInfo)
            av_free(fed->frameInfo);

        if (fed->indexInfo)
            parReader_freeIndexInfo(fed->indexInfo);

        av_free(fed);
    }

    av_destruct_packet(packet);
}
#endif

static void endianSwapAudioData(uint8_t *data, int size)
{
    const uint8_t *dataEnd = data + size;
    uint8_t upper, lower;
    uint16_t predictor = AV_RB16(data);

    AV_WL16(data, predictor);
    data += 4;

    for (;data < dataEnd; data++)  {
        upper = ((*data) & 0xF0) >> 4;
        lower = ((*data) & 0x0F) << 4;
        *data = upper | lower;
    }
}

static int64_t getLastFrameTime(int fc, ParFrameInfo *fi, ParDisplaySettings *disp)
{
    int64_t lastFrame = 0;
    int isReliable;
    
    
    lastFrame = parReader_getEndTime(fi, &isReliable) * 1000LL;
    
    if (isReliable == 0)  {
        long startFrame = fi->frameNumber;
        int streamId = fi->channel;
        int ii = 1;
        
        disp->cameraNum = fi->channel & 0xFFFF;
        disp->fileSeqNo = -1;
        disp->fileLock = 1;    // Don't seek beyond the file
        do  {
            disp->frameNumber = fc - ii++;
            if (disp->frameNumber <= startFrame)
                break;
            parReader_loadFrame(fi, disp, NULL);
        } while (streamId != fi->channel);
        
        lastFrame = fi->imageTime * 1000LL + fi->imageMS;
        
        // Now go back to where we were and reset the state
        disp->playMode = RWND;
        disp->frameNumber = startFrame;
        parReader_loadFrame(fi, disp, NULL);
        disp->playMode = PLAY;
        disp->fileLock = 0;
    }
        
    return lastFrame;
}

static AVStream* createStream(AVFormatContext * avf)
{
    PARDecContext *p = avf->priv_data;
    ParFrameInfo *fi = (ParFrameInfo *)&p->frameInfo;
    char textbuf[128];
    int w, h;
    int fc = 0;
    
    unsigned long startT, endT;
    AVStream * st = NULL;

    if ((NULL==avf) || (NULL==fi) || (NULL==fi->frameBuffer))
        return NULL;

    st = avformat_new_stream(avf, NULL);
    st->id = fi->channel;

    parReader_getIndexData(fi, NULL, &fc, &startT, &endT);
    
    if (parReader_frameIsVideo(fi))  {
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        
        switch(parReader_getFrameSubType(fi))  {
            case(FRAME_FORMAT_JPEG_422):
            case(FRAME_FORMAT_JPEG_411):
                st->codec->codec_id = CODEC_ID_MJPEG;
                st->codec->has_b_frames = 0;
                break;
            case(FRAME_FORMAT_MPEG4_411):
            case(FRAME_FORMAT_MPEG4_411_I):
            case(FRAME_FORMAT_MPEG4_411_GOV_P):
            case(FRAME_FORMAT_MPEG4_411_GOV_I):
                st->codec->codec_id = CODEC_ID_MPEG4;
                break;
            case(FRAME_FORMAT_RAW_422I):
            case(FRAME_FORMAT_H264_I):
            case(FRAME_FORMAT_H264_P):
                st->codec->codec_id = CODEC_ID_H264;
                break;
            case(FRAME_FORMAT_PBM):
                st->codec->codec_id = CODEC_ID_PBM;
                break;
            default:
                // Set unknown types to data so we don't try and play them
                st->codec->codec_type = AVMEDIA_TYPE_DATA;
                st->codec->codec_id = CODEC_ID_NONE;
                break;
        }
        
        if (AVMEDIA_TYPE_VIDEO == st->codec->codec_type)  {
            if (CODEC_ID_PBM != st->codec->codec_id)  {
                parReader_getFrameSize(fi, &w, &h);
                st->codec->width = w;
                st->codec->height = h;
                
                // Set pixel aspect ratio, display aspect is (sar * width / height)
                /// \todo Could set better values here by checking resolutions and
                /// assuming PAL/NTSC aspect
                if( (w > 360) && (h < 480) )
                    st->sample_aspect_ratio = (AVRational) { 1, 2 };
                else
                    st->sample_aspect_ratio = (AVRational) { 1, 1 };

                parReader_getStreamName(fi->frameBuffer,
                                        fi->frameBufferSize,
                                        textbuf,
                                        sizeof(textbuf));
                av_dict_set(&st->metadata, "title", textbuf, 0);

                parReader_getStreamDate(fi, textbuf, sizeof(textbuf));
                av_dict_set(&st->metadata, "date", textbuf, 0);

                snprintf(textbuf, sizeof(textbuf), "%d", fi->channel & 0xFFFF);
                av_dict_set(&st->metadata, "track", textbuf, 0);
                
                av_dict_set(&st->metadata, "type", "camera", 0);
            }
            else  {
                av_dict_set(&st->metadata, "type", "mask", 0);
            }
        }
        
        // Set timebase to 1 millisecond, and min frame rate to 1 / timebase
        avpriv_set_pts_info(st, 32, 1, 1000);
        st->r_frame_rate = (AVRational) { 1, 1 };
        st->start_time   = fi->imageTime * 1000LL + fi->imageMS;
        st->duration     = getLastFrameTime(fc, fi, &p->dispSet) - st->start_time;
    }
    else if (parReader_frameIsAudio(fi))  {
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->channels = 1;
        st->codec->block_align = 0;
        st->start_time = fi->imageTime * 1000LL + fi->imageMS;
        st->duration = getLastFrameTime(fc, fi, &p->dispSet) - st->start_time;

        switch(parReader_getFrameSubType(fi))  {
            case(FRAME_FORMAT_AUD_ADPCM_8000):
                st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
                st->codec->bits_per_coded_sample = 4;
                st->codec->sample_rate = 8000;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_16000):
                st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
                st->codec->bits_per_coded_sample = 4;
                st->codec->sample_rate = 16000;
                break;
            case(FRAME_FORMAT_AUD_L16_44100):
                st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
                st->codec->bits_per_coded_sample = 4;
                st->codec->sample_rate = 441000;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_11025):
                st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
                st->codec->bits_per_coded_sample = 4;
                st->codec->sample_rate = 11025;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_22050):
                st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
                st->codec->bits_per_coded_sample = 4;
                st->codec->sample_rate = 22050;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_32000):
                st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
                st->codec->bits_per_coded_sample = 4;
                st->codec->sample_rate = 32000;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_44100):
                st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
                st->codec->bits_per_coded_sample = 4;
                st->codec->sample_rate = 44100;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_48000):
                st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
                st->codec->bits_per_coded_sample = 4;
                st->codec->sample_rate = 48000;
                break;
            case(FRAME_FORMAT_AUD_L16_8000):
                st->codec->codec_id = CODEC_ID_PCM_S16LE;
                st->codec->bits_per_coded_sample = 16;
                st->codec->sample_rate = 8000;
                break;
            case(FRAME_FORMAT_AUD_L16_11025):
                st->codec->codec_id = CODEC_ID_PCM_S16LE;
                st->codec->bits_per_coded_sample = 16;
                st->codec->sample_rate = 11025;
                break;
            case(FRAME_FORMAT_AUD_L16_16000):
                st->codec->codec_id = CODEC_ID_PCM_S16LE;
                st->codec->bits_per_coded_sample = 16;
                st->codec->sample_rate = 16000;
                break;
            case(FRAME_FORMAT_AUD_L16_22050):
                st->codec->codec_id = CODEC_ID_PCM_S16LE;
                st->codec->bits_per_coded_sample = 16;
                st->codec->sample_rate = 22050;
                break;
            case(FRAME_FORMAT_AUD_L16_32000):
                st->codec->codec_id = CODEC_ID_PCM_S16LE;
                st->codec->bits_per_coded_sample = 16;
                st->codec->sample_rate = 32000;
                break;
            case(FRAME_FORMAT_AUD_L16_48000):
                st->codec->codec_id = CODEC_ID_PCM_S16LE;
                st->codec->bits_per_coded_sample = 16;
                st->codec->sample_rate = 48000;
                break;
            case(FRAME_FORMAT_AUD_L16_12000):
                st->codec->codec_id = CODEC_ID_PCM_S16LE;
                st->codec->bits_per_coded_sample = 16;
                st->codec->sample_rate = 12000;
                break;
            case(FRAME_FORMAT_AUD_L16_24000):
                st->codec->codec_id = CODEC_ID_PCM_S16LE;
                st->codec->bits_per_coded_sample = 16;
                st->codec->sample_rate = 24000;
                break;
            default:
                st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
                st->codec->sample_rate = 8000;
                break;
        }
        avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
    }
    else  {
        st->codec->codec_type = AVMEDIA_TYPE_DATA;
        
        // Set timebase to 1 millisecond, and min frame rate to 1 / timebase
        avpriv_set_pts_info(st, 32, 1, 1000);
        st->r_frame_rate = (AVRational) { 1, 1 };

        st->start_time = startT * 1000LL;
        st->duration   = getLastFrameTime(fc, fi, &p->dispSet) - st->start_time;
    }

    return st;
}

static int createPacket(AVFormatContext *avf, AVPacket *pkt, int siz)
{
    PARDecContext *ctxt = avf->priv_data;
    ParFrameInfo *fi = &ctxt->frameInfo;
    int id = fi->channel;
    int ii;
    AVStream *st = NULL;
    
#if defined(AD_SIDEDATA_IN_PRIV)
    LibparFrameExtra *pktExt = NULL;
    ParFrameInfo *pktFI = NULL;
#elif defined(AD_SIDEDATA)
    int adDataSize = 0;
    uint8_t *sideData = NULL;
    int textBufSize = 0;
#endif

    for(ii = 0; ii < avf->nb_streams; ii++)  {
        if ( (NULL != avf->streams[ii]) && (avf->streams[ii]->id == id) )  {
            st = avf->streams[ii];
            break;
        }
    }
    if (NULL == st)  {
        st = createStream(avf);
        if (st == NULL)
            return -1;
    }
    
    
    // If frame is MPEG4 video and is the first sent then add a VOL header to it
    if (st->codec->codec_id == CODEC_ID_MPEG4)  {
        if ((ctxt->seqStartAdded & (1<<st->index)) == 0)  {
            if (parReader_isIFrame(fi)) {
                if (parReader_add_start_of_sequence(fi->frameBuffer))
                    ctxt->seqStartAdded |= 1 << st->index;
            }
        }
    }
    
    if (st->codec->codec_id == CODEC_ID_PBM)  {
        char *comment = NULL;
        int w, h;
        uint8_t *pbm = av_malloc(fi->size);
        memcpy(pbm, fi->frameData, fi->size);
        pkt->size = ad_pbmDecompress(&comment, &pbm, fi->size, pkt, &w, &h);
        if (pkt->size > 0)  {            
            st->codec->width = w;
            st->codec->height = h;
        }
        if (comment)  {
            int camera = parReader_getCamera(fi, NULL, 0);
            char name[128];
            snprintf(name, sizeof(name), "Camera %u: %s", camera, comment);
            av_dict_set(&st->metadata, "title", name, 0);
            av_free(comment);
        }
    }
    else  {
        if ( ((uint8_t*)fi->frameData + siz) < ((uint8_t*)fi->frameBuffer + MAX_FRAMEBUFFER_SIZE) )  {
            av_new_packet(pkt, siz);

            if (NULL == pkt->data)  {
                pkt->size = 0;
                return AVERROR(ENOMEM);
            }
            memcpy(pkt->data, fi->frameData, siz);
        }
        else  {
            av_log(avf, AV_LOG_ERROR, "Copying %d bytes would read beyond framebuffer end", siz);
            return AVERROR(ENOMEM);
        }
    }
    pkt->stream_index = st->index;

    if (parReader_frameIsAudio(fi))  {
        if (st->codec->codec_id == CODEC_ID_ADPCM_IMA_WAV)
            endianSwapAudioData(pkt->data, siz);
    }
    else if (parReader_frameIsVideo(fi))  {
        if (parReader_isIFrame(fi))
            pkt->flags |= AV_PKT_FLAG_KEY;
    }

    if (fi->imageTime > 0)  {
        pkt->pts = fi->imageTime;
        pkt->pts *= 1000ULL;
        pkt->pts += fi->imageMS;
    }
    else if (fi->indexTime > 0)  {
        pkt->pts = fi->indexTime;
        pkt->pts *= 1000ULL;
        pkt->pts += fi->indexMS;
    }
    else  {
        pkt->pts = AV_NOPTS_VALUE;
    }
    pkt->dts = pkt->pts;
    pkt->duration = 1;
    
#if defined(AD_SIDEDATA_IN_PRIV)
    pkt->destruct = libpar_packet_destroy;
    pktExt = av_malloc(sizeof(LibparFrameExtra));
    if (NULL == pktExt)  {
        pkt->size = 0;
        return AVERROR(ENOMEM);
    }
    pktExt->fileChanged = ctxt->fileChanged;
    pktExt->indexInfoCount = parReader_getIndexInfo(fi, &pktExt->indexInfo);
    
    pktExt->frameInfo = av_mallocz(sizeof(ParFrameInfo));
    if (NULL == pktExt->frameInfo)  {
        pkt->size = 0;
        return AVERROR(ENOMEM);
    }
    pktFI = pktExt->frameInfo;
    if (parReader_frameIsVideo(fi))
        pktFI->frameBufferSize = parReader_getPicStructSize();
    else if (parReader_frameIsAudio(fi))
        pktFI->frameBufferSize = parReader_getAudStructSize();

    if (pktFI->frameBufferSize > 0)  {
        // Make a copy of the ParFrameInfo struct and the frame header
        // for use by client code that knows this is here

        // Save frameBufferSize as it's about to be overwritten by memcpy
        int fbs = pktFI->frameBufferSize;
        memcpy(pktFI, fi, sizeof(ParFrameInfo));
        pktFI->frameBufferSize = fbs;
        pktFI->frameBuffer = av_malloc(fbs);
        if (NULL == pktFI->frameBuffer)  {
            pkt->size = 0;
            return AVERROR(ENOMEM);
        }
        memcpy(pktFI->frameBuffer, fi->frameBuffer, fbs);
        pktFI->frameData = NULL;
    }
    
    pkt->priv = pktExt;
#elif defined(AD_SIDEDATA)
    if (parReader_frameIsVideo(fi))
        adDataSize = parReader_getPicStructSize();
    else if (parReader_frameIsAudio(fi))
        adDataSize = parReader_getAudStructSize();
    if (adDataSize > 0)  {
        sideData = av_packet_new_side_data(pkt, AV_PKT_DATA_AD_FRAME, adDataSize);
        if (sideData)
            memcpy(sideData, fi->frameBuffer, adDataSize);
    }
            
    if (fi->frameText)  {
        textBufSize = strlen(fi->frameText) + 1;
        sideData = av_packet_new_side_data(pkt, AV_PKT_DATA_AD_TEXT, textBufSize);
        if (sideData)
            memcpy(sideData, fi->frameText, textBufSize);
    }
    
    if (ctxt->fileChanged)  {
        int fc = 0;
        unsigned long startT, endT, lastFT;
    
        parReader_getIndexData(fi, NULL, &fc, &startT, &endT);
        lastFT = getLastFrameTime(fc, fi, &ctxt->dispSet);
        for (ii = 0; ii < avf->nb_streams; ii++)  {
            st->start_time = fi->imageTime * 1000LL + fi->imageMS;
            st->duration = lastFT - st->start_time;
        }
        
        sideData = av_packet_new_side_data(pkt, AV_PKT_DATA_AD_PARINF, sizeof(ctxt->frameInfo));
        if (sideData)
            memcpy(sideData, &(ctxt->frameInfo), sizeof(ctxt->frameInfo));
        ctxt->fileChanged = 0;
    }
#endif

    ctxt->frameCached = 0;

    return 0;
}

    
static int par_probe(AVProbeData *p)
{
    unsigned long first4;
    if (p->buf_size < 4)
        return 0;

    first4 = *((unsigned long *)p->buf);
    first4 = av_le2ne32(first4);
    if (first4 == 0x00524150)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int par_read_header(AVFormatContext * avf)
{
    int res, siz;
    PARDecContext *p = avf->priv_data;
    char **filelist;
    int seqLen;
    int64_t seconds = 0;
    AVStream *strm = NULL;
    char textbuf[128];


    if (parReader_version(textbuf, sizeof(textbuf)) > 0)  {
        av_log(avf, AV_LOG_INFO, "ParReader library version: %s\n", textbuf);
        av_dict_set(&avf->metadata, "ParReader", textbuf, 0);
    }

    parReader_initFrameInfo(&p->frameInfo, MAX_FRAMEBUFFER_SIZE, av_malloc(MAX_FRAMEBUFFER_SIZE));
    if (p->frameInfo.frameBuffer == NULL)
        return AVERROR(ENOMEM);

    switch(av_log_get_level())  {
        case(AV_LOG_QUIET):
            parReader_setLogLevel(-1);
            break;
        case(AV_LOG_PANIC):
            parReader_setLogLevel(PARREADER_LOG_CRITICAL);
            break;
        case(AV_LOG_FATAL):
            parReader_setLogLevel(PARREADER_LOG_CRITICAL);
            break;
        case(AV_LOG_ERROR):
            parReader_setLogLevel(PARREADER_LOG_ERROR);
            break;
        case(AV_LOG_WARNING):
            parReader_setLogLevel(PARREADER_LOG_WARNING);
            break;
        case(AV_LOG_INFO):
            parReader_setLogLevel(PARREADER_LOG_INFO);
            break;
        case(AV_LOG_VERBOSE):
            parReader_setLogLevel(PARREADER_LOG_INFO);
            break;
        case(AV_LOG_DEBUG):
            parReader_setLogLevel(PARREADER_LOG_DEBUG);
            break;
    }
    parReader_setLogCallback(parreaderLogger);

    parReader_setDisplaySettingsDefaults(&p->dispSet);

    res = parReader_loadParFile(NULL, avf->filename, -1, &p->frameInfo, 0);
    if (0 == res)
        return AVERROR(EIO);

    seqLen = parReader_getFilelist(&p->frameInfo, &filelist);
    if (1 == seqLen)  {
        int frameNumber, frameCount;
        unsigned long start, end;
        if (parReader_getIndexData(&p->frameInfo, &frameNumber, &frameCount, &start, &end))
            seconds = end - start;
    }
    else  {
        int res, frameNumber, frameCount;
        unsigned long start, end, realStart, realEnd;
        if (parReader_getIndexData(&p->frameInfo, &frameNumber, &frameCount, &start, &end))  {
            realStart = start;
            av_log(avf, AV_LOG_DEBUG, "par_read_header:  %s (%d)\n", filelist[0], seqLen - 1);
            res = parReader_loadParFile(NULL, filelist[0], seqLen - 1, &p->frameInfo, 0);
            if (res && parReader_getIndexData(&p->frameInfo, &frameNumber, &frameCount, &start, &end))  {
                realEnd = end;
                seconds = realEnd - realStart;
            }
            av_log(avf, AV_LOG_DEBUG, "par_read_header:  %s (%d)\n", filelist[0], res);
            res = parReader_loadParFile(NULL, filelist[0], -1, &p->frameInfo, 0);
        }
    }

    siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &p->fileChanged);
    
    p->frameCached = siz;
    p->fileChanged = 1;
    p->seqStartAdded = 0;

    snprintf(textbuf, sizeof(textbuf), "%d", parReader_getUTCOffset(&p->frameInfo));
    av_dict_set(&avf->metadata, "timezone", textbuf, 0);
    
    strm = createStream(avf);
//    if (strm)  {
//        // Note: Do not set avf->start_time, ffmpeg computes it from AVStream values
//        avf->duration = av_rescale_q(seconds, secondsTB, strm->time_base);
//    }

    avf->ctx_flags |= AVFMTCTX_NOHEADER;

    return 0;
}

static int par_read_packet(AVFormatContext * avf, AVPacket * pkt)
{
    PARDecContext *p = avf->priv_data;
    int siz = 0;

    if (p->frameCached)  {
        siz = p->frameCached;
    }
    else
        siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &p->fileChanged);

    if (siz < 0)  {
        p->frameCached = 0;
        p->fileChanged = 0;
        pkt->size = 0;
        return AVERROR_EOF;
    }

    if (p->fileChanged)
        parReader_getFilename(&p->frameInfo, avf->filename, sizeof(avf->filename));

    if ( (siz == 0) || (NULL == p->frameInfo.frameData) )  {
        p->frameCached = 0;
        return AVERROR(EAGAIN);
    }
    
    return createPacket(avf, pkt, siz);
}

static int par_read_seek(AVFormatContext *avf, int stream,
                         int64_t target, int flags)
{
    PARDecContext *p = avf->priv_data;
    int siz = 0;
    int streamId = 0;
    int isKeyFrame = 0;
    int step;
    int anyStreamWillDo = 0;
    int prevPlayMode, prevLock;

    av_log(avf, AV_LOG_DEBUG, "par_read_seek target    = %"PRId64"\n", target);
    
    if ((stream < 0) || (stream >= avf->nb_streams))  {
        anyStreamWillDo = 1;
        streamId = avf->streams[0]->id;
    }
    else
        streamId = avf->streams[stream]->id;
    
    prevPlayMode = p->dispSet.playMode;
    prevLock = p->dispSet.fileLock;
    
    p->seqStartAdded = 0;
    p->dispSet.cameraNum = streamId;
    if (flags & AVSEEK_FLAG_BACKWARD)
        p->dispSet.playMode = RWND;

    if ( (flags & AVSEEK_FLAG_FRAME) && (target < 0) )   {
        p->dispSet.fileSeqNo = (-target) - 1;
        p->dispSet.frameNumber = 0;
    }
    else  {
        p->dispSet.fileSeqNo = -1;
        
        if (flags & AVSEEK_FLAG_FRAME)  {
            // Don't seek beyond the file
            p->dispSet.fileLock = 1;
            p->dispSet.frameNumber = target;
        }
        else  {
            p->dispSet.timestamp = target / 1000LL;
            p->dispSet.millisecs = target % 1000;
        }
    }

    do  {
        siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &p->fileChanged);

        // If this frame is not acceptable we want to just iterate through
        // until we find one that is, not seek again, so reset targets
        p->dispSet.frameNumber = -1;
        p->dispSet.timestamp = 0;
        p->dispSet.millisecs = 0;

        if (siz < 0)
            break;

        if (parReader_frameIsVideo(&p->frameInfo))  {
            if (flags & AVSEEK_FLAG_ANY)
                isKeyFrame = 1;
            else
                isKeyFrame = parReader_isIFrame(&p->frameInfo);
        }
        else  {
            // Always seek to a video frame
            isKeyFrame = 0;
        }
        
        // If we don't care which stream then force the streamId to match
        if (anyStreamWillDo)
            streamId = p->frameInfo.channel;
    }
    while ( (streamId != p->frameInfo.channel) || (0 == isKeyFrame) );

    p->dispSet.fileLock = prevLock;
    p->dispSet.playMode = prevPlayMode;

    if (siz > 0)  {
        p->frameCached = siz;
        for(step = 0; step < avf->nb_streams; step++)  {
            if ( (NULL != avf->streams[step]) && (avf->streams[step]->id == streamId) )  {
                avf->streams[step]->codec->frame_number = p->frameInfo.frameNumber;
                break;
            }
        }
        av_log(avf, AV_LOG_DEBUG, "par_read_seek seek done = %lu\n", p->frameInfo.imageTime);
        return p->frameInfo.imageTime;
    }
    else  {
        av_log(avf, AV_LOG_DEBUG, "par_read_seek seek failed\n");
        return -1;
    }
}

static int par_read_close(AVFormatContext * avf)
{
    PARDecContext *p = avf->priv_data;
    av_log(avf, AV_LOG_DEBUG, "par_read_close");
    av_free(p->frameInfo.frameBuffer);
    parReader_closeParFile(&p->frameInfo);
    return 0;
}


AVOutputFormat ff_libparreader_muxer = {
    .name           = "libpar",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings PAR format"),
    .mime_type      = "video/adhbinary",
    .extensions     = "par",
    .priv_data_size = sizeof(PAREncContext),
    .audio_codec    = CODEC_ID_ADPCM_IMA_WAV,
    .video_codec    = CODEC_ID_MJPEG,
    .write_header   = par_write_header,
    .write_packet   = par_write_packet,
    .write_trailer  = par_write_trailer,
    .flags          = AVFMT_GLOBALHEADER,
};

AVInputFormat ff_libparreader_demuxer = {
    .name           = "libpar",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings PAR format"),
    .priv_data_size = sizeof(PARDecContext),
    .read_probe     = par_probe,
    .read_header    = par_read_header,
    .read_packet    = par_read_packet,
    .read_close     = par_read_close,
    .read_seek      = par_read_seek,
    .flags          = AVFMT_TS_DISCONT | AVFMT_VARIABLE_FPS | AVFMT_NO_BYTE_SEEK,
};
