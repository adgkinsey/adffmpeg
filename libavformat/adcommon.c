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

#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"

#include "adpic.h"
#include "adjfif.h"
#include "netvu.h"


#define AUDIO_STREAM_ID             1
#define DATA_STREAM_ID              2


AVStream * netvu_get_stream(AVFormatContext *s, NetVuImageData *pic);
AVStream * ad_get_data_stream(AVFormatContext *s);
void ad_release_packet(AVPacket *pkt);
static void ad_parseText(ADFrameData *frameData);


int ad_read_header(AVFormatContext *s, AVFormatParameters *ap, int *utcOffset)
{
    AVIOContext*        pb = s->pb;
    URLContext*            urlContext = pb->opaque;
    NetvuContext*        nv = NULL;

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
            av_metadata_set2(&s->metadata, nv->hdrNames[ii], nv->hdrs[ii], 0);
        }
        if ( (nv->utc_offset >= 0) && (nv->utc_offset <= 1440) )  {
            snprintf(temp, sizeof(temp), "%d", nv->utc_offset);
            av_metadata_set2(&s->metadata, "timezone", temp, 0);
        }
    }

    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return 0;
}

void ad_network2host(NetVuImageData *pic, uint8_t *data)
{
    pic->version                = AV_RB32(data + 0);
    pic->mode                    = AV_RB32(data + 4);
    pic->cam                    = AV_RB32(data + 8);
    pic->vid_format                = AV_RB32(data + 12);
    pic->start_offset            = AV_RB32(data + 16);
    pic->size                    = AV_RB32(data + 20);
    pic->max_size                = AV_RB32(data + 24);
    pic->target_size            = AV_RB32(data + 28);
    pic->factor                    = AV_RB32(data + 32);
    pic->alm_bitmask_hi            = AV_RB32(data + 36);
    pic->status                    = AV_RB32(data + 40);
    pic->session_time            = AV_RB32(data + 44);
    pic->milliseconds            = AV_RB32(data + 48);
    if ((uint8_t *)pic != data)  {
        memcpy(pic->res,    data + 52, 4);
        memcpy(pic->title,  data + 56, 31);
        memcpy(pic->alarm,  data + 87, 31);
    }
    pic->format.src_pixels        = AV_RB16(data + 118);
    pic->format.src_lines        = AV_RB16(data + 120);
    pic->format.target_pixels    = AV_RB16(data + 122);
    pic->format.target_lines    = AV_RB16(data + 124);
    pic->format.pixel_offset    = AV_RB16(data + 126);
    pic->format.line_offset        = AV_RB16(data + 128);
    if ((uint8_t *)pic != data)
        memcpy(pic->locale, data + 130, 30);
    pic->utc_offset                = AV_RB32(data + 160);
    pic->alm_bitmask            = AV_RB32(data + 164);
}

AVStream * netvu_get_stream(AVFormatContext *s, NetVuImageData *p)
{
    time_t dateSec;
    char dateStr[18];
    AVStream *stream = ad_get_stream(s,
                                     p->format.target_pixels,
                                     p->format.target_lines,
                                     p->cam,
                                     p->vid_format,
                                     p->title);
    stream->start_time = p->session_time * 1000LL + p->milliseconds;
    dateSec = p->session_time;
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%MZ", gmtime(&dateSec));
    av_metadata_set2(&stream->metadata, "date", dateStr, 0);
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

AVStream * ad_get_stream(AVFormatContext *s, uint16_t w, uint16_t h, uint8_t cam, int32_t format, const char *title)
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

    id = (codec_type << 29)     |
         ((cam - 1) << 24) |
         ((w >> 4) << 12)    |
         ((h >> 4) << 0);

    found = FALSE;
    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        if (st->id == id) {
            found = TRUE;
            break;
        }
    }
    if (!found) {
        st = av_new_stream( s, id);
        if (st) {
            st->codec->codec_type = CODEC_TYPE_VIDEO;
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
            st->r_frame_rate = (AVRational) { 1, 1000 };
            av_set_pts_info(st, 32, 1, 1000);
            st->codec->time_base = (AVRational) { 1, 1000 };

            if (title)
                av_metadata_set2(&st->metadata, "title", title, 0);
            snprintf(textbuffer, sizeof(textbuffer), "%u", cam);
            av_metadata_set2(&st->metadata, "track", textbuffer, 0);
        }
    }
    return st;
}

