/*
 * AD-Holdings common functions for demuxers
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

#include <strings.h>

#include "internal.h"
#include "url.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"

#include "adffmpeg_errors.h"
#include "adpic.h"
#include "adjfif.h"
#include "netvu.h"


static const AVRational MilliTB = {1, 1000};


int ad_read_header(AVFormatContext *s, int *utcOffset)
{
    AVIOContext*    pb          = s->pb;
    URLContext*     urlContext  = pb->opaque;
    NetvuContext*   nv          = NULL;

    if (urlContext && urlContext->is_streamed)  {
        if ( av_stristart(urlContext->filename, "netvu://", NULL) == 1)
            nv = urlContext->priv_data;
    }
    if (nv)  {
        int ii;
        char temp[12];

        if (utcOffset)
            *utcOffset = nv->utc_offset;

        for(ii = 0; ii < NETVU_MAX_HEADERS; ii++)  {
            av_dict_set(&s->metadata, nv->hdrNames[ii], nv->hdrs[ii], 0);
        }
        if ( (nv->utc_offset >= 0) && (nv->utc_offset <= 1440) )  {
            snprintf(temp, sizeof(temp), "%d", nv->utc_offset);
            av_dict_set(&s->metadata, "timezone", temp, 0);
        }
    }

    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return 0;
}

void ad_network2host(struct NetVuImageData *pic, uint8_t *data)
{
    pic->version                = AV_RB32(data + 0);
    pic->mode                   = AV_RB32(data + 4);
    pic->cam                    = AV_RB32(data + 8);
    pic->vid_format             = AV_RB32(data + 12);
    pic->start_offset           = AV_RB32(data + 16);
    pic->size                   = AV_RB32(data + 20);
    pic->max_size               = AV_RB32(data + 24);
    pic->target_size            = AV_RB32(data + 28);
    pic->factor                 = AV_RB32(data + 32);
    pic->alm_bitmask_hi         = AV_RB32(data + 36);
    pic->status                 = AV_RB32(data + 40);
    pic->session_time           = AV_RB32(data + 44);
    pic->milliseconds           = AV_RB32(data + 48);
    if ((uint8_t *)pic != data)  {
        memcpy(pic->res,    data + 52, 4);
        memcpy(pic->title,  data + 56, 31);
        memcpy(pic->alarm,  data + 87, 31);
    }
    pic->format.src_pixels      = AV_RB16(data + 118);
    pic->format.src_lines       = AV_RB16(data + 120);
    pic->format.target_pixels   = AV_RB16(data + 122);
    pic->format.target_lines    = AV_RB16(data + 124);
    pic->format.pixel_offset    = AV_RB16(data + 126);
    pic->format.line_offset     = AV_RB16(data + 128);
    if ((uint8_t *)pic != data)
        memcpy(pic->locale, data + 130, 30);
    pic->utc_offset             = AV_RB32(data + 160);
    pic->alm_bitmask            = AV_RB32(data + 164);
}

static AVStream * netvu_get_stream(AVFormatContext *s, struct NetVuImageData *p)
{
    time_t dateSec;
    char dateStr[18];
    AVStream *stream = ad_get_vstream(s,
                                      p->format.target_pixels,
                                      p->format.target_lines,
                                      p->cam,
                                      p->vid_format,
                                      p->title);
    stream->start_time = p->session_time * 1000LL + p->milliseconds;
    dateSec = p->session_time;
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%MZ", gmtime(&dateSec));
    av_dict_set(&stream->metadata, "date", dateStr, 0);
    return stream;
}

int ad_adFormatToCodecId(AVFormatContext *s, int32_t adFormat)
{
    int codec_id = CODEC_ID_NONE;
    
    switch(adFormat) {
        case PIC_MODE_JPEG_422:
        case PIC_MODE_JPEG_411:
            codec_id = CODEC_ID_MJPEG;
            break;

        case PIC_MODE_MPEG4_411:
        case PIC_MODE_MPEG4_411_I:
        case PIC_MODE_MPEG4_411_GOV_P:
        case PIC_MODE_MPEG4_411_GOV_I:
            codec_id = CODEC_ID_MPEG4;
            break;

        case PIC_MODE_H264I:
        case PIC_MODE_H264P:
        case PIC_MODE_H264J:
            codec_id = CODEC_ID_H264;
            break;

        default:
            av_log(s, AV_LOG_WARNING,
                   "ad_get_stream: unrecognised vid_format %d\n",
                   adFormat);
            codec_id = CODEC_ID_NONE;
    }
    return codec_id;
}

AVStream * ad_get_vstream(AVFormatContext *s, uint16_t w, uint16_t h, uint8_t cam, int32_t format, const char *title)
{
    uint8_t codec_type = 0;
    int codec_id, id;
    int i, found;
    char textbuffer[4];
    AVStream *st;

    codec_id = ad_adFormatToCodecId(s, format);
    if (codec_id == CODEC_ID_MJPEG)
        codec_type = 0;
    else if (codec_id == CODEC_ID_MPEG4)
        codec_type = 1;
    else if (codec_id)
        codec_type = 2;

    id = ((codec_type & 0x0003) << 29) |
         (((cam - 1)  & 0x001F) << 24) |
         (((w >> 4)   & 0x0FFF) << 12) |
         (((h >> 4)   & 0x0FFF) << 0);

    found = FALSE;
    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        if (st->id == id) {
            found = TRUE;
            break;
        }
    }
    if (!found) {
        st = avformat_new_stream(s, NULL);
        if (st) {
            st->id = id;
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codec->codec_id = codec_id;
            st->codec->width = w;
            st->codec->height = h;
            st->index = i;

            // Set pixel aspect ratio, display aspect is (sar * width / height)
            // May get overridden by codec
            if( (st->codec->width > 360) && (st->codec->height < 480) )
                st->sample_aspect_ratio = (AVRational) { 1, 2 };
            else
                st->sample_aspect_ratio = (AVRational) { 1, 1 };

            // Use milliseconds as the time base
            st->r_frame_rate = MilliTB;
            avpriv_set_pts_info(st, 32, MilliTB.num, MilliTB.den);
            st->codec->time_base = MilliTB;

            if (title)
                av_dict_set(&st->metadata, "title", title, 0);
            snprintf(textbuffer, sizeof(textbuffer), "%u", cam);
            av_dict_set(&st->metadata, "track", textbuffer, 0);
            
            av_dict_set(&st->metadata, "type", "camera", 0);
        }
    }
    return st;
}

static unsigned int RSHash(int camera, const char *name, unsigned int len)
{
    unsigned int b    = 378551;
    unsigned int a    = (camera << 16) + (camera << 8) + camera;
    unsigned int hash = 0;
    unsigned int i    = 0;
    
    for(i = 0; i < len; name++, i++)  {
        hash = hash * a + (*name);
        a    = a * b;
    }
    return hash;
}

static AVStream * ad_get_overlay_stream(AVFormatContext *s, int channel, const char *title)
{
    static const int codec_id = CODEC_ID_PBM;
    unsigned int id;
    int i, found;
    AVStream *st;

    id = RSHash(channel+1, title, strlen(title));

    found = FALSE;
    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        if ((st->codec->codec_id == codec_id) && (st->id == id)) {
            found = TRUE;
            break;
        }
    }
    if (!found) {
        st = avformat_new_stream(s, NULL);
        if (st) {
            st->id = id;
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codec->codec_id = codec_id;
            st->index = i;

            // Use milliseconds as the time base
            st->r_frame_rate = MilliTB;
            avpriv_set_pts_info(st, 32, MilliTB.num, MilliTB.den);
            st->codec->time_base = MilliTB;

            av_dict_set(&st->metadata, "title", title, 0);
            av_dict_set(&st->metadata, "type", "mask", 0);
        }
    }
    return st;
}

AVStream * ad_get_audio_stream(AVFormatContext *s, struct NetVuAudioData* audioHeader)
{
    int i, found;
    AVStream *st;

    found = FALSE;
    for( i = 0; i < s->nb_streams; i++ ) {
        st = s->streams[i];
        if ( (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) && (st->id == audioHeader->channel) ) {
            found = TRUE;
            break;
        }
    }

    // Did we find our audio stream? If not, create a new one
    if( !found ) {
        st = avformat_new_stream(s, NULL);
        if (st) {
            st->id = audioHeader->channel;
            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            st->codec->channels = 1;
            st->codec->block_align = 0;

            if (audioHeader)  {
                switch(audioHeader->mode)  {
                    default:
                    case(RTP_PAYLOAD_TYPE_8000HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_16000HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_44100HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_11025HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_22050HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_32000HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_48000HZ_ADPCM):
                        st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
                        st->codec->bits_per_coded_sample = 4;
                        break;
                    case(RTP_PAYLOAD_TYPE_8000HZ_PCM):
                    case(RTP_PAYLOAD_TYPE_16000HZ_PCM):
                    case(RTP_PAYLOAD_TYPE_44100HZ_PCM):
                    case(RTP_PAYLOAD_TYPE_11025HZ_PCM):
                    case(RTP_PAYLOAD_TYPE_22050HZ_PCM):
                    case(RTP_PAYLOAD_TYPE_32000HZ_PCM):
                    case(RTP_PAYLOAD_TYPE_48000HZ_PCM):
                        st->codec->codec_id = CODEC_ID_PCM_S16LE;
                        st->codec->bits_per_coded_sample = 16;
                        break;
                }
                switch(audioHeader->mode)  {
                    default:
                    case(RTP_PAYLOAD_TYPE_8000HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_8000HZ_PCM):
                        st->codec->sample_rate = 8000;
                        break;
                    case(RTP_PAYLOAD_TYPE_16000HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_16000HZ_PCM):
                        st->codec->sample_rate = 16000;
                        break;
                    case(RTP_PAYLOAD_TYPE_44100HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_44100HZ_PCM):
                        st->codec->sample_rate = 441000;
                        break;
                    case(RTP_PAYLOAD_TYPE_11025HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_11025HZ_PCM):
                        st->codec->sample_rate = 11025;
                        break;
                    case(RTP_PAYLOAD_TYPE_22050HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_22050HZ_PCM):
                        st->codec->sample_rate = 22050;
                        break;
                    case(RTP_PAYLOAD_TYPE_32000HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_32000HZ_PCM):
                        st->codec->sample_rate = 32000;
                        break;
                    case(RTP_PAYLOAD_TYPE_48000HZ_ADPCM):
                    case(RTP_PAYLOAD_TYPE_48000HZ_PCM):
                        st->codec->sample_rate = 48000;
                        break;
                }
            }
            else  {
                st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
                st->codec->bits_per_coded_sample = 4;
                st->codec->sample_rate = 8000;
            }
            avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

            st->index = i;
        }
    }

    return st;
}

/**
 * Returns the data stream associated with the current connection.
 *
 * If there isn't one already, a new one will be created and added to the
 * AVFormatContext passed in.
 *
 * \param s Pointer to AVFormatContext
 * \return Pointer to the data stream on success, NULL on failure
 */
