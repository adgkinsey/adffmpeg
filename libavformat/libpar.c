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
#include "libavutil/intreadwrite.h"
#include "libpar.h"


typedef struct {
    ParDisplaySettings dispSet;
    ParFrameInfo frameInfo;
    int fileChanged;
    int frameCached;
} PARDecContext;

typedef struct {
    char name[64];
    int camera;
    int64_t startTime;
    int utc_offset;
} PAREncStreamContext;

typedef struct {
    ParFrameInfo frameInfo;
    PAREncStreamContext master;
    PAREncStreamContext stream[MAX_STREAMS];
    int picHeaderSize;
} PAREncContext;


void libpar_packet_destroy(struct AVPacket *packet);
AVStream* createStream(AVFormatContext * avf,
                       const ParFrameInfo *frameInfo);
void createPacket(AVFormatContext * avf, AVPacket *packet, int siz, int fChang);
void importMetadata(const AVMetadataTag *tag, PAREncStreamContext *ps);

static void parreaderLogger(int level, const char *format, va_list args);


const unsigned int MAX_FRAMEBUFFER_SIZE = 256 * 1024;


void importMetadata(const AVMetadataTag *tag, PAREncStreamContext *ps)
{
    struct tm timeval = {0};

    if (strcasecmp(tag->key, "title") == 0)
        strncpy(ps->name, tag->value, sizeof(ps->name));
    else if (strcasecmp(tag->key, "date") == 0)  {
        small_strptime(tag->value, "%Y-%m-%d %H:%M", &timeval);
        ps->startTime = mktime(&timeval);
        ps->startTime *= 1000;
    }
    else if (strcasecmp(tag->key, "track") == 0)
        sscanf(tag->value, "%d", &(ps->camera));
    else if (strcasecmp(tag->key, "timezone") == 0)
        sscanf(tag->value, "%d", &(ps->utc_offset));
}

static int par_write_header(AVFormatContext *avf)
{
    PAREncContext *p = avf->priv_data;
    AVMetadataTag *tag = NULL;
    int ii;

    p->picHeaderSize = parReader_getPicStructSize();
    do  {
        tag = av_metadata_get(avf->metadata, "", tag, AV_METADATA_IGNORE_SUFFIX);
        if (tag)
            importMetadata(tag, &(p->master));
    }
    while (tag);
    
    for (ii = 0; ii < avf->nb_streams; ii++)  {
        AVStream *st = avf->streams[ii];
        // Set timebase to 1 millisecond, and min frame rate to 1 / timebase
        av_set_pts_info(st, 32, 1, 1000);
        st->r_frame_rate = (AVRational) { 1, 1 };
    }

    p->frameInfo.parData = parReader_initWritePartition(avf->filename, -1);

    if (p->frameInfo.parData)
        return 0;
    else
        return AVERROR(EIO);
}

