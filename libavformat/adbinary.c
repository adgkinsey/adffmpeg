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
#include "adffmpeg_errors.h"


enum pkt_offsets { PKT_DATATYPE, 
                   PKT_DATACHANNEL,
                   PKT_SIZE_BYTE_0, 
                   PKT_SIZE_BYTE_1,
                   PKT_SIZE_BYTE_2, 
                   PKT_SIZE_BYTE_3,
                   PKT_SEPARATOR_SIZE
                 };

typedef struct  {    // PRC 002
    uint32_t t;
    uint16_t ms;
    uint16_t mode;
} MinimalAudioHeader;


static void audioheader_network2host(struct NetVuAudioData *dst, const uint8_t *src)
{
    dst->version              = AV_RB32(src);
    dst->mode                 = AV_RB32(src + 4);
    dst->channel              = AV_RB32(src + 8);
    dst->sizeOfAdditionalData = AV_RB32(src + 12);
    dst->sizeOfAudioData      = AV_RB32(src + 16);
    dst->seconds              = AV_RB32(src + 20);
    dst->msecs                = AV_RB32(src + 24);
    if ((void*)dst != (const void*)src) // Copy additionalData pointer if needed
        memcpy(&dst->additionalData, src + 28, sizeof(unsigned char *));
}

/**
 * MPEG4 or H264 video frame with a Netvu header
 */
static int adbinary_mpeg(AVFormatContext *s,
                         AVPacket *pkt,
                         struct NetVuImageData *vidDat,
                         char **txtDat)
{
    static const int hdrSize = NetVuImageDataHeaderSize;
    AVIOContext *pb = s->pb;
    int textSize = 0;
    int n, status, errorVal = 0;

    n = avio_read(pb, (uint8_t *)vidDat, hdrSize);
    if (n < hdrSize) {
        av_log(s, AV_LOG_ERROR, "%s: short of data reading header, "
                                "expected %d, read %d\n",
               __func__, hdrSize, n);
        return ADFFMPEG_AD_ERROR_MPEG4_PIC_BODY;
    }
    ad_network2host(vidDat, (uint8_t *)vidDat);
    if (!pic_version_valid(vidDat->version)) {
        av_log(s, AV_LOG_ERROR, "%s: invalid pic version 0x%08X\n", __func__,
               vidDat->version);
        return ADFFMPEG_AD_ERROR_MPEG4_PIC_VERSION_VALID;
    }

    // Get the additional text block
    textSize = vidDat->start_offset;
    *txtDat = av_malloc(textSize + 1);

    if (*txtDat == NULL)  {
        av_log(s, AV_LOG_ERROR, "%s: Failed to allocate memory for text\n", __func__);
        return AVERROR(ENOMEM);
    }

    // Copy the additional text block
    n = avio_get_str(pb, textSize, *txtDat, textSize+1);
    if (n < textSize)
        avio_skip(pb, textSize - n);

    status = av_get_packet(pb, pkt, vidDat->size);
    if (status < 0)  {
        av_log(s, AV_LOG_ERROR, "%s: av_get_packet (size %d) failed, status %d\n",
               __func__, vidDat->size, status);
        return ADFFMPEG_AD_ERROR_MPEG4_NEW_PACKET;
    }

    if ( (vidDat->vid_format == PIC_MODE_MPEG4_411_I) || (vidDat->vid_format == PIC_MODE_MPEG4_411_GOV_I) )
        pkt->flags |= AV_PKT_FLAG_KEY;

    return errorVal;
}

/**
 * MPEG4 or H264 video frame with a minimal header
 */
