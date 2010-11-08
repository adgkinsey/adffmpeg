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


static void audioheader_network2host( NetVuAudioData *hdr );


typedef struct {
    int utc_offset;
} AdbinaryContext;

/* CS - The following structure is used to extract 6 bytes from the incoming stream. It needs therefore to be aligned on a 
   2 byte boundary. Not sure whether this solution is portable though */
#pragma pack(push,2)
typedef struct _minimal_video_header	// PRC 002
{
	uint32_t            t;
	unsigned short      ms;
}MinimalVideoHeader;
#pragma pack(pop) /* Revert to the old alignment */

union _minimal_video	// PRC 002
{
	struct _minimal_video_header header;
	char payload[sizeof(uint32_t)+sizeof(unsigned short)];
};

typedef struct _minimal_audio_header	// PRC 002
{
	uint32_t t;
	unsigned short ms;
	unsigned short mode;
}MinimalAudioHeader;


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

/****************************************************************************************************************
 * Function: adbinary_probe
 * Desc: used to identify the stream as an ad stream 
 * Params: 
 * Return:
 *  AVPROBE_SCORE_MAX if this straem is identifide as a ad stream 0 if not 
 ****************************************************************************************************************/
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
             (dataType == DATA_H264I)  || (dataType == DATA_H264P)  )
        {
            if (p->buf_size >= (SEPARATOR_SIZE + sizeof(NetVuImageData)) )  {
                NetVuImageData test;
                memcpy(&test, dataPtr, sizeof(NetVuImageData));
                ad_network2host(&test);
                if (pic_version_valid(test.version))
                    return AVPROBE_SCORE_MAX;
            }
        }
        else if (dataType == DATA_JFIF)  {
            unsigned char *dataPtr = &p->buf[SEPARATOR_SIZE];
            if ( (*dataPtr == 0xFF) && (*(dataPtr+1) == 0xD8) )
                return AVPROBE_SCORE_MAX;
        }
        else if (dataType == DATA_AUDIO_ADPCM)  {
            if (p->buf_size >= (SEPARATOR_SIZE + sizeof(NetVuAudioData)) )  {
                NetVuAudioData test;
                memcpy(&test, dataPtr, sizeof(NetVuAudioData));
                audioheader_network2host(&test);
                if (test.version == 0x00ABCDEF)
                    return AVPROBE_SCORE_MAX;
            }
        }
        else if (dataType == DATA_AUDIO_RAW)  {
            // We don't handle this format
            return 0;
        }
        else if (dataType == DATA_MINIMAL_MPEG4)  {
            if (p->buf_size >= (SEPARATOR_SIZE + sizeof(MinimalVideoHeader)) )  {
                MinimalVideoHeader test;
                memcpy(&test, dataPtr, sizeof(MinimalVideoHeader));
                test.t  = be2me_32(test.t);
                // If timestamp is between 1980 and 2020 then accept it
                if ( (test.t > 315532800) && (test.t < 1577836800) )
                    return AVPROBE_SCORE_MAX;
            }
        }
        else if (dataType == DATA_MINIMAL_AUDIO_ADPCM)  {
            if (p->buf_size >= (SEPARATOR_SIZE + sizeof(MinimalAudioHeader)) )  {
                MinimalAudioHeader test;
                memcpy(&test, dataPtr, sizeof(MinimalAudioHeader));
                test.t     = be2me_32(test.t);
                test.ms    = be2me_16(test.ms);
                test.mode  = be2me_16(test.mode);

                // If timestamp is between 1980 and 2020 then accept it
                if ( (test.t > 315532800) && (test.t < 1577836800) )  {
                    if ( (test.mode == RTP_PAYLOAD_TYPE_8000HZ_ADPCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_11025HZ_ADPCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_16000HZ_ADPCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_22050HZ_ADPCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_32000HZ_ADPCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_44100HZ_ADPCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_48000HZ_ADPCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_8000HZ_PCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_11025HZ_PCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_16000HZ_PCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_22050HZ_PCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_32000HZ_PCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_44100HZ_PCM) ||
                         (test.mode == RTP_PAYLOAD_TYPE_48000HZ_PCM) )
                    {
                        return AVPROBE_SCORE_MAX;
                    }
                }
            }
        }
        else if (dataType == DATA_LAYOUT)  {
            return AVPROBE_SCORE_MAX;
        }
        else if (dataType == DATA_INFO)  {
            // Payload is a type byte followed by a string
            return AVPROBE_SCORE_MAX;
        }
        else if (dataType == DATA_XML_INFO)  {
            const char *infoString = "<infoList>";
            int infoStringLen = strlen(infoString);
            if (p->buf_size >= (SEPARATOR_SIZE + infoStringLen) )  {
                if (strncasecmp(dataPtr, infoString, infoStringLen) == 0)
                    return AVPROBE_SCORE_MAX;
            }
        }
	}

    return 0;
}

