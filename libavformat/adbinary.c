/*
 * AD-Holdings demuxer for AD stream format (binary)
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

#include "avformat.h"
#include "libavutil/bswap.h"

#include "adpic.h"


enum pkt_offsets { DATA_TYPE, DATA_CHANNEL,
                   DATA_SIZE_BYTE_0, DATA_SIZE_BYTE_1,
                   DATA_SIZE_BYTE_2, DATA_SIZE_BYTE_3,
                   SEPARATOR_SIZE
                 };

static void audioheader_network2host( NetVuAudioData *hdr );
static int ad_read_mpeg(AVFormatContext *s, ByteIOContext *pb,
                        AVPacket *pkt,
                        NetVuImageData *vidDat, char **text_data);
static int ad_read_mpeg_minimal(AVFormatContext *s, ByteIOContext *pb,
                                AVPacket *pkt, int size, int channel,
                                NetVuImageData *vidDat, char **text_data);
static int ad_read_audio(AVFormatContext *s, ByteIOContext *pb,
                         AVPacket *pkt, int size, NetVuAudioData *data);
static int ad_read_audio_minimal(AVFormatContext *s, ByteIOContext *pb,
                                 AVPacket *pkt, int size, NetVuAudioData *data);


typedef struct {
    int utc_offset;     ///< Only used in minimal video case
} AdbinaryContext;

typedef struct _minimal_video_header {	// PRC 002
    uint32_t            t;
    unsigned short      ms;
} MinimalVideoHeader;

typedef struct _minimal_audio_header {	// PRC 002
    uint32_t t;
    unsigned short ms;
    unsigned short mode;
} MinimalAudioHeader;


static void audioheader_network2host( NetVuAudioData *hdr )
{
    hdr->version				= be2me_32(hdr->version);
    hdr->mode					= be2me_32(hdr->mode);
    hdr->channel				= be2me_32(hdr->channel);
    hdr->sizeOfAdditionalData	= be2me_32(hdr->sizeOfAdditionalData);
    hdr->sizeOfAudioData		= be2me_32(hdr->sizeOfAudioData);
    hdr->seconds				= be2me_32(hdr->seconds);
    hdr->msecs					= be2me_32(hdr->msecs);
}

/**
 * Identify if the stream as an AD binary stream
 */