static int adbinary_mpeg_minimal(AVFormatContext *s,
                                AVPacket *pkt, int size, int channel,
                                struct NetVuImageData *vidDat, char **text_data,
                                int adDataType)
{
    static const int titleLen  = sizeof(vidDat->title) / sizeof(vidDat->title[0]);
    AdContext*       adContext = s->priv_data;
    AVIOContext *    pb        = s->pb;
    int              dataSize  = size - (4 + 2);
    int              errorVal  = 0;

    // Get the minimal video header and copy into generic video data structure
    memset(vidDat, 0, sizeof(struct NetVuImageData));
    vidDat->session_time  = avio_rb32(pb);
    vidDat->milliseconds  = avio_rb16(pb);

    if ( pb->error || (vidDat->session_time == 0) )  {
        av_log(s, AV_LOG_ERROR, "%s: Reading header, errorcode %d\n",
               __func__, pb->error);
        return ADFFMPEG_AD_ERROR_MPEG4_MINIMAL_GET_BUFFER;
    }
    vidDat->version = PIC_VERSION;
    vidDat->cam = channel + 1;
    vidDat->utc_offset = adContext->utc_offset;
    snprintf(vidDat->title, titleLen, "Camera %d", vidDat->cam);

    // Now get the main frame data into a new packet
    errorVal = av_get_packet(pb, pkt, dataSize);
    if( errorVal < 0 )  {
        av_log(s, AV_LOG_ERROR, "%s: av_get_packet (size %d) failed, status %d\n",
               __func__, dataSize, errorVal);
        return ADFFMPEG_AD_ERROR_MPEG4_MINIMAL_NEW_PACKET;
    }
    
    if (adContext->streamDatatype == 0)  {
        //if (adDataType == AD_DATATYPE_MININAL_H264)
        //    adContext->streamDatatype = PIC_MODE_H264I;
        //else
        adContext->streamDatatype = mpegOrH264(AV_RB32(pkt->data));
    }
    vidDat->vid_format = adContext->streamDatatype;

    return errorVal;
}

/**
 * Audio frame with a Netvu header
 */
static int ad_read_audio(AVFormatContext *s,
                         AVPacket *pkt, int size, 
                         struct NetVuAudioData *data,
                         enum AVCodecID codec_id)
{
    AVIOContext *pb = s->pb;
    int status;

    // Get the fixed size portion of the audio header
    size = NetVuAudioDataHeaderSize - sizeof(unsigned char *);
    if (avio_read( pb, (uint8_t*)data, size) != size)
        return ADFFMPEG_AD_ERROR_AUDIO_ADPCM_GET_BUFFER;

    // endian fix it...
    audioheader_network2host(data, (uint8_t*)data);

    // Now get the additional bytes
    if( data->sizeOfAdditionalData > 0 ) {
        data->additionalData = av_malloc( data->sizeOfAdditionalData );
        if( data->additionalData == NULL )
            return AVERROR(ENOMEM);

        if (avio_read( pb, data->additionalData, data->sizeOfAdditionalData) != data->sizeOfAdditionalData)
            return ADFFMPEG_AD_ERROR_AUDIO_ADPCM_GET_BUFFER2;
    }
    else
        data->additionalData = NULL;

    status = av_get_packet(pb, pkt, data->sizeOfAudioData);
    if (status  < 0)  {
        av_log(s, AV_LOG_ERROR, "%s: av_get_packet (size %d) failed, status %d\n",
               __func__, data->sizeOfAudioData, status);
        return ADFFMPEG_AD_ERROR_AUDIO_ADPCM_MIME_NEW_PACKET;
    }
    
    if (codec_id == CODEC_ID_ADPCM_IMA_WAV)
        audiodata_network2host(pkt->data, pkt->data, data->sizeOfAudioData);

    return status;
}

/**
 * Audio frame with a minimal header
 */
static int adbinary_audio_minimal(AVFormatContext *s,
                                  AVPacket *pkt, int size, 
                                  struct NetVuAudioData *data)
{
    AVIOContext *pb = s->pb;
    int dataSize = size - (4 + 2 + 2);
    int status;

    // Get the minimal audio header and copy into generic audio data structure
    memset(data, 0, sizeof(struct NetVuAudioData));
    data->seconds = avio_rb32(pb);
    data->msecs   = avio_rb16(pb);
    data->mode    = avio_rb16(pb);
    if ( pb->error || (data->seconds == 0) )  {
        av_log(s, AV_LOG_ERROR, "%s: Reading header, errorcode %d\n",
               __func__, pb->error);
        return ADFFMPEG_AD_ERROR_MINIMAL_AUDIO_ADPCM_GET_BUFFER;
    }