static int par_write_packet(AVFormatContext *avf, AVPacket * pkt)
{
    PAREncContext *p = avf->priv_data;
    PAREncStreamContext *ps = &(p->stream[pkt->stream_index]);
    AVMetadataTag *tag = NULL;
    int64_t parTime;
    AVStream *stream = avf->streams[pkt->stream_index];
    void *hdr;
    uint8_t *ptr;
    int parFrameFormat;
    int64_t srcTime = pkt->pts;
    int written = 0;

    // Metadata
    if (ps->camera < 1)  {
        // Copy over the values from the file data first
        *ps = p->master;

        // Now check if there are stream-specific values
        do  {
            tag = av_metadata_get(stream->metadata, "", tag, AV_METADATA_IGNORE_SUFFIX);
            if (tag)
                importMetadata(tag, ps);
        }
        while (tag);

        if (ps->camera < 1)
            ps->camera = pkt->stream_index + 1;
        if (strlen(ps->name) == 0)
            snprintf(ps->name, sizeof(ps->name), "Camera %d", ps->camera);
    }
    
    uint32_t pktTypeCheck = AV_RL32(pkt->data);
    if (pktTypeCheck == 0xDECADE11)  {
        p->frameInfo.frameBufferSize = pkt->size;
        p->frameInfo.frameBuffer = pkt->data;
        written = parReader_writePartition(p->frameInfo.parData, &p->frameInfo);
        return written;
    }
    else  {
        // PAR files have timestamps that are in UTC, not elapsed time
        // So if the pts we have been given is the latter we need to
        // add an offset to convert it to the former
        if (srcTime < 0)
            srcTime = pkt->dts;
        if ( (srcTime == 0) && (ps->startTime == 0) )  {
            if (avf->start_time_realtime > 0)
                ps->startTime = avf->start_time_realtime / 1000;
            else if (avf->timestamp > 0)
                ps->startTime = avf->timestamp * 1000;
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
        p->frameInfo.frameBuffer = ptr;
        memcpy(ptr, hdr, p->picHeaderSize);
        memcpy(ptr + p->picHeaderSize, pkt->data, pkt->size);
        written = parReader_writePartition(p->frameInfo.parData, &p->frameInfo);
        av_free(ptr);
        parReader_freePicHeader(hdr);

        return written;
    }
}

static int par_write_trailer(AVFormatContext *avf)
{
    PAREncContext *p = avf->priv_data;
    parReader_closeWritePartition(p->frameInfo.parData);
    return 0;
}

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

AVStream* createStream(AVFormatContext * avf,
                       const ParFrameInfo *frameInfo)
{
    char textbuffer[128];
    int w, h;
    int streamId = -1;
    int fc = 0;
    AVStream * st = NULL;

    if ((NULL==avf) || (NULL==frameInfo) || (NULL==frameInfo->frameBuffer))
        return NULL;

    streamId = frameInfo->channel;
    st = av_new_stream(avf, streamId);

    parReader_getIndexData(frameInfo, NULL, &fc, NULL, NULL);
    st->filename = av_strdup(avf->filename);
    st->nb_frames = fc;
    st->start_time = frameInfo->indexTime * 1000LL + frameInfo->indexMS;

    if (parReader_frameIsVideo(frameInfo))  {
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;

        switch(getVideoFrameSubType(frameInfo))  {
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
            default:
                // Set unknown types to data so we don't try and play them
                st->codec->codec_type = AVMEDIA_TYPE_DATA;
                st->codec->codec_id = CODEC_ID_NONE;
                break;
        }

        if (AVMEDIA_TYPE_VIDEO == st->codec->codec_type)  {
            parReader_getFrameSize(frameInfo, &w, &h);
            st->codec->width = w;
            st->codec->height = h;
            st->start_time = frameInfo->imageTime * 1000LL + frameInfo->imageMS;

            // Set timebase to 1 millisecond, and min frame rate to 1 / timebase
            av_set_pts_info(st, 32, 1, 1000);
            st->r_frame_rate = (AVRational) { 1, 1 };

            // Set pixel aspect ratio, display aspect is (sar * width / height)
            /// \todo Could set better values here by checking resolutions and
            /// assuming PAL/NTSC aspect
            if( (w > 360) && (h <= 480) )
                st->sample_aspect_ratio = (AVRational) { 1, 2 };
            else
                st->sample_aspect_ratio = (AVRational) { 1, 1 };

            parReader_getStreamName(frameInfo->frameBuffer,
                                    frameInfo->frameBufferSize,
                                    textbuffer,
                                    sizeof(textbuffer));
            av_metadata_set2(&st->metadata, "title", textbuffer, 0);

            parReader_getStreamDate(frameInfo, textbuffer, sizeof(textbuffer));
            av_metadata_set2(&st->metadata, "date", textbuffer, 0);

            snprintf(textbuffer, sizeof(textbuffer), "%d", frameInfo->channel);
            av_metadata_set2(&st->metadata, "track", textbuffer, 0);
        }
    }
    else if (parReader_frameIsAudio(frameInfo))  {
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id = CODEC_ID_ADPCM_ADH;
        st->codec->channels = 1;
        st->codec->block_align = 0;
        st->start_time = frameInfo->imageTime * 1000 + frameInfo->imageMS;

        switch(getAudioFrameSubType(frameInfo))  {
            case(FRAME_FORMAT_AUD_ADPCM_8000):
                st->codec->sample_rate = 8000;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_16000):
                st->codec->sample_rate = 16000;
                break;
            case(FRAME_FORMAT_AUD_L16_44100):
                st->codec->sample_rate = 441000;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_11025):
                st->codec->sample_rate = 11025;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_22050):
                st->codec->sample_rate = 22050;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_32000):
                st->codec->sample_rate = 32000;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_44100):
                st->codec->sample_rate = 44100;
                break;
            case(FRAME_FORMAT_AUD_ADPCM_48000):
                st->codec->sample_rate = 48000;
                break;
            case(FRAME_FORMAT_AUD_L16_8000):
                st->codec->sample_rate = 8000;
                break;
            case(FRAME_FORMAT_AUD_L16_11025):
                st->codec->sample_rate = 11025;
                break;
            case(FRAME_FORMAT_AUD_L16_16000):
                st->codec->sample_rate = 16000;
                break;
            case(FRAME_FORMAT_AUD_L16_22050):
                st->codec->sample_rate = 22050;
                break;
            case(FRAME_FORMAT_AUD_L16_32000):
                st->codec->sample_rate = 32000;
                break;
            case(FRAME_FORMAT_AUD_L16_48000):
                st->codec->sample_rate = 48000;
                break;
            case(FRAME_FORMAT_AUD_L16_12000):
                st->codec->sample_rate = 12000;
                break;
            case(FRAME_FORMAT_AUD_L16_24000):
                st->codec->sample_rate = 24000;
                break;
            default:
                st->codec->sample_rate = 8000;
                break;
        }
        st->codec->codec_tag = 0x0012;
    }
    else  {
        st->codec->codec_type = AVMEDIA_TYPE_DATA;
    }

    // Don't set the st->duration since we don't really know it, so we use
    // avf->duration

    return st;
}

