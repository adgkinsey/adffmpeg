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

/**
 * @file 
 * AD-Holdings demuxer for AD stream format (binary)
 */

#include <strings.h>

#include "avformat.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"

#include "adpic.h"


enum pkt_offsets { DATA_TYPE, DATA_CHANNEL,
                   DATA_SIZE_BYTE_0, DATA_SIZE_BYTE_1,
                   DATA_SIZE_BYTE_2, DATA_SIZE_BYTE_3,
                   SEPARATOR_SIZE
                 };

static void audioheader_network2host(NetVuAudioData *dst, uint8_t *src);
static int ad_read_mpeg(AVFormatContext *s, AVIOContext *pb,
                        AVPacket *pkt,
                        NetVuImageData *vidDat, char **text_data);
static int ad_read_mpeg_minimal(AVFormatContext *s, AVIOContext *pb,
                                AVPacket *pkt, int size, int channel,
                                NetVuImageData *vidDat, char **text_data);
static int ad_read_audio(AVFormatContext *s, AVIOContext *pb,
                         AVPacket *pkt, int size, NetVuAudioData *data);
static int ad_read_audio_minimal(AVFormatContext *s, AVIOContext *pb,
                                 AVPacket *pkt, int size, NetVuAudioData *data);


typedef struct {
    int utc_offset;     ///< Only used in minimal video case
} AdbinaryContext;

typedef struct  {	// PRC 002
    uint32_t t;
    uint16_t ms;
    uint16_t mode;
} MinimalAudioHeader;


static void audioheader_network2host(NetVuAudioData *dst, uint8_t *src)
{
    dst->version				= AV_RB32(src);
    dst->mode					= AV_RB32(src + 4);
    dst->channel				= AV_RB32(src + 8);
    dst->sizeOfAdditionalData	= AV_RB32(src + 12);
    dst->sizeOfAudioData		= AV_RB32(src + 16);
    dst->seconds				= AV_RB32(src + 20);
    dst->msecs					= AV_RB32(src + 24);
    if ((void*)dst != (void*)src) // Copy additionalData pointer if needed
        memcpy(&dst->additionalData, src + 28, sizeof(unsigned char *));
}

/**
 * Identify if the stream as an AD binary stream
 */