static AVStream * ad_get_data_stream(AVFormatContext *s, enum AVCodecID codecId)
{
    int i, found = FALSE;
    AVStream *st = NULL;

    for (i = 0; i < s->nb_streams && !found; i++ ) {
        st = s->streams[i];
        if (st->id == codecId)
            found = TRUE;
    }

    // Did we find our data stream? If not, create a new one
    if( !found ) {
        st = avformat_new_stream(s, NULL);
        if (st) {
            st->id = codecId;
            st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
            st->codec->codec_id = CODEC_ID_TEXT;

            // Use milliseconds as the time base
            //st->r_frame_rate = MilliTB;
            avpriv_set_pts_info(st, 32, MilliTB.num, MilliTB.den);
            //st->codec->time_base = MilliTB;

            st->index = i;
        }
    }
    return st;
}

//static AVStream *ad_get_stream(AVFormatContext *s, enum AVMediaType media, 
//                               enum AVCodecID codecId, void *data)
//{
//    switch (media)  {
//        case(AVMEDIA_TYPE_VIDEO):
//            if (codecId == CODEC_ID_PBM)
//                return ad_get_overlay_stream(s, (const char *)data);
//            else
//                return netvu_get_stream(s, (struct NetVuImageData *)data);
//            break;
//        case(AVMEDIA_TYPE_AUDIO):
//            return ad_get_audio_stream(s, (struct NetVuAudioData *)data);
//            break;
//        case(AVMEDIA_TYPE_DATA):
//            return ad_get_data_stream(s);
//            break;
//        default:
//            break;
//    }
//    return NULL;
//}