static int adbinary_probe(AVProbeData *p)
{
    int dataType;
    unsigned char *dataPtr;
    unsigned long dataSize;

    if (p->buf_size <= SEPARATOR_SIZE)
        return 0;

    dataSize =  (p->buf[DATA_SIZE_BYTE_0] << 24) +
                (p->buf[DATA_SIZE_BYTE_1] << 16) +
                (p->buf[DATA_SIZE_BYTE_2] << 8 ) +
                (p->buf[DATA_SIZE_BYTE_3]);
    // Frames should not be larger than 2MB
    if (dataSize > 0x200000)
        return 0;

    dataType = p->buf[DATA_TYPE];
    dataPtr = &p->buf[SEPARATOR_SIZE];

    if ((dataType <= DATA_XML_INFO) && (p->buf[DATA_CHANNEL] <= 32))  {
        if ( (dataType == DATA_JPEG)  ||
             (dataType == DATA_MPEG4I) || (dataType == DATA_MPEG4P) ||
             (dataType == DATA_H264I)  || (dataType == DATA_H264P)  ) {
            if (p->buf_size >= (SEPARATOR_SIZE + sizeof(NetVuImageData)) )  {
                NetVuImageData test;
                memcpy(&test, dataPtr, sizeof(NetVuImageData));
                ad_network2host(&test);
                if (pic_version_valid(test.version))
                    return AVPROBE_SCORE_MAX;
            }
        }
        else if (dataType == DATA_JFIF)  {
            if (p->buf_size >= (SEPARATOR_SIZE + 2) )  {
                unsigned char *dataPtr = &p->buf[SEPARATOR_SIZE];
                if ( (*dataPtr == 0xFF) && (*(dataPtr + 1) == 0xD8) )
                    return AVPROBE_SCORE_MAX;
            }
        }
        else if (dataType == DATA_AUDIO_ADPCM)  {
            if (p->buf_size >= (SEPARATOR_SIZE + sizeof(NetVuAudioData)) )  {
                NetVuAudioData test;
                memcpy(&test, dataPtr, sizeof(NetVuAudioData));
                audioheader_network2host(&test);
                if (test.version == AUD_VERSION)
                    return AVPROBE_SCORE_MAX;
            }
        }
        else if (dataType == DATA_AUDIO_RAW)  {
            // We don't handle this format
            return 0;
        }
        else if (dataType == DATA_MINIMAL_MPEG4)  {
            if (p->buf_size >= (SEPARATOR_SIZE + 6) ) {
                MinimalVideoHeader test;
                memcpy(&test, dataPtr, 6);
            test.t  = be2me_32(test.t);
                // If timestamp is between 1980 and 2020 then accept it
                if ( (test.t > 315532800) && (test.t < 1577836800) )
                    return AVPROBE_SCORE_MAX;
            }
        }
        else if (dataType == DATA_MINIMAL_AUDIO_ADPCM)  {
            if (p->buf_size >= (SEPARATOR_SIZE + sizeof(MinimalAudioHeader)) ) {
                MinimalAudioHeader test;
                memcpy(&test, dataPtr, sizeof(MinimalAudioHeader));
            test.t     = be2me_32(test.t);
            test.ms    = be2me_16(test.ms);
            test.mode  = be2me_16(test.mode);

                if ( (test.mode == RTP_PAYLOAD_TYPE_8000HZ_ADPCM)  ||
                     (test.mode == RTP_PAYLOAD_TYPE_11025HZ_ADPCM) ||
                     (test.mode == RTP_PAYLOAD_TYPE_16000HZ_ADPCM) ||
                     (test.mode == RTP_PAYLOAD_TYPE_22050HZ_ADPCM) ||
                     (test.mode == RTP_PAYLOAD_TYPE_32000HZ_ADPCM) ||
                     (test.mode == RTP_PAYLOAD_TYPE_44100HZ_ADPCM) ||
                     (test.mode == RTP_PAYLOAD_TYPE_48000HZ_ADPCM) ||
                     (test.mode == RTP_PAYLOAD_TYPE_8000HZ_PCM)    ||
                     (test.mode == RTP_PAYLOAD_TYPE_11025HZ_PCM)   ||
                     (test.mode == RTP_PAYLOAD_TYPE_16000HZ_PCM)   ||
                     (test.mode == RTP_PAYLOAD_TYPE_22050HZ_PCM)   ||
                     (test.mode == RTP_PAYLOAD_TYPE_32000HZ_PCM)   ||
                     (test.mode == RTP_PAYLOAD_TYPE_44100HZ_PCM)   ||
                     (test.mode == RTP_PAYLOAD_TYPE_48000HZ_PCM)   ) {
                    return AVPROBE_SCORE_MAX;
                }
            }
        }
        else if (dataType == DATA_LAYOUT)  {
            // Need a stronger test for this
            return AVPROBE_SCORE_MAX / 2;
        }
        else if (dataType == DATA_INFO)  {
            if (p->buf_size >= (SEPARATOR_SIZE + 5) )  {
                if (memcmp(dataPtr + 1, "SITE", 4) == 0)
                    return AVPROBE_SCORE_MAX;
                else
                    return AVPROBE_SCORE_MAX / 2;
            }
        }
        else if (dataType == DATA_XML_INFO)  {
            const char *infoString = "<infoList>";
            if (p->buf_size >= (SEPARATOR_SIZE + strlen(infoString)) )  {
                int infoStringLen = strlen(infoString);
                if (p->buf_size >= (SEPARATOR_SIZE + infoStringLen) )  {
                    if (strncasecmp(dataPtr, infoString, infoStringLen) == 0)
                        return AVPROBE_SCORE_MAX;
                }
            }
        }
    }

    return 0;
}

static int adbinary_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AdbinaryContext *adContext = s->priv_data;
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return ad_read_header(s, ap, &adContext->utc_offset);
}