static int adbinary_probe(AVProbeData *p)
{
    int score = 0;
    unsigned char *dataPtr;
    uint32_t dataSize;
    int bufferSize = p->buf_size;
    uint8_t *bufPtr = p->buf;
    
    // Netvu protocol can only send adbinary or admime
    if ( (p->filename) && (av_stristart(p->filename, "netvu://", NULL) == 1))
        score += AVPROBE_SCORE_MAX / 4;
    
    while ((bufferSize >= SEPARATOR_SIZE) && (score < AVPROBE_SCORE_MAX))  {
        dataSize =  (bufPtr[DATA_SIZE_BYTE_0] << 24) +
                    (bufPtr[DATA_SIZE_BYTE_1] << 16) +
                    (bufPtr[DATA_SIZE_BYTE_2] << 8 ) +
                    (bufPtr[DATA_SIZE_BYTE_3]);

        // Maximum of 32 cameras can be connected to a system
        if (bufPtr[DATA_CHANNEL] > 32)
            return 0;
        
        if (bufPtr[DATA_TYPE] >= MAX_DATA_TYPE)
            return 0;
        
        dataPtr = &bufPtr[SEPARATOR_SIZE];
        bufferSize -= SEPARATOR_SIZE;
        
        switch (bufPtr[DATA_TYPE])  {
            case(DATA_JPEG):
            case(DATA_MPEG4I):
            case(DATA_MPEG4P):
            case(DATA_H264I):
            case(DATA_H264P):
                if (bufferSize >= NetVuImageDataHeaderSize) {
                    NetVuImageData test;
                    ad_network2host(&test, dataPtr);
                    if (pic_version_valid(test.version))  {
                        av_log(NULL, AV_LOG_DEBUG, "%s: Detected video packet\n", __func__);
                        score += AVPROBE_SCORE_MAX;
                    }
                }
                break;
            case(DATA_JFIF):
                if (bufferSize >= 2)  {
                    if ( (*dataPtr == 0xFF) && (*(dataPtr + 1) == 0xD8) )  {
                        av_log(NULL, AV_LOG_DEBUG, "%s: Detected JFIF packet\n", __func__);
                        score += AVPROBE_SCORE_MAX;
                    }
                }
                break;
            case(DATA_AUDIO_ADPCM):
                if (bufferSize >= NetVuAudioDataHeaderSize)  {
                    NetVuAudioData test;
                    audioheader_network2host(&test, dataPtr);
                    if (test.version == AUD_VERSION)  {
                        av_log(NULL, AV_LOG_DEBUG, "%s: Detected audio packet\n", __func__);
                        score += AVPROBE_SCORE_MAX;
                    }
                }
                break;
            case(DATA_AUDIO_RAW):
                // We don't handle this format
                av_log(NULL, AV_LOG_ERROR, "%s: Detected raw audio packet (unsupported)\n", __func__);
                break;
            case(DATA_MINIMAL_MPEG4):
                if (bufferSize >= 10)  {
                    uint32_t sec  = AV_RB32(dataPtr);
                    //uint16_t mil  = AV_RB16(dataPtr + 4);
                    uint32_t vos  = AV_RB32(dataPtr + 6);
                    // Check for MPEG4 start code in data along with a timestamp
                    // of 1980 or later.  Crappy test, but there isn't much data
                    // to go on.  Should be able to use milliseconds <= 1000 but
                    // servers often send larger values than this, 
                    // nonsensical as that is
                    if ((vos >= 0x1B0) && (vos <= 0x1B6) && (sec > 315532800)) {
                        av_log(NULL, AV_LOG_DEBUG, "%s: Detected minimal MPEG4 packet\n", __func__);
                        score += AVPROBE_SCORE_MAX / 4;
                    }
                }
                break;
            case(DATA_MINIMAL_AUDIO_ADPCM):
                if (bufferSize >= 8)  {
                    MinimalAudioHeader test;
                    test.t     = AV_RB32(dataPtr);
                    test.ms    = AV_RB16(dataPtr + 4);
                    test.mode  = AV_RB16(dataPtr + 6);

                    switch(test.mode)  {
                        case(RTP_PAYLOAD_TYPE_8000HZ_ADPCM):
                        case(RTP_PAYLOAD_TYPE_11025HZ_ADPCM):
                        case(RTP_PAYLOAD_TYPE_16000HZ_ADPCM):
                        case(RTP_PAYLOAD_TYPE_22050HZ_ADPCM):
                        case(RTP_PAYLOAD_TYPE_32000HZ_ADPCM):
                        case(RTP_PAYLOAD_TYPE_44100HZ_ADPCM):
                        case(RTP_PAYLOAD_TYPE_48000HZ_ADPCM):
                        case(RTP_PAYLOAD_TYPE_8000HZ_PCM):
                        case(RTP_PAYLOAD_TYPE_11025HZ_PCM):
                        case(RTP_PAYLOAD_TYPE_16000HZ_PCM):
                        case(RTP_PAYLOAD_TYPE_22050HZ_PCM):
                        case(RTP_PAYLOAD_TYPE_32000HZ_PCM):
                        case(RTP_PAYLOAD_TYPE_44100HZ_PCM):
                        case(RTP_PAYLOAD_TYPE_48000HZ_PCM):
                            av_log(NULL, AV_LOG_DEBUG, "%s: Detected minimal audio packet\n", __func__);
                            score += AVPROBE_SCORE_MAX;
                    }
                }
                break;
            case(DATA_LAYOUT):
                av_log(NULL, AV_LOG_DEBUG, "%s: Detected layout packet\n", __func__);
                break;
            case(DATA_INFO):
                av_log(NULL, AV_LOG_DEBUG, "%s: Detected info packet\n", __func__);
                break;
            case(DATA_XML_INFO):
                if (bufferSize >= dataSize)  {
                    const char *infoString = "<infoList>";
                    int infoStringLen = strlen(infoString);
                    if (infoStringLen > dataSize)
                        infoStringLen = dataSize;
                    if (strncasecmp(dataPtr, infoString, infoStringLen) == 0)  {
                        av_log(NULL, AV_LOG_DEBUG, "%s: Detected xml info packet\n", __func__);
                        score += AVPROBE_SCORE_MAX;
                    }
                }
                break;
        }
        
        bufferSize -= dataSize;
        bufPtr = dataPtr + dataSize;
    }

    if (score > AVPROBE_SCORE_MAX)
        score = AVPROBE_SCORE_MAX;
    
    av_log(NULL, AV_LOG_DEBUG, "%s: Score %d\n", __func__, score);
    
    return score;
}