#ifdef AD_SIDEDATA_IN_PRIV
static void ad_release_packet( AVPacket *pkt )
{
    if (pkt == NULL)
        return;

    if (pkt->priv)  {
        // Have a look what type of frame we have and then free as appropriate
        struct ADFrameData *frameData = (struct ADFrameData *)pkt->priv;
        struct NetVuAudioData *audHeader;

        switch(frameData->frameType)  {
            case(NetVuAudio):
                audHeader = (struct NetVuAudioData *)frameData->frameData;
                if( audHeader->additionalData )
                    av_free( audHeader->additionalData );
            case(NetVuVideo):
            case(DMVideo):
            case(DMNudge):
            case(NetVuDataInfo):
            case(NetVuDataLayout):
            case(RTPAudio):
                av_freep(&frameData->frameData);
                av_freep(&frameData->additionalData);
                break;
            default:
                // Error, unrecognised frameType
                break;
        }
        av_freep(&pkt->priv);
    }

    // Now use the default routine to release the rest of the packet's resources
    av_destruct_packet( pkt );
}
#endif

#ifdef AD_SIDEDATA_IN_PRIV
int ad_new_packet(AVPacket *pkt, int size)
{
    int retVal = av_new_packet( pkt, size );
    pkt->priv = NULL;
    if( retVal >= 0 ) {
        // Give the packet its own destruct function
        pkt->destruct = ad_release_packet;
    }

    return retVal;
}

static void ad_keyvalsplit(const char *line, char *key, char *val)
{
    int ii, jj = 0;
    int len = strlen(line);
    int inKey, inVal;
    
    inKey = 1;
    inVal = 0;
    for (ii = 0; ii < len; ii++)  {
        if (inKey)  {
            if (line[ii] == ':')  {
                key[jj++] = '\0';
                inKey = 0;
                inVal = 1;
                jj = 0;
            }
            else  {
                key[jj++] = line[ii];
            }
        }
        else if (inVal)  {
            val[jj++] = line[ii];
        }
    }
    val[jj++] = '\0';
}