void createPacket(AVFormatContext * avf, AVPacket *pkt, int siz, int fChang)
{
    PARDecContext *ctxt = avf->priv_data;
    ParFrameInfo *fi = &ctxt->frameInfo;
    LibparFrameExtra *pktExt = NULL;
    ParFrameInfo *pktFI = NULL;

    int streamIndex = -1;
    int id = fi->channel;
    for(int ii = 0; ii < avf->nb_streams; ii++)  {
        if ( (NULL != avf->streams[ii]) && (avf->streams[ii]->id == id) )  {
            streamIndex = ii;
            break;
        }
    }
    if (-1 == streamIndex)  {
        AVStream *str = createStream(avf, fi);
        if (str)
            streamIndex = str->index;
        else
            return;
    }
    av_new_packet(pkt, siz);

    if (NULL == pkt->data)  {
        pkt->size = 0;
        return;
    }

    pkt->destruct = libpar_packet_destroy;

    pkt->stream_index = streamIndex;

    pktExt = av_malloc(sizeof(LibparFrameExtra));
    if (NULL == pktExt)  {
        pkt->size = 0;
        return;
    }
    pkt->priv = pktExt;

    pktExt->fileChanged = fChang;

    pktExt->indexInfoCount = parReader_getIndexInfo(fi, &pktExt->indexInfo);

    memcpy(pkt->data, fi->frameData, siz);

    pktExt->frameInfo = av_mallocz(sizeof(ParFrameInfo));
    if (NULL == pktExt->frameInfo)  {
        pkt->size = 0;
        return;
    }
    pktFI = pktExt->frameInfo;

    if (parReader_frameIsVideo(fi))  {
        pktFI->frameBufferSize = parReader_getPicStructSize();
        if (parReader_isIFrame(fi))
            pkt->flags |= AV_PKT_FLAG_KEY;
    }
    else if (parReader_frameIsAudio(fi))  {
        pktFI->frameBufferSize = parReader_getAudStructSize();
    }
    else  {
        /// \todo Do something with data frames
    }

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
            return;
        }
        memcpy(pktFI->frameBuffer, fi->frameBuffer, fbs);
        pktFI->frameData = NULL;
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

    ctxt->frameCached = 0;
}

static int par_probe(AVProbeData *p)
{
    unsigned long first4;
    if (p->buf_size < 4)
        return 0;

    first4 = *((unsigned long *)p->buf);
    first4 = le2me_32(first4);
    if (first4 == 0x00524150)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int par_read_header(AVFormatContext * avf, AVFormatParameters * ap)
{
    int res, siz;
    PARDecContext *p = avf->priv_data;
    char **filelist;
    int seqLen;
    int64_t seconds = 0;
    AVStream *strm = NULL;
    AVRational secondsTB = {1, 1};
    char libVer[128], textbuf[5];


    parReader_version(libVer, sizeof(libVer));
    av_log(avf, AV_LOG_INFO, "ParReader library version: %s\n", libVer);

    p->frameInfo.frameBufferSize = MAX_FRAMEBUFFER_SIZE;
    p->frameInfo.frameBuffer = av_malloc(p->frameInfo.frameBufferSize);

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
            av_log(NULL, AV_LOG_DEBUG, "par_read_header:  %s (%d)\n", filelist[0], seqLen - 1);
            res = parReader_loadParFile(NULL, filelist[0], seqLen - 1, &p->frameInfo, 0);
            if (parReader_getIndexData(&p->frameInfo, &frameNumber, &frameCount, &start, &end))  {
                realEnd = end;
                seconds = realEnd - realStart;
            }
            av_log(NULL, AV_LOG_DEBUG, "par_read_header:  %s (%d)\n", filelist[0], -1);
            res = parReader_loadParFile(NULL, filelist[0], -1, &p->frameInfo, 0);
        }
    }

    siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &p->fileChanged);
    p->frameCached = siz;

    // Reading the header opens the file, so ignore this file change notifier
    p->fileChanged = 0;

    snprintf(textbuf, sizeof(textbuf), "%d", parReader_getUTCOffset(&p->frameInfo));
    av_metadata_set2(&avf->metadata, "timezone", textbuf, 0);
    
    strm = createStream(avf, &p->frameInfo);
    if (strm)  {
        // Note: Do not set avf->start_time, ffmpeg computes it from AVStream values
        avf->duration = av_rescale_q(seconds, secondsTB, strm->time_base);
    }

    avf->ctx_flags |= AVFMTCTX_NOHEADER;

    return 0;
}