    // Now get the main frame data into a new packet
    status = av_get_packet(pb, pkt, dataSize);
    if (status < 0)  {
        av_log(s, AV_LOG_ERROR, "%s: av_get_packet (size %d) failed, status %d\n",
               __func__, data->sizeOfAudioData, status);
        return ADFFMPEG_AD_ERROR_MINIMAL_AUDIO_ADPCM_NEW_PACKET;
    }

    audiodata_network2host(pkt->data, pkt->data, dataSize);

    return status;
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

    while ((bufferSize >= PKT_SEPARATOR_SIZE) && (score < AVPROBE_SCORE_MAX))  {
        dataSize =  (bufPtr[PKT_SIZE_BYTE_0] << 24) +
                    (bufPtr[PKT_SIZE_BYTE_1] << 16) +
                    (bufPtr[PKT_SIZE_BYTE_2] << 8 ) +
                    (bufPtr[PKT_SIZE_BYTE_3]);

        // Sanity check on dataSize
        if ((dataSize < 6) || (dataSize > 0x1000000))
            return 0;
        
        // Maximum of 32 cameras can be connected to a system
        if (bufPtr[PKT_DATACHANNEL] > 32)
            return 0;

        dataPtr = &bufPtr[PKT_SEPARATOR_SIZE];
        bufferSize -= PKT_SEPARATOR_SIZE;

        switch (bufPtr[PKT_DATATYPE])  {
            case AD_DATATYPE_JPEG:
            case AD_DATATYPE_MPEG4I:
            case AD_DATATYPE_MPEG4P:
            case AD_DATATYPE_H264I:
            case AD_DATATYPE_H264P:
                if (bufferSize >= NetVuImageDataHeaderSize) {
                    struct NetVuImageData test;
                    ad_network2host(&test, dataPtr);
                    if (pic_version_valid(test.version))  {
                        av_log(NULL, AV_LOG_DEBUG, "%s: Detected video packet\n", __func__);
                        score += AVPROBE_SCORE_MAX;
                    }
                }
                break;
            case AD_DATATYPE_JFIF:
                if (bufferSize >= 2)  {
                    if ( (*dataPtr == 0xFF) && (*(dataPtr + 1) == 0xD8) )  {
                        av_log(NULL, AV_LOG_DEBUG, "%s: Detected JFIF packet\n", __func__);
                        score += AVPROBE_SCORE_MAX;
                    }
                }
                break;
            case AD_DATATYPE_AUDIO_ADPCM:
                if (bufferSize >= NetVuAudioDataHeaderSize)  {
                    struct NetVuAudioData test;
                    audioheader_network2host(&test, dataPtr);
                    if (test.version == AUD_VERSION)  {
                        av_log(NULL, AV_LOG_DEBUG, "%s: Detected audio packet\n", __func__);
                        score += AVPROBE_SCORE_MAX;
                    }
                }
                break;
            case AD_DATATYPE_AUDIO_RAW:
                // We don't handle this format
                av_log(NULL, AV_LOG_DEBUG, "%s: Detected raw audio packet (unsupported)\n", __func__);
                break;
            case AD_DATATYPE_MINIMAL_MPEG4:
                if (bufferSize >= 10)  {
                    uint32_t sec  = AV_RB32(dataPtr);
                    uint32_t vos  = AV_RB32(dataPtr + 6);
                    
                    if (mpegOrH264(vos) == PIC_MODE_MPEG4_411)  {
                        // Check for MPEG4 start code in data along with a timestamp
                        // of 1980 or later.  Crappy test, but there isn't much data
                        // to go on.  Should be able to use milliseconds <= 1000 but
                        // servers often send larger values than this,
                        // nonsensical as that is
                        if (sec > 315532800)  {
                            if ((vos >= 0x1B0) && (vos <= 0x1B6)) {
                                av_log(NULL, AV_LOG_DEBUG, "%s: Detected minimal MPEG4 packet %u\n", __func__, dataSize);
                                score += AVPROBE_SCORE_MAX / 4;
                            }
                        }
                        break;
                    }
                }
                // Servers can send h264 identified as MPEG4, so fall through
                // to next case
            //case(AD_DATATYPE_MINIMAL_H264):
                if (bufferSize >= 10)  {
                    uint32_t sec  = AV_RB32(dataPtr);
                    uint32_t vos  = AV_RB32(dataPtr + 6);
                    // Check for h264 start code in data along with a timestamp
                    // of 1980 or later.  Crappy test, but there isn't much data
                    // to go on.  Should be able to use milliseconds <= 1000 but
                    // servers often send larger values than this,
                    // nonsensical as that is
                    if (sec > 315532800)  {
                        if (vos == 0x01) {
                            av_log(NULL, AV_LOG_DEBUG, "%s: Detected minimal h264 packet %u\n", __func__, dataSize);
                            score += AVPROBE_SCORE_MAX / 4;
                        }
                    }
                }
                break;
            case AD_DATATYPE_MINIMAL_AUDIO_ADPCM:
                if (bufferSize >= 8)  {
                    MinimalAudioHeader test;
                    test.t     = AV_RB32(dataPtr);
                    test.ms    = AV_RB16(dataPtr + 4);
                    test.mode  = AV_RB16(dataPtr + 6);

                    switch(test.mode)  {
                        case RTP_PAYLOAD_TYPE_8000HZ_ADPCM:
                        case RTP_PAYLOAD_TYPE_11025HZ_ADPCM:
                        case RTP_PAYLOAD_TYPE_16000HZ_ADPCM:
                        case RTP_PAYLOAD_TYPE_22050HZ_ADPCM:
                        case RTP_PAYLOAD_TYPE_32000HZ_ADPCM:
                        case RTP_PAYLOAD_TYPE_44100HZ_ADPCM:
                        case RTP_PAYLOAD_TYPE_48000HZ_ADPCM:
                        case RTP_PAYLOAD_TYPE_8000HZ_PCM:
                        case RTP_PAYLOAD_TYPE_11025HZ_PCM:
                        case RTP_PAYLOAD_TYPE_16000HZ_PCM:
                        case RTP_PAYLOAD_TYPE_22050HZ_PCM:
                        case RTP_PAYLOAD_TYPE_32000HZ_PCM: 
                        case RTP_PAYLOAD_TYPE_44100HZ_PCM:
                        case RTP_PAYLOAD_TYPE_48000HZ_PCM:
                            av_log(NULL, AV_LOG_DEBUG, "%s: Detected minimal audio packet\n", __func__);
                            score += AVPROBE_SCORE_MAX / 4;
                    }
                }
                break;
            case AD_DATATYPE_LAYOUT:
                av_log(NULL, AV_LOG_DEBUG, "%s: Detected layout packet\n", __func__);
                break;
            case AD_DATATYPE_INFO:
                if ( (bufferSize >= 1) && (dataPtr[0] == 0) || (dataPtr[0] == 1) )  {
                    av_log(NULL, AV_LOG_DEBUG, "%s: Detected info packet (%d)\n", __func__, dataPtr[0]);
                    if ((bufferSize >= 5) && (strncmp(&dataPtr[1], "SITE", 4) == 0))
                        score += AVPROBE_SCORE_MAX;
                    else if ((bufferSize >= 15) && (strncmp(&dataPtr[1], "(JPEG)TARGSIZE", 14) == 0))
                        score += AVPROBE_SCORE_MAX;
                    else
                        score += 5;
                }
                break;
            case AD_DATATYPE_XML_INFO:
                if (bufferSize >= dataSize)  {
                    const char *infoString = "<infoList>";
                    int infoStringLen = strlen(infoString);
                    if ( (infoStringLen <= dataSize) && (av_strncasecmp(dataPtr, infoString, infoStringLen) == 0) )  {
                        av_log(NULL, AV_LOG_DEBUG, "%s: Detected xml info packet\n", __func__);
                        score += AVPROBE_SCORE_MAX;
                    }
                }
                break;
            case AD_DATATYPE_BMP:
                av_log(NULL, AV_LOG_DEBUG, "%s: Detected bmp packet\n", __func__);
                break;
            case AD_DATATYPE_PBM:
                if (bufferSize >= 3)  {
                    if ((dataPtr[0] == 'P') && (dataPtr[1] >= '1') && (dataPtr[1] <= '6'))  {
                        if (dataPtr[2] == 0x0A)  {
                            score += AVPROBE_SCORE_MAX;
                            av_log(NULL, AV_LOG_DEBUG, "%s: Detected pbm packet\n", __func__);
                        }
                    }
                }
                break;
            case AD_DATATYPE_SVARS_INFO:
                if ( (bufferSize >= 1) && (dataPtr[0] == 0) || (dataPtr[0] == 1) )  {
                    av_log(NULL, AV_LOG_DEBUG, "%s: Detected svars info packet (%d)\n", __func__, dataPtr[0]);
                    score += 5;
                }
                break;
            default:
                av_log(NULL, AV_LOG_DEBUG, "%s: Detected unknown packet type\n", __func__);
                break;
        }

        if (dataSize <= bufferSize)  {
            bufferSize -= dataSize;
            bufPtr = dataPtr + dataSize;
        }
        else  {
            bufferSize = 0;
            bufPtr = p->buf;
        }
    }