AVStream * ad_get_audio_stream(AVFormatContext *s, NetVuAudioData* audioHeader)
{
    int id;
    int i, found;
    AVStream *st;

    found = FALSE;

    id = AUDIO_STREAM_ID;

    for( i = 0; i < s->nb_streams; i++ ) {
        st = s->streams[i];
        if( st->id == id ) {
            found = TRUE;
            break;
        }
    }

    // Did we find our audio stream? If not, create a new one
    if( !found ) {
        st = av_new_stream( s, id );
        if (st) {
            st->codec->codec_type = CODEC_TYPE_AUDIO;
            st->codec->codec_id = CODEC_ID_ADPCM_IMA_WAV;
            st->codec->channels = 1;
            st->codec->block_align = 0;
            st->codec->bits_per_coded_sample = 4;

            if (audioHeader)  {
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
            else
                st->codec->sample_rate = 8000;
            av_set_pts_info(st, 32, 1, st->codec->sample_rate);

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
AVStream * ad_get_data_stream( AVFormatContext *s )
{
    int id = DATA_STREAM_ID;
    int i, found = FALSE;
    AVStream *st = NULL;

    for( i = 0; i < s->nb_streams && !found; i++ ) {
        st = s->streams[i];
        if( st->id == id )
            found = TRUE;
    }

    // Did we find our data stream? If not, create a new one
    if( !found ) {
        st = av_new_stream( s, id );
        if (st) {
            st->codec->codec_type = CODEC_TYPE_DATA;
            st->codec->codec_id = CODEC_ID_TEXT;

            // Use milliseconds as the time base
            //st->r_frame_rate = (AVRational) { 1, 1000 };
            av_set_pts_info(st, 32, 1, 1000);
            //st->codec->time_base = (AVRational) { 1, 1000 };

            st->index = i;
        }
    }
    return st;
}

int ad_new_packet(AVPacket *pkt, int size)
{
    int retVal = av_new_packet( pkt, size );

    if( retVal >= 0 ) {
        // Give the packet its own destruct function
        pkt->destruct = ad_release_packet;
    }

    return retVal;
}

void ad_release_packet( AVPacket *pkt )
{
    ADFrameData *frameData;

    if ( (pkt == NULL) || (pkt->priv == NULL) )
        return;

    // Have a look what type of frame we have and then free as appropriate
    frameData = (ADFrameData *)pkt->priv;

    if( frameData->frameType == NetVuAudio ) {
        NetVuAudioData *audioHeader = (NetVuAudioData *)frameData->frameData;
        if( audioHeader->additionalData )
            av_free( audioHeader->additionalData );
    }

    // Nothing else has nested allocs so just delete the frameData if it exists
    if( frameData->frameData  )
        av_free( frameData->frameData );
    if( frameData->additionalData )
        av_free( frameData->additionalData );

    av_free( pkt->priv );

    // Now use the default routine to release the rest of the packet's resources
    av_destruct_packet( pkt );
}

int ad_get_buffer(AVIOContext *s, uint8_t *buf, int size)
{
#ifdef FF_API_OLD_AVIO
    return avio_read(s, buf, size);
#else
    int TotalDataRead = 0;
    int DataReadThisTime = 0;
    int RetryBoundry = 200;
    int retrys = 0;

    //get data while ther is no time out and we still need data
    while(TotalDataRead < size && retrys < RetryBoundry) {
        DataReadThisTime += avio_read(s, buf, (size - TotalDataRead));

        // if we retreave some data keep trying until we get the required data
        // or we have much longer time out
        if(DataReadThisTime > 0 && RetryBoundry < 1000)
            RetryBoundry += 10;

        TotalDataRead += DataReadThisTime;
        retrys++;
    }
    return TotalDataRead;
#endif
}

int initADData(int data_type, ADFrameType *frameType, void **payload)
{
    if( (data_type == DATA_JPEG)          || (data_type == DATA_JFIF)   ||
        (data_type == DATA_MPEG4I)        || (data_type == DATA_MPEG4P) ||
        (data_type == DATA_H264I)         || (data_type == DATA_H264P)  ||
        (data_type == DATA_MINIMAL_MPEG4)                               )
    {
        *frameType = NetVuVideo;
        *payload = av_malloc( sizeof(NetVuImageData) );
        if( *payload == NULL )
            return AVERROR(ENOMEM);
    }
    else if ( (data_type == DATA_AUDIO_ADPCM) ||
              (data_type == DATA_MINIMAL_AUDIO_ADPCM) ) {
        *frameType = NetVuAudio;
        *payload = av_malloc( sizeof(NetVuAudioData) );
        if( *payload == NULL )
            return AVERROR(ENOMEM);
    }
    else if ( (data_type == DATA_INFO) || (data_type == DATA_XML_INFO) )
        *frameType = NetVuDataInfo;
    else if( data_type == DATA_LAYOUT )
        *frameType = NetVuDataLayout;
    else
        *frameType = FrameTypeUnknown;

    return 0;
}

int ad_read_jpeg(AVFormatContext *s, AVIOContext *pb,
                 AVPacket *pkt,
                 NetVuImageData *video_data, char **text_data)
{
    static const int nviSize = NetVuImageDataHeaderSize;
    int hdrSize;
    char jfif[2048], *ptr;
    int n, textSize, errorVal = 0;
    int status;

    // Check if we've already read a pic header
    if (!pic_version_valid(video_data->version))  {
        // Read the pic structure
        if ((n = ad_get_buffer(pb, (uint8_t*)video_data, nviSize)) != nviSize)  {
            av_log(s, AV_LOG_ERROR, "ad_read_jpeg: Short of data reading "
                                    "NetVuImageData, expected %d, read %d\n",
                                    nviSize, n);
            return ADPIC_JPEG_IMAGE_DATA_READ_ERROR;
        }

        // Endian convert if necessary
        ad_network2host(video_data, (uint8_t *)video_data);
    }

    if (!pic_version_valid(video_data->version))  {
        av_log(s, AV_LOG_ERROR, "%s: invalid NetVuImageData version "
                                "0x%08X\n", __func__, video_data->version);
        return ADPIC_JPEG_PIC_VERSION_ERROR;
    }

    // Get the additional text block
    textSize = video_data->start_offset;
    *text_data = av_malloc(textSize + 1);
    if( *text_data == NULL )  {
        av_log(s, AV_LOG_ERROR, "ad_read_jpeg: text_data allocation failed "
                                "(%d bytes)", textSize + 1);
        return AVERROR(ENOMEM);
    }

    // Copy the additional text block
    if( (n = ad_get_buffer( pb, *text_data, textSize )) != textSize )  {
        av_log(s, AV_LOG_ERROR, "ad_read_jpeg: short of data reading text block"
                                " data, expected %d, read %d\n", textSize, n);
        return ADPIC_JPEG_READ_TEXT_BLOCK_ERROR;
    }

    // Somtimes the buffer seems to end with a NULL terminator, other times it
    // doesn't.  Adding a terminator here regardless
    (*text_data)[textSize] = '\0';

    // Use the NetVuImageData struct to build a JFIF header
    if ((hdrSize = build_jpeg_header( jfif, video_data, 2048)) <= 0)  {
        av_log(s, AV_LOG_ERROR, "ad_read_jpeg: build_jpeg_header failed\n");
        return ADPIC_JPEG_HEADER_ERROR;
    }
    // We now know the packet size required for the image, allocate it.
    if ((status = ad_new_packet(pkt, hdrSize + video_data->size + 2)) < 0)  {
        av_log(s, AV_LOG_ERROR, "ad_read_jpeg: ad_new_packet %d failed, "
                                "status %d\n", hdrSize + video_data->size + 2,
                                status);
        return ADPIC_JPEG_NEW_PACKET_ERROR;
    }
    ptr = pkt->data;
    // Copy the JFIF header into the packet
    memcpy(ptr, jfif, hdrSize);
    ptr += hdrSize;
    // Now get the compressed JPEG data into the packet
    if ((n = ad_get_buffer(pb, ptr, video_data->size)) != video_data->size) {
        av_log(s, AV_LOG_ERROR, "ad_read_jpeg: short of data reading pic body, "
                                "expected %d, read %d\n", video_data->size, n);
        return ADPIC_JPEG_READ_BODY_ERROR;
    }
    ptr += video_data->size;
    // Add the EOI marker
    *ptr++ = 0xff;
    *ptr++ = 0xd9;

    return errorVal;
}

int ad_read_jfif(AVFormatContext *s, AVIOContext *pb,
                 AVPacket *pkt, int imgLoaded, int size,
                 NetVuImageData *video_data, char **text_data)
{
    int n, status, errorVal = 0;

    if(!imgLoaded) {
        if ((status = ad_new_packet(pkt, size)) < 0) { // PRC 003
            av_log(s, AV_LOG_ERROR, "ad_read_jfif: ad_new_packet %d failed, status %d\n", size, status);
            return ADPIC_JFIF_NEW_PACKET_ERROR;
        }

        if ((n = ad_get_buffer(pb, pkt->data, size)) < size) {
            av_log(s, AV_LOG_ERROR, "ad_read_jfif: short of data reading jfif image, expected %d, read %d\n", size, n);
            return ADPIC_JFIF_GET_BUFFER_ERROR;
        }
    }
    if ( parse_jfif(s, pkt->data, video_data, size, text_data ) <= 0) {
        av_log(s, AV_LOG_ERROR, "ad_read_jfif: parse_jfif failed\n");
        return ADPIC_JFIF_MANUAL_SIZE_ERROR;
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
 *       the NetVuImageData struct
 *
 * Example strings, taken from a minimal mp4 stream: (Prefixed by a zero byte)
 * SITE DVIP3S;CAM 1:TV;(JPEG)TARGSIZE 0:(MPEG)BITRATE 262144;IMAGESIZE 0,0:704,256;
 * SITE DVIP3S;CAM 2:Directors office;(JPEG)TARGSIZE 0:(MPEG)BITRATE 262144;IMAGESIZE 0,0:704,256;
 * SITE DVIP3S;CAM 3:Development;(JPEG)TARGSIZE 0:(MPEG)BITRATE 262144;IMAGESIZE 0,0:704,256;
 * SITE DVIP3S;CAM 4:Rear road;(JPEG)TARGSIZE 0:(MPEG)BITRATE 262144;IMAGESIZE 0,0:704,256;
 */
int ad_read_info(AVFormatContext *s, AVIOContext *pb,
                 AVPacket *pkt, int size)
{
    int n, status, errorVal = 0;

    // Allocate a new packet
    if( (status = ad_new_packet( pkt, size )) < 0 )
        return ADPIC_INFO_NEW_PACKET_ERROR;

    // Skip first byte
    avio_r8(pb);
    --size;
    
    // Get the data
    if( (n = ad_get_buffer( pb, pkt->data, size)) != size )
        return ADPIC_INFO_GET_BUFFER_ERROR;

    return errorVal;
}

int ad_read_layout(AVFormatContext *s, AVIOContext *pb,
                   AVPacket *pkt, int size)
{
    int n, status, errorVal = 0;

    // Allocate a new packet
    if( (status = ad_new_packet( pkt, size )) < 0 )
        return ADPIC_LAYOUT_NEW_PACKET_ERROR;

    // Get the data
    if( (n = ad_get_buffer( pb, pkt->data, size)) != size )
        return ADPIC_LAYOUT_GET_BUFFER_ERROR;

    return errorVal;
}

int ad_read_packet(AVFormatContext *s, AVIOContext *pb, AVPacket *pkt,
                   ADFrameType frameType, void *data, char *text_data)
{
    AVStream    *st        = NULL;
    ADFrameData *frameData = NULL;

    if (frameType == NetVuVideo)  {
        // At this point We have a legal NetVuImageData structure which we use
        // to determine which codec stream to use
        NetVuImageData *video_data = (NetVuImageData *)data;
        if ( (st = netvu_get_stream( s, video_data)) == NULL ) {
            av_log(s, AV_LOG_ERROR, "ad_read_packet: Failed get_stream for video\n");
            return ADPIC_GET_STREAM_ERROR;
        }
        else  {
            if (video_data->session_time > 0)  {
                pkt->pts = video_data->session_time;
                pkt->pts *= 1000ULL;
                pkt->pts += video_data->milliseconds % 1000;
            }
            else
                pkt->pts = AV_NOPTS_VALUE;

            // Servers occasionally send insane timezone data, which can screw
            // up clients.  Check for this and set to 0
            if (abs(video_data->utc_offset) > 1440)  {
                av_log(s, AV_LOG_INFO,
                       "ad_read_packet: Invalid utc_offset of %d, "
                       "setting to zero\n", video_data->utc_offset);
                video_data->utc_offset = 0;
            }
        }
    }
    else if( frameType == NetVuAudio ) {
        // Get the audio stream
        NetVuAudioData *audio_data = (NetVuAudioData *)data;
        if ( (st = ad_get_audio_stream( s, audio_data )) == NULL ) {
            av_log(s, AV_LOG_ERROR, "ad_read_packet: ad_get_audio_stream failed\n");
            return ADPIC_GET_AUDIO_STREAM_ERROR;
        }
        else  {
            if (audio_data->seconds > 0)  {
                pkt->pts = audio_data->seconds;
                pkt->pts *= 1000ULL;
                pkt->pts += audio_data->msecs % 1000;
            }
            else
                pkt->pts = AV_NOPTS_VALUE;
        }
    }
    else if( frameType == NetVuDataInfo || frameType == NetVuDataLayout ) {
        // Get or create a data stream
        if ( (st = ad_get_data_stream( s )) == NULL ) {
            av_log(s, AV_LOG_ERROR, "ad_read_packet: ad_get_data_stream failed\n");
            return ADPIC_GET_INFO_LAYOUT_STREAM_ERROR;
        }
    }

    pkt->stream_index = st->index;
    frameData = av_mallocz(sizeof(*frameData));
    if( frameData == NULL )
        return AVERROR(ENOMEM);

    frameData->frameType = frameType;

    if ( (frameType == NetVuVideo) || (frameType == NetVuAudio) )  {
        frameData->frameData = data;
        if (text_data != NULL)  {
            frameData->additionalData = text_data;

            /// Todo: AVOption to allow toggling parsing of text on and off
            ad_parseText(frameData);
        }
    }
    else if (frameType == NetVuDataInfo || frameType == NetVuDataLayout)
        frameData->frameData = NULL;
    else  // Shouldn't really get here...
        frameData->frameType = FrameTypeUnknown;
    
    pkt->priv = frameData;
    pkt->duration = 0;
    pkt->pos = -1;

    return 0;
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

static int ad_splitcsv(const char *csv, int *results, int maxElements)
{
    int ii, jj, ee;
    char element[8];
    int len = strlen(csv);
    
    for (ii = 0, jj = 0, ee = 0; ii < len; ii++)  {
        if ((csv[ii] == ',') || (csv[ii] == ';') || (ii == (len -1)))  {
            element[jj++] = '\0';
            sscanf(element, "%d", &results[ee++]);
            if (ee >= maxElements)
                break;
            jj = 0;
        }
        else
            element[jj++] = csv[ii];
    }
    return ee;
}

static void ad_parseVSD(const char *vsd, ADFrameData *frame)
{
    char key[64], val[128];
    ad_keyvalsplit(vsd, key, val);
    
    if ((strlen(key) == 2) && (key[0] == 'M'))  {
        switch(key[1])  {
            case('0'):
                ad_splitcsv(val, frame->vsd[VSD_M0], 16);
                break;
            case('1'):
                ad_splitcsv(val, frame->vsd[VSD_M1], 16);
                break;
            case('2'):
                ad_splitcsv(val, frame->vsd[VSD_M2], 16);
                break;
            case('3'):
                ad_splitcsv(val, frame->vsd[VSD_M3], 16);
                break;
            case('4'):
                ad_splitcsv(val, frame->vsd[VSD_M4], 16);
                break;
            case('5'):
                ad_splitcsv(val, frame->vsd[VSD_M5], 16);
                break;
            case('6'):
                ad_splitcsv(val, frame->vsd[VSD_M6], 16);
                break;
        }
    }
    else if ((strlen(key) == 3) && (strncasecmp(key, "FM0", 3) == 0))  {
        ad_splitcsv(val, frame->vsd[VSD_FM0], 16);
    }
    else if ((strlen(key) == 1) && (key[0] == 'F')) {
        ad_splitcsv(val, frame->vsd[VSD_F], 16);
    }
}

static void ad_parseLine(const char *line, ADFrameData *frame)
{
    char key[128], val[128];
    ad_keyvalsplit(line, key, val);
    
    if (strncasecmp(key, "Active-zones", 12) == 0)  {
        sscanf(val, "%d", &frame->activeZones);
    }
    else if (strncasecmp(key, "FrameNum", 8) == 0)  {
        sscanf(val, "%u", &frame->frameNum);
    }
//    else if (strncasecmp(key, "Site-ID", 7) == 0)  {
//    }
    else if (strncasecmp(key, "ActMask", 7) == 0)  {
        for (int ii = 0, jj = 0; (ii < 16) && (jj < strlen(val)); ii++)  {
            sscanf(&val[jj], "0x%04hx", &frame->activityMask[ii]);
            jj += 7;
        }
    }
    else if (strncasecmp(key, "VSD", 3) == 0)  {
        ad_parseVSD(val, frame);
    }
}

static void ad_parseText(ADFrameData *frameData)
{
    const char *src = frameData->additionalData;
    int len = strlen(src);
    char line[512];
    int ii, jj = 0;
    
    for (ii = 0; ii < len; ii++)  {
        if ( (src[ii] == '\r') || (src[ii] == '\n') )  {
            line[jj++] = '\0';
            if (strlen(line) > 0)
                ad_parseLine(line, frameData);
            jj = 0;
        }
        else
            line[jj++] = src[ii];
    }
}

void audiodata_network2host(uint8_t *data, int size)
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