static int par_read_packet(AVFormatContext * avf, AVPacket * pkt)
{
    PARDecContext *p = avf->priv_data;

    int siz = 0;
    int fileChanged = 0;
    if (p->frameCached)  {
        siz = p->frameCached;
        fileChanged = p->fileChanged;
    }
    else
        siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &fileChanged);

    if (siz < 0)  {
        p->frameCached = 0;
        p->fileChanged = 0;
        pkt->size = 0;
        return AVERROR_EOF;
    }

    if (fileChanged)
        parReader_getFilename(&p->frameInfo, avf->filename, sizeof(avf->filename));

    if ( (siz == 0) || (NULL == p->frameInfo.frameData) )  {
        p->frameCached = 0;
        return AVERROR(EAGAIN);
    }

    createPacket(avf, pkt, siz, fileChanged);

    return 0;
}

static int par_read_seek(AVFormatContext *avf, int stream,
                         int64_t target, int flags)
{
    PARDecContext *p = avf->priv_data;
    int siz = 0;
    AVStream *st = avf->streams[stream];
    int streamId = st->id;
    int isKeyFrame = 0;
    int step;

    av_log(NULL, AV_LOG_DEBUG, "par_read_seek target    = %"PRId64"\n", target);

    p->dispSet.cameraNum = streamId;
    if (flags & AVSEEK_FLAG_BACKWARD)
        p->dispSet.playMode = RWND;

    do  {
        if ( (flags & AVSEEK_FLAG_FRAME) && (target < 0) )   {
            p->dispSet.fileSeqNo = (-target) - 1;
            p->dispSet.frameNumber = 0;
        }
        else  {
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
        }
        while ( (streamId != p->frameInfo.channel) || (0 == isKeyFrame) );

        if (siz <= 0)  {
            // If we have failed to load a frame then try again with (target - 1)
            if (target >= 0)  {
                if (flags & AVSEEK_FLAG_FRAME)
                    step = 1;
                else
                    step = 1000;

                if (RWND == p->dispSet.playMode)
                    target = target + step;
                else
                    target = target - step;
            }
            else  {
                // Jumping to file failed, should never happen
                break;
            }
        }
        else  {
            p->frameCached = siz;
            st->codec->frame_number = p->frameInfo.frameNumber;
        }
    }
    while (siz <= 0);

    p->dispSet.fileLock = 0;
    p->dispSet.playMode = PLAY;

    av_log(NULL, AV_LOG_DEBUG, "par_read_seek seek_done = %lu\n", p->frameInfo.imageTime);

    return p->frameInfo.imageTime;
}

static int par_read_close(AVFormatContext * avf)
{
    PARDecContext *p = avf->priv_data;
    av_free(p->frameInfo.frameBuffer);
    return 0;
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

AVOutputFormat libparreader_muxer = {
    .name           = "libpar",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings PAR format"),
    .mime_type      = "video/adhbinary",
    .extensions     = "par",
    .priv_data_size = sizeof(PAREncContext),
    .audio_codec    = CODEC_ID_ADPCM_ADH,
    .video_codec    = CODEC_ID_MJPEG,
    .write_header   = par_write_header,
    .write_packet   = par_write_packet,
    .write_trailer  = par_write_trailer,
    .flags          = AVFMT_GLOBALHEADER,
};

AVInputFormat libparreader_demuxer = {
    .name           = "libpar",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings PAR format"),
    .priv_data_size = sizeof(PARDecContext),
    .read_probe     = par_probe,
    .read_header    = par_read_header,
    .read_packet    = par_read_packet,
    .read_close     = par_read_close,
    .read_seek      = par_read_seek,
};