static int ad_splitcsv(const char *csv, int *results, int maxElements, int base)
{
    int ii, jj, ee;
    char element[8];
    int len = strlen(csv);
    
    for (ii = 0, jj = 0, ee = 0; ii < len; ii++)  {
        if ((csv[ii] == ',') || (csv[ii] == ';') || (ii == (len -1)))  {
            element[jj++] = '\0';
            if (base == 10)
                sscanf(element, "%d", &results[ee++]);
            else
                sscanf(element, "%x", &results[ee++]);
            if (ee >= maxElements)
                break;
            jj = 0;
        }
        else
            element[jj++] = csv[ii];
    }
    return ee;
}

static void ad_parseVSD(const char *vsd, struct ADFrameData *frame)
{
    char key[64], val[128];
    ad_keyvalsplit(vsd, key, val);
    
    if ((strlen(key) == 2) && (key[0] == 'M'))  {
        switch(key[1])  {
            case('0'):
                ad_splitcsv(val, frame->vsd[VSD_M0], VSDARRAYLEN, 10);
                break;
            case('1'):
                ad_splitcsv(val, frame->vsd[VSD_M1], VSDARRAYLEN, 10);
                break;
            case('2'):
                ad_splitcsv(val, frame->vsd[VSD_M2], VSDARRAYLEN, 10);
                break;
            case('3'):
                ad_splitcsv(val, frame->vsd[VSD_M3], VSDARRAYLEN, 10);
                break;
            case('4'):
                ad_splitcsv(val, frame->vsd[VSD_M4], VSDARRAYLEN, 10);
                break;
            case('5'):
                ad_splitcsv(val, frame->vsd[VSD_M5], VSDARRAYLEN, 10);
                break;
            case('6'):
                ad_splitcsv(val, frame->vsd[VSD_M6], VSDARRAYLEN, 10);
                break;
        }
    }
    else if ((strlen(key) == 3) && (av_strncasecmp(key, "FM0", 3) == 0))  {
        ad_splitcsv(val, frame->vsd[VSD_FM0], VSDARRAYLEN, 10);
    }
    else if ((strlen(key) == 1) && (key[0] == 'F')) {
        ad_splitcsv(val, frame->vsd[VSD_F], VSDARRAYLEN, 16);
    }
    else if ((strlen(key) == 3) && (av_strncasecmp(key, "EM0", 3) == 0))  {
        ad_splitcsv(val, frame->vsd[VSD_EM0], VSDARRAYLEN, 10);
    }
    else
        av_log(NULL, AV_LOG_DEBUG, "Unknown VSD key: %s:  Val: %s", key, val);
}

static void ad_parseLine(AVFormatContext *s, const char *line, struct ADFrameData *frame)
{
    char key[32], val[128];
    ad_keyvalsplit(line, key, val);
    
    if (av_strncasecmp(key, "Active-zones", 12) == 0)  {
        sscanf(val, "%d", &frame->activeZones);
    }
    else if (av_strncasecmp(key, "FrameNum", 8) == 0)  {
        sscanf(val, "%u", &frame->frameNum);
    }
//    else if (av_strncasecmp(key, "Site-ID", 7) == 0)  {
//    }
    else if (av_strncasecmp(key, "ActMask", 7) == 0)  {
        for (int ii = 0, jj = 0; (ii < ACTMASKLEN) && (jj < strlen(val)); ii++)  {
            sscanf(&val[jj], "0x%04hx", &frame->activityMask[ii]);
            jj += 7;
        }
    }
    else if (av_strncasecmp(key, "VSD", 3) == 0)  {
        ad_parseVSD(val, frame);
    }
}

static void ad_parseText(AVFormatContext *s, struct ADFrameData *frameData)
{
    const char *src = frameData->additionalData;
    int len = strlen(src);
    char line[512];
    int ii, jj = 0;
    
    for (ii = 0; ii < len; ii++)  {
        if ( (src[ii] == '\r') || (src[ii] == '\n') )  {
            line[jj++] = '\0';
            if (strlen(line) > 0)
                ad_parseLine(s, line, frameData);
            jj = 0;
        }
        else
            line[jj++] = src[ii];
    }
}
#endif