static int adbinary_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AdbinaryContext *adContext = s->priv_data;
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return ad_read_header(s, ap, &adContext->utc_offset);
}

static int adbinary_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *         pb = s->pb;
    NetVuAudioData *        audDat = NULL;
    NetVuImageData *        vidDat = NULL;
    char *                  txtDat = NULL;
    int                     data_type, data_channel, size;
    int                     errorVal = 0;
    ADFrameType             frameType;

    // First read the 6 byte separator
    data_type    = avio_r8(pb);
    data_channel = avio_r8(pb);
    size         = avio_rb32(pb);
    if (size == 0)  {
        if(url_feof(pb))
            errorVal = AVERROR_EOF;
        else {
            av_log(s, AV_LOG_ERROR, "%s: Reading separator, errorcode %d\n",  
                   __func__, url_ferror(pb));
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
                av_log(s, AV_LOG_WARNING, "%s: No handler for data_type = %d\n",
                       __func__, data_type);
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
    
    if (errorVal < 0)  {
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
static int ad_read_mpeg(AVFormatContext *s, AVIOContext *pb,
                        AVPacket *pkt,
                        NetVuImageData *vidDat, char **text_data)
{
    static const int hdrSize = NetVuImageDataHeaderSize;
    int textSize = 0;
    int n, status, errorVal = 0;

    if ((n = ad_get_buffer(pb, (uint8_t *)vidDat, hdrSize)) != hdrSize) {
        av_log(s, AV_LOG_ERROR, "%s: short of data reading header, "
                                "expected %d, read %d\n", 
               __func__, hdrSize, n);
        return ADPIC_MPEG4_GET_BUFFER_ERROR;
    }
    ad_network2host(vidDat, (uint8_t *)vidDat);
    if (!pic_version_valid(vidDat->version)) {
        av_log(s, AV_LOG_ERROR, "%s: invalid pic version 0x%08X\n", __func__, 
               vidDat->version);
        return ADPIC_MPEG4_PIC_VERSION_VALID_ERROR;
    }

    // Get the additional text block
    textSize = vidDat->start_offset;
    *text_data = av_malloc( textSize + 1 );

    if( *text_data == NULL )
        return ADPIC_MPEG4_ALOCATE_TEXT_BUFFER_ERROR;

    // Copy the additional text block
    if( (n = ad_get_buffer( pb, *text_data, textSize)) != textSize) {
        av_log(s, AV_LOG_ERROR, "%s: short of data reading text, "
                                "expected %d, read %d\n", __func__, 
               textSize, n);
        return ADPIC_MPEG4_GET_TEXT_BUFFER_ERROR;
    }

    // Somtimes the buffer seems to end with a NULL terminator,
    // other times it doesn't.  Adding a terminator here regardless
    (*text_data)[textSize] = '\0';

    if ((status = ad_new_packet(pkt, vidDat->size)) < 0) {
        av_log(s, AV_LOG_ERROR, "%s: ad_new_packet (size %d) failed, status %d\n", 
               __func__, vidDat->size, status);
        return ADPIC_MPEG4_NEW_PACKET_ERROR;
    }
    if ((n = ad_get_buffer(pb, pkt->data, vidDat->size)) != vidDat->size) {
        av_log(s, AV_LOG_ERROR, "%s: short of data reading mpeg, "
                                "expected %d, read %d\n", 
               __func__, vidDat->size, n);
        return ADPIC_MPEG4_PIC_BODY_ERROR;
    }
    
    if (vidDat->vid_format & (PIC_MODE_MPEG4_411_I | PIC_MODE_MPEG4_411_GOV_I))
        pkt->flags |= AV_PKT_FLAG_KEY;    
    
    return errorVal;
}

/**
 * MPEG4 or H264 video frame with a minimal header
 */
static int ad_read_mpeg_minimal(AVFormatContext *s, AVIOContext *pb,
                                AVPacket *pkt, int size, int channel,
                                NetVuImageData *vidDat, char **text_data)
{
    static const int titleLen = sizeof(vidDat->title) / sizeof(vidDat->title[0]);
    AdbinaryContext* adContext = s->priv_data;
    int              dataSize     = size - 6;
    int              errorVal     = 0;

    // Get the minimal video header and copy into generic video data structure
    memset(vidDat, 0, sizeof(NetVuImageData));
    vidDat->session_time  = avio_rb32(pb);
    vidDat->milliseconds  = avio_rb16(pb);
    
    if ( url_ferror(pb) || (vidDat->session_time == 0) )  {
        av_log(s, AV_LOG_ERROR, "%s: Reading header, errorcode %d\n", 
               __func__, url_ferror(pb));
        return ADPIC_MPEG4_MINIMAL_GET_BUFFER_ERROR;
    }
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
static int ad_read_audio(AVFormatContext *s, AVIOContext *pb,
                         AVPacket *pkt, int size, NetVuAudioData *data)
{
    int n, status, errorVal = 0;

    // Get the fixed size portion of the audio header
    size = NetVuAudioDataHeaderSize - sizeof(unsigned char *);
    if( (n = ad_get_buffer( pb, (uint8_t*)data, size)) != size)
        return ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR;

    // endian fix it...
    audioheader_network2host(data, (uint8_t*)data);

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
    
    audiodata_network2host(pkt->data, data->sizeOfAudioData);

    return errorVal;
}

/**
 * Audio frame with a minimal header
 */
static int ad_read_audio_minimal(AVFormatContext *s, AVIOContext *pb,
                                 AVPacket *pkt, int size, NetVuAudioData *data)
{
    int dataSize = size - sizeof(MinimalAudioHeader);
    int n, status, errorVal = 0;

    // Get the minimal audio header and copy into generic audio data structure
    memset(data, 0, sizeof(NetVuAudioData));
    data->seconds = avio_rb32(pb);
    data->msecs   = avio_rb16(pb);
    data->mode    = avio_rb16(pb);
    if ( url_ferror(pb) || (data->seconds == 0) )  {
        av_log(s, AV_LOG_ERROR, "%s: Reading header, errorcode %d\n", 
               __func__, url_ferror(pb));
        return ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR;
    }

    // Now get the main frame data into a new packet
    if( (status = ad_new_packet( pkt, dataSize )) < 0 )
        return ADPIC_MINIMAL_AUDIO_ADPCM_NEW_PACKET_ERROR;

    if( (n = ad_get_buffer( pb, pkt->data, dataSize )) != dataSize )
        return ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR2;

    audiodata_network2host(pkt->data, dataSize);
    
    return errorVal;
}


AVInputFormat ff_adbinary_demuxer = {
    .name           = "adbinary",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings video format (binary)"),
    .priv_data_size = sizeof(AdbinaryContext),
    .read_probe     = adbinary_probe,
    .read_header    = adbinary_read_header,
    .read_packet    = adbinary_read_packet,
    .read_close     = adbinary_read_close,
};