    if (score > AVPROBE_SCORE_MAX)
        score = AVPROBE_SCORE_MAX;

    av_log(NULL, AV_LOG_DEBUG, "%s: Score %d\n", __func__, score);

    return score;
}

static int adbinary_read_header(AVFormatContext *s)
{
    AdContext *adContext = s->priv_data;
    return ad_read_header(s, &adContext->utc_offset);
}

static int adbinary_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *       pb        = s->pb;
    void *              payload   = NULL;
    char *              txtDat    = NULL;
    int                 errorVal  = -1;
    unsigned char *     tempbuf   = NULL;
    enum AVMediaType    mediaType = AVMEDIA_TYPE_UNKNOWN;
    enum CodecID        codecId   = CODEC_ID_NONE;
    int                 data_type, data_channel;
    unsigned int        size;
    uint8_t             temp[6];

    // First read the 6 byte separator
    if (avio_read(pb, temp, 6) >= 6)  {
        data_type    = temp[0];
        data_channel = temp[1];
        size         = AV_RB32(temp + 2);
        if (data_type >= AD_DATATYPE_MAX)  {
            av_log(s, AV_LOG_WARNING, "%s: No handler for data_type = %d", __func__, data_type);
            return ADFFMPEG_AD_ERROR_READ_6_BYTE_SEPARATOR;
        }
        if (data_channel >= 32)  {
            av_log(s, AV_LOG_WARNING, "%s: Channel number %d too high", __func__, data_channel);
            return ADFFMPEG_AD_ERROR_READ_6_BYTE_SEPARATOR;
        }
        if (size >= 0x1000000)  {
            av_log(s, AV_LOG_WARNING, "%s: Packet too large, %d bytes", __func__, size);
            return ADFFMPEG_AD_ERROR_READ_6_BYTE_SEPARATOR;
        }
    }
    else
        return ADFFMPEG_AD_ERROR_READ_6_BYTE_SEPARATOR;

    if (size == 0)  {
        if(pb->eof_reached)
            errorVal = AVERROR_EOF;
        else {
            av_log(s, AV_LOG_ERROR, "%s: Reading separator, error code %d\n",
                   __func__, pb->error);
            errorVal = ADFFMPEG_AD_ERROR_READ_6_BYTE_SEPARATOR;
        }
        return errorVal;
    }

    // Prepare for video or audio read
    errorVal = initADData(data_type, &mediaType, &codecId, &payload);
    if (errorVal >= 0)  {
        // Proceed based on the type of data in this frame
        switch(data_type) {
            case AD_DATATYPE_JPEG:
                errorVal = ad_read_jpeg(s, pkt, payload, &txtDat);
                break;
            case AD_DATATYPE_JFIF:
                errorVal = ad_read_jfif(s, pkt, 0, size, payload, &txtDat);
                break;
            case AD_DATATYPE_MPEG4I:
            case AD_DATATYPE_MPEG4P:
            case AD_DATATYPE_H264I:
            case AD_DATATYPE_H264P:
                errorVal = adbinary_mpeg(s, pkt, payload, &txtDat);
                break;
            case AD_DATATYPE_MINIMAL_MPEG4:
            //case(AD_DATATYPE_MINIMAL_H264):
                errorVal = adbinary_mpeg_minimal(s, pkt, size, data_channel,
                                                 payload, &txtDat, data_type);
                break;
            case AD_DATATYPE_MINIMAL_AUDIO_ADPCM:
                errorVal = adbinary_audio_minimal(s, pkt, size, payload);
                break;
            case AD_DATATYPE_AUDIO_ADPCM:
                errorVal = ad_read_audio(s, pkt, size, payload, CODEC_ID_ADPCM_IMA_WAV);
                break;
            case AD_DATATYPE_INFO:
            case AD_DATATYPE_XML_INFO:
            case AD_DATATYPE_SVARS_INFO:
                // May want to handle INFO, XML_INFO and SVARS_INFO separately in future
                errorVal = ad_read_info(s, pkt, size);
                break;
            case AD_DATATYPE_LAYOUT:
                errorVal = ad_read_layout(s, pkt, size);
                break;
            case AD_DATATYPE_BMP:
                av_dlog(s, "Bitmap overlay\n");
                tempbuf = av_malloc(size);  
                if (tempbuf)  {
                    avio_read(pb, tempbuf, size);
                    av_free(tempbuf);
                }
                else
                    return AVERROR(ENOMEM);
                return ADFFMPEG_AD_ERROR_DEFAULT;
            case AD_DATATYPE_PBM:
                errorVal = ad_read_overlay(s, pkt, data_channel, size, &txtDat);
                break;
            default:
                av_log(s, AV_LOG_WARNING, "%s: No handler for data_type = %d  "
                       "Surrounding bytes = %02x%02x%08x\n",
                       __func__, data_type, data_type, data_channel, size);
                       
                // Would like to use avio_skip, but that needs seek support, 
                // so just read the data into a buffer then throw it away
                tempbuf = av_malloc(size);                
                avio_read(pb, tempbuf, size);
                av_free(tempbuf);
                
                return ADFFMPEG_AD_ERROR_DEFAULT;
        }
    }

    if (errorVal >= 0)  {
        errorVal = ad_read_packet(s, pkt, data_channel, mediaType, codecId, payload, txtDat);
    }
    else  {
        av_log(s, AV_LOG_ERROR, "%s: Error %d creating packet\n", __func__, errorVal);

#ifdef AD_SIDEDATA_IN_PRIV
        // If there was an error, release any memory that has been allocated
        if (payload != NULL)
            av_freep(&payload);

        if( txtDat != NULL )
            av_freep(&txtDat);
#endif
    }

#ifndef AD_SIDEDATA_IN_PRIV
    if (payload != NULL)
        av_freep(&payload);

    if( txtDat != NULL )
        av_freep(&txtDat);
#endif

    return errorVal;
}

static int adbinary_read_close(AVFormatContext *s)
{
    return 0;
}


AVInputFormat ff_adbinary_demuxer = {
    .name           = "adbinary",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings video format (binary)"),
    .priv_data_size = sizeof(AdContext),
    .read_probe     = adbinary_probe,
    .read_header    = adbinary_read_header,
    .read_packet    = adbinary_read_packet,
    .read_close     = adbinary_read_close,
    .flags          = AVFMT_TS_DISCONT | AVFMT_VARIABLE_FPS | AVFMT_NO_BYTE_SEEK,
};