int initADData(int data_type, enum AVMediaType *mediaType, enum AVCodecID *codecId, void **payload)
{
    switch(data_type)  {
        case(AD_DATATYPE_JPEG):
        case(AD_DATATYPE_JFIF):
        case(AD_DATATYPE_MPEG4I):
        case(AD_DATATYPE_MPEG4P):
        case(AD_DATATYPE_H264I):
        case(AD_DATATYPE_H264P):
        case(AD_DATATYPE_MINIMAL_MPEG4):
        //case(AD_DATATYPE_MINIMAL_H264):
            *payload = av_mallocz( sizeof(struct NetVuImageData) );
            if( *payload == NULL )
                return AVERROR(ENOMEM);
            *mediaType = AVMEDIA_TYPE_VIDEO;
            switch(data_type)  {
                case(AD_DATATYPE_JPEG):
                case(AD_DATATYPE_JFIF):
                    *codecId = CODEC_ID_MJPEG;
                    break;
                case(AD_DATATYPE_MPEG4I):
                case(AD_DATATYPE_MPEG4P):
                case(AD_DATATYPE_MINIMAL_MPEG4):
                    *codecId = CODEC_ID_MPEG4;
                    break;
                case(AD_DATATYPE_H264I):
                case(AD_DATATYPE_H264P):
                //case(AD_DATATYPE_MINIMAL_H264):
                    *codecId = CODEC_ID_H264;
                    break;
            }
            break;
        case(AD_DATATYPE_AUDIO_ADPCM):
        case(AD_DATATYPE_MINIMAL_AUDIO_ADPCM):
            *payload = av_malloc( sizeof(struct NetVuAudioData) );
            if( *payload == NULL )
                return AVERROR(ENOMEM);
            *mediaType = AVMEDIA_TYPE_AUDIO;
            *codecId = CODEC_ID_ADPCM_IMA_WAV;
            break;
        case(AD_DATATYPE_INFO):
        case(AD_DATATYPE_XML_INFO):
        case(AD_DATATYPE_LAYOUT):
        case(AD_DATATYPE_SVARS_INFO):
            *mediaType = AVMEDIA_TYPE_DATA;
            *codecId = CODEC_ID_FFMETADATA;
            break;
        case(AD_DATATYPE_BMP):
            *mediaType = AVMEDIA_TYPE_VIDEO;
            *codecId = CODEC_ID_BMP;
            break;
        case(AD_DATATYPE_PBM):
            *mediaType = AVMEDIA_TYPE_VIDEO;
            *codecId = CODEC_ID_PBM;
            break;
        default:
            *mediaType = AVMEDIA_TYPE_UNKNOWN;
            *codecId = CODEC_ID_NONE;
    }
    return 0;
}

#if CONFIG_ADBINARY_DEMUXER || CONFIG_ADMIME_DEMUXER
int ad_read_jpeg(AVFormatContext *s, AVPacket *pkt, struct NetVuImageData *video_data, 
                 char **text_data)
{
    static const int nviSize = NetVuImageDataHeaderSize;
    AVIOContext *pb = s->pb;
    int hdrSize;
    char jfif[2048], *ptr;
    int n, textSize, errorVal = 0;
    int status;

    // Check if we've already read a NetVuImageData header
    // (possible if this is called by adraw demuxer)
    if (video_data && (!pic_version_valid(video_data->version)))  {
        // Read the pic structure
        if ((n = avio_read(pb, (uint8_t*)video_data, nviSize)) != nviSize)  {
            av_log(s, AV_LOG_ERROR, "%s: Short of data reading "
                                    "struct NetVuImageData, expected %d, read %d\n",
                                    __func__, nviSize, n);
            return ADFFMPEG_AD_ERROR_JPEG_IMAGE_DATA_READ;
        }

        // Endian convert if necessary
        ad_network2host(video_data, (uint8_t *)video_data);
    }

    if ((video_data==NULL) || !pic_version_valid(video_data->version))  {
        av_log(s, AV_LOG_ERROR, "%s: invalid struct NetVuImageData version "
                                "0x%08X\n", __func__, video_data->version);
        return ADFFMPEG_AD_ERROR_JPEG_PIC_VERSION;
    }

    // Get the additional text block
    textSize = video_data->start_offset;
    *text_data = av_malloc(textSize + 1);
    if( *text_data == NULL )  {
        av_log(s, AV_LOG_ERROR, "%s: text_data allocation failed "
                                "(%d bytes)", __func__, textSize + 1);
        return AVERROR(ENOMEM);
    }

    // Copy the additional text block
    if( (n = avio_read( pb, *text_data, textSize )) != textSize )  {
        av_log(s, AV_LOG_ERROR, "%s: short of data reading text block"
                                " data, expected %d, read %d\n",
                                __func__, textSize, n);
        return ADFFMPEG_AD_ERROR_JPEG_READ_TEXT_BLOCK;
    }

    // Somtimes the buffer seems to end with a NULL terminator, other times it
    // doesn't.  Adding a terminator here regardless
    (*text_data)[textSize] = '\0';

    // Use the struct NetVuImageData struct to build a JFIF header
    if ((hdrSize = build_jpeg_header( jfif, video_data, 2048)) <= 0)  {
        av_log(s, AV_LOG_ERROR, "%s: build_jpeg_header failed\n", __func__);
        return ADFFMPEG_AD_ERROR_JPEG_HEADER;
    }
    // We now know the packet size required for the image, allocate it.
    if ((status = ad_new_packet(pkt, hdrSize + video_data->size + 2)) < 0)  {
        av_log(s, AV_LOG_ERROR, "%s: ad_new_packet %d failed, "
                                "status %d\n", __func__,
                                hdrSize + video_data->size + 2, status);
        return ADFFMPEG_AD_ERROR_JPEG_NEW_PACKET;
    }
    ptr = pkt->data;
    // Copy the JFIF header into the packet
    memcpy(ptr, jfif, hdrSize);
    ptr += hdrSize;
    // Now get the compressed JPEG data into the packet
    if ((n = avio_read(pb, ptr, video_data->size)) != video_data->size) {
        av_log(s, AV_LOG_ERROR, "%s: short of data reading pic body, "
                                "expected %d, read %d\n", __func__,
                                video_data->size, n);
        return ADFFMPEG_AD_ERROR_JPEG_READ_BODY;
    }
    ptr += video_data->size;
    // Add the EOI marker
    *ptr++ = 0xff;
    *ptr++ = 0xd9;

    return errorVal;
}