static int adbinary_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *         pb = s->pb;
    NetVuAudioData *        audDat = NULL;
    NetVuImageData *        vidDat = NULL;
    char *                  txtDat = NULL;
    int                     data_type, data_channel, size;
    int                     errorVal = 0;
    ADFrameType             frameType;

    // First read the 6 byte separator
    data_type    = get_byte(pb);
    data_channel = get_byte(pb);
    size         = get_be32(pb);
    if (size == 0)  {
        if(url_feof(pb))
            errorVal = ADPIC_END_OF_STREAM;
        else {
            av_log(s, AV_LOG_ERROR, "adbinary_read_packet: Reading separator, "
                   "errorcode %d\n", url_ferror(pb));
            errorVal = ADPIC_READ_6_BYTE_SEPARATOR_ERROR;
        }
        return errorVal;
    }

    // Prepare for video or audio read
    errorVal = initADData(data_type, &frameType, &vidDat, &audDat);
    if (errorVal >= 0)  {
        // Proceed based on the type of data in this frame
        switch(data_type) {
            case DATA_JPEG:
                errorVal = ad_read_jpeg(s, pb, pkt, vidDat, &txtDat);
                break;
            case DATA_JFIF:
                errorVal = ad_read_jfif(s, pb, pkt, 0, size, vidDat, &txtDat);
                break;
            case DATA_MPEG4I:
            case DATA_MPEG4P:
            case DATA_H264I:
            case DATA_H264P:
                errorVal = ad_read_mpeg(s, pb, pkt, vidDat, &txtDat);
                break;
            case DATA_MINIMAL_MPEG4:
                errorVal = ad_read_mpeg_minimal(s, pb, pkt, size, data_channel,
                                                vidDat, &txtDat);
                break;
            case DATA_MINIMAL_AUDIO_ADPCM:
                errorVal = ad_read_audio_minimal(s, pb, pkt, size, audDat);
                break;
            case DATA_AUDIO_ADPCM:
                errorVal = ad_read_audio(s, pb, pkt, size, audDat);
                break;
            case DATA_INFO:
            case DATA_XML_INFO:
                // May want to handle INFO and XML_INFO separately in future
                errorVal = ad_read_info(s, pb, pkt, size);
                break;
            case DATA_LAYOUT:
                errorVal = ad_read_layout(s, pb, pkt, size);
                break;
            default:
                av_log(s, AV_LOG_WARNING, "adbinary_read_packet: "
                       "No handler for data_type = "
                       "%d\n", data_type);
                errorVal = ADPIC_DEFAULT_ERROR;
                break;
        }
    }

    if (errorVal >= 0)  {
        if (frameType == NetVuAudio)
            errorVal = ad_read_packet(s, pb, pkt, frameType, audDat, txtDat);
        else
            errorVal = ad_read_packet(s, pb, pkt, frameType, vidDat, txtDat);
    }
    else  {
        // If there was an error, release any memory has been allocated
        if( vidDat != NULL )
            av_free( vidDat );

        if( audDat != NULL )
            av_free( audDat );

        if( txtDat != NULL )
            av_free( txtDat );
    }

    return errorVal;
}

static int adbinary_read_close(AVFormatContext *s)
{
    return 0;
}

/**
 * MPEG4 or H264 video frame with a Netvu header
 */
static int ad_read_mpeg(AVFormatContext *s, ByteIOContext *pb,
                        AVPacket *pkt,
                        NetVuImageData *vidDat, char **text_data)
{
    static const int hdrSize = sizeof(NetVuImageData);
    int n, status, errorVal = 0;

    if ((n = ad_get_buffer(pb, (uint8_t *)vidDat, hdrSize)) != hdrSize) {
        av_log(s, AV_LOG_ERROR, "ad_read_mpeg: short of data reading header, "
                                "expected %d, read %d\n", 
               sizeof (NetVuImageData), n);
        errorVal = ADPIC_MPEG4_GET_BUFFER_ERROR;
        return errorVal;
    }
    ad_network2host(vidDat);
    if (!pic_version_valid(vidDat->version)) {
        av_log(s, AV_LOG_ERROR, "ad_read_mpeg: invalid pic version 0x%08X\n", 
               vidDat->version);
        errorVal = ADPIC_MPEG4_PIC_VERSION_VALID_ERROR;
        return errorVal;
    }

    // Get the additional text block
    *text_data = av_malloc( vidDat->start_offset + 1 );

    if( *text_data == NULL )
        return ADPIC_MPEG4_ALOCATE_TEXT_BUFFER_ERROR;

    // Copy the additional text block
    if( (n = ad_get_buffer( pb, *text_data, vidDat->start_offset )) != vidDat->start_offset ) {
        av_log(s, AV_LOG_ERROR, "ad_read_mpeg: short of data reading text, "
                                "expected %d, read %d\n", 
               vidDat->start_offset, n);
        errorVal = ADPIC_MPEG4_GET_TEXT_BUFFER_ERROR;
        return errorVal;
    }

    // Somtimes the buffer seems to end with a NULL terminator,
    // other times it doesn't.  Adding a NULL terminator here regardless
    (*text_data)[vidDat->start_offset] = '\0';

    if ((status = ad_new_packet(pkt, vidDat->size)) < 0) { // PRC 003
        av_log(s, AV_LOG_ERROR, "ad_read_mpeg: ad_new_packet (size %d) failed, "
                                "status %d\n", vidDat->size, status);
        errorVal = ADPIC_MPEG4_NEW_PACKET_ERROR;
        return errorVal;
    }
    if ((n = ad_get_buffer(pb, pkt->data, vidDat->size)) != vidDat->size) {
        av_log(s, AV_LOG_ERROR, "ad_read_mpeg: short of data reading mpeg, "
                                "expected %d, read %d\n", vidDat->size, n);
        errorVal = ADPIC_MPEG4_PIC_BODY_ERROR;
        return errorVal;
    }
    return errorVal;
}