static int adbinary_read_close(AVFormatContext *s)
{
    return 0;
}

static int adbinary_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
	AdbinaryContext *adpicContext = s->priv_data;
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return ad_read_header(s, ap, &adpicContext->utc_offset);
}

static int adbinary_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
	AdbinaryContext*		adpicContext = s->priv_data;
    ByteIOContext *         pb = s->pb;
    NetVuAudioData *        audio_data = NULL;
    NetVuImageData *        video_data = NULL;
    char *                  text_data = NULL;
    unsigned char           adpkt[SEPARATOR_SIZE];
    int                     data_type = MAX_DATA_TYPE;
    int                     data_channel = 0;
    int                     n, size = -1;
    int                     status;
    int                     errorVal = ADPIC_UNKNOWN_ERROR;
    FrameType               currentFrameType = FrameTypeUnknown;
    
    // First read the 6 byte separator
	if ((n=get_buffer(pb, &adpkt[0], SEPARATOR_SIZE)) != SEPARATOR_SIZE)
	{
        if(url_feof(pb))
        {
            errorVal = ADPIC_END_OF_STREAM;
        }
        else
        {
            av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading seperator, expected %d, read %d\n", SEPARATOR_SIZE, n);
            errorVal = ADPIC_READ_6_BYTE_SEPARATOR_ERROR;
        }
		goto cleanup;
	}

    // Get info out of the separator
    memcpy(&size, &adpkt[DATA_SIZE_BYTE_0], 4);
    size = be2me_32(size);
    data_type = adpkt[DATA_TYPE];
    data_channel = (adpkt[DATA_CHANNEL]+1);

    // Prepare for video or audio read
    if( (data_type == DATA_JPEG)          || (data_type == DATA_JFIF)   || 
        (data_type == DATA_MPEG4I)        || (data_type == DATA_MPEG4P) ||
        (data_type == DATA_MINIMAL_MPEG4) || 
        (data_type == DATA_H264I)         || (data_type == DATA_H264P)  )
    {
        currentFrameType = NetVuVideo;

        video_data = av_malloc( sizeof(NetVuImageData) );

        if( video_data == NULL )
        {
            errorVal = ADPIC_NETVU_IMAGE_DATA_ERROR;
            goto cleanup;
        }
    }
    else if ( (data_type == DATA_AUDIO_ADPCM) || (data_type == DATA_MINIMAL_AUDIO_ADPCM) )
    {
        currentFrameType = NetVuAudio;

        audio_data = av_malloc( sizeof(NetVuAudioData) );

        if( audio_data == NULL )
        {
            errorVal = ADPIC_NETVU_AUDIO_DATA_ERROR;
            goto cleanup;
        }
    }
    else if ( (data_type == DATA_INFO) || (data_type == DATA_XML_INFO) )
    {
        currentFrameType = NetVuDataInfo;
    }
    else if( data_type == DATA_LAYOUT )
    {
        currentFrameType = NetVuDataLayout;
    }

    // Proceed based on the type of data in this frame
    switch(data_type)
    {
        case DATA_JPEG:
            errorVal = ad_read_jpeg(s, pb, pkt, video_data, &text_data);
            if (errorVal < 0)
                goto cleanup;
            break;

        case DATA_JFIF:
            errorVal = ad_read_jfif(s, pb, pkt, FALSE, size, 
                                       video_data, &text_data);
            if (errorVal < 0)
                goto cleanup;
            break;

        case DATA_MPEG4I:
        case DATA_MPEG4P:
        case DATA_H264I:
        case DATA_H264P:
	    {
            if ((n=ad_get_buffer(pb, (unsigned char*)video_data, sizeof (NetVuImageData))) != sizeof (NetVuImageData))
            {
                av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading pic struct, expected %d, read %d\n", sizeof (NetVuImageData), n);
                errorVal = ADPIC_MPEG4_GET_BUFFER_ERROR;
                goto cleanup;
            }
            ad_network2host(video_data);
            if (!pic_version_valid(video_data->version))
            {
                av_log(s, AV_LOG_ERROR, "ADPIC: invalid pic version 0x%08X\n", video_data->version);
                errorVal = ADPIC_MPEG4_PIC_VERSION_VALID_ERROR;
                goto cleanup;
            }

            /* Get the additional text block */
            text_data = av_malloc( video_data->start_offset + 1 );

            if( text_data == NULL )
            {
                errorVal = ADPIC_MPEG4_ALOCATE_TEXT_BUFFER_ERROR;
                goto cleanup;
            }

            /* Copy the additional text block */
            if( (n = ad_get_buffer( pb, text_data, video_data->start_offset )) != video_data->start_offset )
            {
                av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading start_offset data, expected %d, read %d\n", size, n);
                errorVal = ADPIC_MPEG4_GET_TEXT_BUFFER_ERROR;
                goto cleanup;
            }

            /* CS - somtimes the buffer seems to end with a NULL terminator, other times it doesn't. I'm adding a NULL temrinator here regardless */
            text_data[video_data->start_offset] = '\0';

            if ((status = ad_new_packet(pkt, video_data->size))<0) // PRC 003
            {
                av_log(s, AV_LOG_ERROR, "ADPIC: DATA_MPEG4 ad_new_packet %d failed, status %d\n", video_data->size, status);
                errorVal = ADPIC_MPEG4_NEW_PACKET_ERROR;
                goto cleanup;
            }
            if ((n=ad_get_buffer(pb, pkt->data, video_data->size)) != video_data->size)
            {
                av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading pic body, expected %d, read %d\n", video_data->size, n);
                errorVal = ADPIC_MPEG4_PIC_BODY_ERROR;
                goto cleanup;
            }
	    }	
	    break;

        /* CS - Support for minimal MPEG4 */
        case DATA_MINIMAL_MPEG4:
        {
            MinimalVideoHeader      videoHeader;
            int                     dataSize = size - sizeof(MinimalVideoHeader);

            /* Get the minimal video header */
	        if( (n = ad_get_buffer( pb, (unsigned char*)&videoHeader, sizeof(MinimalVideoHeader) )) != sizeof(MinimalVideoHeader) )
            {
                errorVal = ADPIC_MPEG4_MINIMAL_GET_BUFFER_ERROR;
		        goto cleanup;
            }

            videoHeader.t  = be2me_32(videoHeader.t);
            videoHeader.ms = be2me_16(videoHeader.ms);

            /* Copy pertinent data into generic video data structure */
            memset(video_data, 0, sizeof(NetVuImageData));
            video_data->cam = data_channel;
            video_data->title[0] = 'C';
            video_data->title[1] = 'a';
            video_data->title[2] = 'm';
            video_data->title[3] = 'e';
            video_data->title[4] = 'r';
            video_data->title[5] = 'a';
            video_data->title[6] = '\n';
            video_data->session_time = videoHeader.t;
            video_data->milliseconds = videoHeader.ms;
			video_data->utc_offset = adpicContext->utc_offset;
			
            /* Remember to identify the type of frame data we have - this ensures the right codec is used to decode this frame */
            video_data->vid_format = PIC_MODE_MPEG4_411;

            /* Now get the main frame data into a new packet */
	        if( (status = ad_new_packet( pkt, dataSize )) < 0 )
            {
                errorVal = ADPIC_MPEG4_MINIMAL_NEW_PACKET_ERROR;
		        goto cleanup;
            }

	        if( (n = ad_get_buffer( pb, pkt->data, dataSize )) != dataSize )
            {
                errorVal = ADPIC_MPEG4_MINIMAL_NEW_PACKET_ERROR2;
		        goto cleanup;
            }
        }
        break;

        /* Support for minimal audio frames */
        case DATA_MINIMAL_AUDIO_ADPCM:
        {
            MinimalAudioHeader      audioHeader;
            int                     dataSize = size - sizeof(MinimalAudioHeader);

            /* Get the minimal video header */
	        if( (n = ad_get_buffer( pb, (unsigned char*)&audioHeader, sizeof(MinimalAudioHeader) )) != sizeof(MinimalAudioHeader) )
            {
                errorVal = ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR;
		        goto cleanup;
            }

            audioHeader.t    = be2me_32(audioHeader.t);
            audioHeader.ms   = be2me_16(audioHeader.ms);
            audioHeader.mode = be2me_16(audioHeader.mode);

            /* Copy pertinent data into generic audio data structure */
            memset(audio_data, 0, sizeof(NetVuAudioData));
            audio_data->mode = audioHeader.mode;
            audio_data->seconds = audioHeader.t;
            audio_data->msecs = audioHeader.ms;

            /* Now get the main frame data into a new packet */
	        if( (status = ad_new_packet( pkt, dataSize )) < 0 )
            {
                errorVal = ADPIC_MINIMAL_AUDIO_ADPCM_NEW_PACKET_ERROR;
		        goto cleanup;
            }

	        if( (n = ad_get_buffer( pb, pkt->data, dataSize )) != dataSize )
            {
                errorVal = ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR2;
		        goto cleanup;
            }
        }
        break;

        // CS - Support for ADPCM frames
        case DATA_AUDIO_ADPCM:
        {
            // Get the fixed size portion of the audio header
            size = sizeof(NetVuAudioData) - sizeof(unsigned char *);
            if( (n = ad_get_buffer( pb, (unsigned char*)audio_data, size)) != size)
            {
                errorVal = ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR;
                goto cleanup;
            }

            // endian fix it...
            audioheader_network2host( audio_data );

            // Now get the additional bytes
            if( audio_data->sizeOfAdditionalData > 0 )
            {
                audio_data->additionalData = av_malloc( audio_data->sizeOfAdditionalData );
                if( audio_data->additionalData == NULL )
                {
                    errorVal = ADPIC_AUDIO_ADPCM_ALOCATE_ADITIONAL_ERROR;
                    goto cleanup;
                }

                //ASSERT(audio_data->additionalData);

                if( (n = ad_get_buffer( pb, audio_data->additionalData, audio_data->sizeOfAdditionalData )) != audio_data->sizeOfAdditionalData )
                {
                    errorVal = ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR2;
                    goto cleanup;
                }
            }
            else
                audio_data->additionalData = NULL;

		    if( (status = ad_new_packet( pkt, audio_data->sizeOfAudioData )) < 0 )
            {
                errorVal = ADPIC_AUDIO_ADPCM_MIME_NEW_PACKET_ERROR;
			    goto cleanup;
            }

            // Now get the actual audio data
            if( (n = ad_get_buffer( pb, pkt->data, audio_data->sizeOfAudioData)) != audio_data->sizeOfAudioData )
            { 
                errorVal = ADPIC_AUDIO_ADPCM_MIME_GET_BUFFER_ERROR;
                goto cleanup;
            }
        }
        break;

        // CS - Data info extraction. A DATA_INFO frame simply contains a 
        // type byte followed by a text block. Due to its simplicity, we'll 
        // just extract the whole block into the AVPacket's data structure and 
        // let the client deal with checking its type and parsing its text
        case DATA_INFO:
            errorVal = ad_read_info(s, pb, pkt, size);
            if (errorVal < 0)
                goto cleanup;
            break;

        case DATA_LAYOUT:
            errorVal = ad_read_layout(s, pb, pkt, size);
            if (errorVal < 0)
                goto cleanup;
            break;
        
        case DATA_XML_INFO:
            errorVal = ad_read_info(s, pb, pkt, size);
            if (errorVal < 0)
                goto cleanup;
            break;

        default:
        {
            av_log(s, AV_LOG_WARNING, "ADPIC: adbinary_read_packet, No handler for data_type=%d\n", data_type );
            errorVal = ADPIC_DEFAULT_ERROR;
	        goto cleanup;
        }
        break;
		
    }

    if (currentFrameType == NetVuAudio)
        errorVal = ad_read_packet(s, pb, pkt, currentFrameType, audio_data, text_data);
    else
        errorVal = ad_read_packet(s, pb, pkt, currentFrameType, video_data, text_data);
    

cleanup:
    if( errorVal < 0 )
    {
        /* If there was an error, make sure we release any memory that might have been allocated */
        if( video_data != NULL )
            av_free( video_data );

        if( audio_data != NULL )
            av_free( audio_data );

        if( text_data != NULL )
            av_free( text_data );
    }

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