int ad_read_jfif(AVFormatContext *s, AVPacket *pkt, int imgLoaded, int size,
                 struct NetVuImageData *video_data, char **text_data)
{
    int n, status, errorVal = 0;
    AVIOContext *pb = s->pb;

    if(!imgLoaded) {
        if ((status = ad_new_packet(pkt, size)) < 0) { // PRC 003
            av_log(s, AV_LOG_ERROR, "ad_read_jfif: ad_new_packet %d failed, status %d\n", size, status);
            return ADFFMPEG_AD_ERROR_JFIF_NEW_PACKET;
        }

        if ((n = avio_read(pb, pkt->data, size)) < size) {
            av_log(s, AV_LOG_ERROR, "ad_read_jfif: short of data reading jfif image, expected %d, read %d\n", size, n);
            return ADFFMPEG_AD_ERROR_JFIF_GET_BUFFER;
        }
    }
    if ( parse_jfif(s, pkt->data, video_data, size, text_data ) <= 0) {
        av_log(s, AV_LOG_ERROR, "ad_read_jfif: parse_jfif failed\n");
        return ADFFMPEG_AD_ERROR_JFIF_MANUAL_SIZE;
    }
    return errorVal;
}

/**
 * Data info extraction. A DATA_INFO frame simply contains a
 * type byte followed by a text block. Due to its simplicity, we'll
 * just extract the whole block into the AVPacket's data structure and
 * let the client deal with checking its type and parsing its text
 *
 * \todo Parse the string and use the information to add metadata and/or update
 *       the struct NetVuImageData struct
 *
 * Example strings, taken from a minimal mp4 stream: (Prefixed by a zero byte)
 * SITE DVIP3S;CAM 1:TV;(JPEG)TARGSIZE 0:(MPEG)BITRATE 262144;IMAGESIZE 0,0:704,256;
 * SITE DVIP3S;CAM 2:Directors office;(JPEG)TARGSIZE 0:(MPEG)BITRATE 262144;IMAGESIZE 0,0:704,256;
 * SITE DVIP3S;CAM 3:Development;(JPEG)TARGSIZE 0:(MPEG)BITRATE 262144;IMAGESIZE 0,0:704,256;
 * SITE DVIP3S;CAM 4:Rear road;(JPEG)TARGSIZE 0:(MPEG)BITRATE 262144;IMAGESIZE 0,0:704,256;
 */
int ad_read_info(AVFormatContext *s, AVPacket *pkt, int size)
{
    int n, status, errorVal = 0;
    AVIOContext *pb = s->pb;
    //uint8_t dataDatatype;

    // Allocate a new packet
    if( (status = ad_new_packet( pkt, size )) < 0 )
        return ADFFMPEG_AD_ERROR_INFO_NEW_PACKET;

    //dataDatatype = avio_r8(pb);
    //--size;
    
    // Get the data
    if( (n = avio_read( pb, pkt->data, size)) != size )
        return ADFFMPEG_AD_ERROR_INFO_GET_BUFFER;

    return errorVal;
}

int ad_read_layout(AVFormatContext *s, AVPacket *pkt, int size)
{
    int n, status, errorVal = 0;
    AVIOContext *pb = s->pb;

    // Allocate a new packet
    if( (status = ad_new_packet( pkt, size )) < 0 )
        return ADFFMPEG_AD_ERROR_LAYOUT_NEW_PACKET;

    // Get the data
    if( (n = avio_read( pb, pkt->data, size)) != size )
        return ADFFMPEG_AD_ERROR_LAYOUT_GET_BUFFER;

    return errorVal;
}