/**
 * MPEG4 or H264 video frame with a minimal header
 */
static int ad_read_mpeg_minimal(AVFormatContext *s, ByteIOContext *pb,
                                AVPacket *pkt, int size, int channel,
                                NetVuImageData *vidDat, char **text_data)
{
    static const int titleLen = sizeof(vidDat->title) / sizeof(vidDat->title[0]);
    AdbinaryContext* adContext = s->priv_data;
    int              dataSize     = size - 6;
    int              errorVal     = 0;

    // Get the minimal video header and copy into generic video data structure
    memset(vidDat, 0, sizeof(NetVuImageData));
    vidDat->session_time  = get_be32(pb);
    vidDat->milliseconds  = get_be16(pb);
    
    if ( url_ferror(pb) || (vidDat->session_time == 0) )
        return ADPIC_MPEG4_MINIMAL_GET_BUFFER_ERROR;
    vidDat->version = PIC_VERSION;
    vidDat->cam = channel + 1;
    vidDat->utc_offset = adContext->utc_offset;
    vidDat->vid_format = PIC_MODE_MPEG4_411;
    snprintf(vidDat->title, titleLen, "Camera %d", vidDat->cam);

    // Now get the main frame data into a new packet
    if( ad_new_packet(pkt, dataSize) < 0 )
        return ADPIC_MPEG4_MINIMAL_NEW_PACKET_ERROR;

    if( ad_get_buffer(pb, pkt->data, dataSize) != dataSize )
        return ADPIC_MPEG4_MINIMAL_NEW_PACKET_ERROR2;

    return errorVal;
}

/**
 * Audio frame with a Netvu header
 */
static int ad_read_audio(AVFormatContext *s, ByteIOContext *pb,
                         AVPacket *pkt, int size, NetVuAudioData *data)
{
    int n, status, errorVal = 0;

    // Get the fixed size portion of the audio header
    size = sizeof(NetVuAudioData) - sizeof(unsigned char *);
    if( (n = ad_get_buffer( pb, (unsigned char*)data, size)) != size)
        return ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR;

    // endian fix it...
    audioheader_network2host(data);

    // Now get the additional bytes
    if( data->sizeOfAdditionalData > 0 ) {
        data->additionalData = av_malloc( data->sizeOfAdditionalData );
        if( data->additionalData == NULL )
            return ADPIC_AUDIO_ADPCM_ALOCATE_ADITIONAL_ERROR;

        if( (n = ad_get_buffer( pb, data->additionalData, data->sizeOfAdditionalData )) != data->sizeOfAdditionalData )
            return ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR2;
    }
    else
        data->additionalData = NULL;

    if( (status = ad_new_packet( pkt, data->sizeOfAudioData )) < 0 )
        return ADPIC_AUDIO_ADPCM_MIME_NEW_PACKET_ERROR;

    // Now get the actual audio data
    if( (n = ad_get_buffer( pb, pkt->data, data->sizeOfAudioData)) != data->sizeOfAudioData )
        return ADPIC_AUDIO_ADPCM_MIME_GET_BUFFER_ERROR;

    return errorVal;
}

/**
 * Audio frame with a minimal header
 */
static int ad_read_audio_minimal(AVFormatContext *s, ByteIOContext *pb,
                                 AVPacket *pkt, int size, NetVuAudioData *data)
{
    int dataSize = size - sizeof(MinimalAudioHeader);
    int n, status, errorVal = 0;

    // Get the minimal audio header and copy into generic audio data structure
    memset(data, 0, sizeof(NetVuAudioData));
    data->seconds = get_be32(pb);
    data->msecs = get_be16(pb);
    data->mode = get_be16(pb);
    if ( url_ferror(pb) || (data->seconds == 0) )
        return ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR;

    // Now get the main frame data into a new packet
    if( (status = ad_new_packet( pkt, dataSize )) < 0 )
        return ADPIC_MINIMAL_AUDIO_ADPCM_NEW_PACKET_ERROR;

    if( (n = ad_get_buffer( pb, pkt->data, dataSize )) != dataSize )
        return ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR2;

    return errorVal;
}


AVInputFormat adbinary_demuxer = {
    .name           = "adbinary",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings video format (binary)"),
    .priv_data_size = sizeof(AdbinaryContext),
    .read_probe     = adbinary_probe,
    .read_header    = adbinary_read_header,
    .read_packet    = adbinary_read_packet,
    .read_close     = adbinary_read_close,
};