int ad_read_overlay(AVFormatContext *s, AVPacket *pkt, int channel, int insize, char **text_data)
{
    AdContext* adContext = s->priv_data;
    AVIOContext *pb      = s->pb;
    AVStream *st         = NULL;
    uint8_t *inbuf       = NULL;
    int n, w, h;
    char *comment = NULL;
    
    inbuf = av_malloc(insize);
    n = avio_read(pb, inbuf, insize);
    if (n != insize)  {
        av_log(s, AV_LOG_ERROR, "%s: short of data reading pbm data body, expected %d, read %d\n", __func__, insize, n);
        return ADFFMPEG_AD_ERROR_OVERLAY_GET_BUFFER;
    }
    
    pkt->size = ad_pbmDecompress(&comment, &inbuf, insize, pkt, &w, &h);
    if (pkt->size <= 0) {
		av_log(s, AV_LOG_ERROR, "ADPIC: ad_pbmDecompress failed\n");
		return ADFFMPEG_AD_ERROR_OVERLAY_PBM_READ;
	}
    
    if (text_data)  {
        int len = 12 + strlen(comment);
        *text_data = av_malloc(len);
        snprintf(*text_data, len-1, "Camera %u: %s", channel+1, comment);
        
        st = ad_get_overlay_stream(s, channel, *text_data);
        st->codec->width = w;
        st->codec->height = h;
    }
    
    av_free(comment);
    
    if (adContext)
        pkt->dts = pkt->pts = adContext->lastVideoPTS;
    
    return 0;
}
#endif


int ad_pbmDecompress(char **comment, uint8_t **src, int size, AVPacket *pkt, int *width, int *height)
{
    static const uint8_t pbm[3] = { 0x50, 0x34, 0x0A };
    static const uint8_t rle[4] = { 0x52, 0x4C, 0x45, 0x20 };
    int isrle                   = 0;
    const uint8_t *ptr          = *src;
    const uint8_t *endPtr       = (*src) + size;
    uint8_t *dPtr               = NULL;
    int strSize                 = 0;
    const char *strPtr          = NULL;
    const char *endStrPtr       = NULL;
    unsigned int elementsRead;
    
    if ((size >= sizeof(pbm)) && (ptr[0] == 'P') && (ptr[1] >= '1') && (ptr[1] <= '6') && (ptr[2] == 0x0A) )
        ptr += sizeof(pbm);
    else
        return -1;
    
    while ( (ptr < endPtr) && (*ptr == '#') )  {
        ++ptr;
        
        if ( ((endPtr - ptr) > sizeof(rle)) && (memcmp(ptr, rle, sizeof(rle)) == 0) )  {
            isrle = 1;
            ptr += sizeof(rle);
        }
        
        strPtr = ptr;
        while ( (ptr < endPtr) && (*ptr != 0x0A) )  {
            ++ptr;
        }
        endStrPtr = ptr;
        strSize = endStrPtr - strPtr;
        
        if (comment)  {
            if (*comment)
                av_free(*comment);
            *comment = av_malloc(strSize + 1);
            
            memcpy(*comment, strPtr, strSize);
            (*comment)[strSize] = '\0';
        }
        ++ptr;
    }
    
    elementsRead = sscanf(ptr, "%d", width);
    ptr += sizeof(*width) * elementsRead;
    elementsRead = sscanf(ptr, "%d", height);
    ptr += sizeof(*height) * elementsRead;
    
    if (isrle)  {
        // Data is Runlength Encoded, alloc a new buffer and decode into it
        int len;
        uint8_t val;
        unsigned int headerSize   = (ptr - *src) - sizeof(rle);
        unsigned int headerP1Size = sizeof(pbm) + 1;
        unsigned int headerP2Size = headerSize - headerP1Size;
        unsigned int dataSize = ((*width) * (*height)) / 8;
        
        ad_new_packet(pkt, headerSize + dataSize);
        dPtr = pkt->data;
        
        memcpy(dPtr, *src, headerP1Size);
        dPtr += headerP1Size;
        if (strPtr)  {
            memcpy(dPtr, strPtr, headerP2Size);
            dPtr += headerP2Size;
        }
        
        // Decompress loop
        while (ptr < endPtr)  {
            len = *ptr++;
            val = *ptr++;
            do  {
				len--;
				*dPtr++ = val;
			} while(len>0);
        }
        
        // Free compressed data
        av_freep(src);
        
        return headerSize + dataSize;
    }
    else  {
        ad_new_packet(pkt, size);
        memcpy(pkt->data, *src, size);
        av_freep(src);
        return size;
    }
}
            
static int addSideData(AVFormatContext *s, AVPacket *pkt, 
                       enum AVMediaType media, unsigned int size, 
                       void *data, const char *text)
{
#if defined(AD_SIDEDATA_IN_PRIV)
    struct ADFrameData *frameData = av_mallocz(sizeof(*frameData));
    if( frameData == NULL )
        return AVERROR(ENOMEM);

    if (media == AVMEDIA_TYPE_VIDEO)
        frameData->frameType = NetVuVideo;
    else
        frameData->frameType = NetVuAudio;
    
    frameData->frameData = data;
    frameData->additionalData = (unsigned char *)text;
    
    if (text != NULL)
        ad_parseText(s, frameData);
    pkt->priv = frameData;
#elif defined(AD_SIDEDATA)
    uint8_t *side = av_packet_new_side_data(pkt, AV_PKT_DATA_AD_FRAME, size);
    if (side)
        memcpy(side, data, size);
    
    if (text)  {
        size = strlen(text) + 1;
        side = av_packet_new_side_data(pkt, AV_PKT_DATA_AD_TEXT, size);
        if (side)
            memcpy(side, text, size);
    }
#endif
    return 0;
}
    
int ad_read_packet(AVFormatContext *s, AVPacket *pkt, int channel, 
                   enum AVMediaType media, enum AVCodecID codecId, 
                   void *data, char *text)
{
    AdContext *adContext          = s->priv_data;
    AVStream *st                  = NULL;

    if ((media == AVMEDIA_TYPE_VIDEO) && (codecId == CODEC_ID_PBM))  {
        // Get or create a data stream
        if ( (st = ad_get_overlay_stream(s, channel, text)) == NULL ) {
            av_log(s, AV_LOG_ERROR, "%s: ad_get_overlay_stream failed\n", __func__);
            return ADFFMPEG_AD_ERROR_GET_OVERLAY_STREAM;
        }
    }
    else if (media == AVMEDIA_TYPE_VIDEO)  {
        // At this point We have a legal NetVuImageData structure which we use
        // to determine which codec stream to use
        struct NetVuImageData *video_data = (struct NetVuImageData *)data;
        if ( (st = netvu_get_stream( s, video_data)) == NULL ) {
            av_log(s, AV_LOG_ERROR, "ad_read_packet: Failed get_stream for video\n");
            return ADFFMPEG_AD_ERROR_GET_STREAM;
        }
        else  {
            if (video_data->session_time > 0)  {
                pkt->pts = video_data->session_time;
                pkt->pts *= 1000ULL;
                pkt->pts += video_data->milliseconds % 1000;
            }
            else
                pkt->pts = AV_NOPTS_VALUE;
            if (adContext)
                adContext->lastVideoPTS = pkt->pts;

            // Servers occasionally send insane timezone data, which can screw
            // up clients.  Check for this and set to 0
            if (abs(video_data->utc_offset) > 1440)  {
                av_log(s, AV_LOG_INFO,
                       "ad_read_packet: Invalid utc_offset of %d, "
                       "setting to zero\n", video_data->utc_offset);
                video_data->utc_offset = 0;
            }
            
            if (adContext && (!adContext->metadataSet))  {
                char utcOffsetStr[12];
                snprintf(utcOffsetStr, sizeof(utcOffsetStr), "%d", video_data->utc_offset);
                av_dict_set(&s->metadata, "locale", video_data->locale, 0);
                av_dict_set(&s->metadata, "timezone", utcOffsetStr, 0);
                adContext->metadataSet = 1;
            }

            addSideData(s, pkt, media, sizeof(struct NetVuImageData), data, text);
        }
    }
    else if (media == AVMEDIA_TYPE_AUDIO) {
        // Get the audio stream
        struct NetVuAudioData *audHdr = (struct NetVuAudioData *)data;
        if ( (st = ad_get_audio_stream( s, audHdr )) == NULL ) {
            av_log(s, AV_LOG_ERROR, "ad_read_packet: ad_get_audio_stream failed\n");
            return ADFFMPEG_AD_ERROR_GET_AUDIO_STREAM;
        }
        else  {
            if (audHdr->seconds > 0)  {
                int64_t milliseconds = audHdr->seconds * 1000ULL + (audHdr->msecs % 1000);
                pkt->pts = av_rescale_q(milliseconds, MilliTB, st->time_base);
            }
            else
                pkt->pts = AV_NOPTS_VALUE;
            
            addSideData(s, pkt, media, sizeof(struct NetVuAudioData), audHdr, text);
        }
    }
    else if (media == AVMEDIA_TYPE_DATA) {
        // Get or create a data stream
        if ( (st = ad_get_data_stream(s, codecId)) == NULL ) {
            av_log(s, AV_LOG_ERROR, "%s: ad_get_data_stream failed\n", __func__);
            return ADFFMPEG_AD_ERROR_GET_INFO_LAYOUT_STREAM;
        }
    }

    if (st)  {
        pkt->stream_index = st->index;
        
        pkt->duration = 1;
        pkt->pos = -1;
    }

    return 0;
}

void audiodata_network2host(uint8_t *dest, const uint8_t *src, int size)
{
    const uint8_t *dataEnd = src + size;
    
    uint16_t predictor = AV_RB16(src);
    src += 2;

    AV_WL16(dest, predictor);
    dest += 2;
    
    *dest++ = *src++;
    *dest++ = *src++;

    for (;src < dataEnd; src++, dest++)  {
        *dest = (((*src) & 0xF0) >> 4) | (((*src) & 0x0F) << 4);
    }
}

int mpegOrH264(unsigned int startCode)
{
    if ( (startCode & 0xFFFFFF00) == 0x00000100)
        return PIC_MODE_MPEG4_411;
    else
        return PIC_MODE_H264I;
}
