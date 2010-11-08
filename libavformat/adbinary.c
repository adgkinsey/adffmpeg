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

#include <strings.h>

#include "avformat.h"
#include "libavutil/bswap.h"
//#include "libavutil/avstring.h"

#include "adpic.h"
#include "jfif_img.h"
//#include "netvu.h"
#include "ds_exports.h"


static void audioheader_network2host( NetVuAudioData *hdr );
static int skipInfoList(ByteIOContext * pb);
static int findTag(const char *tag, ByteIOContext *pb, int lookAhead, int *read);


#define TEMP_BUFFER_SIZE        1024
 
#define BINARY_PUSH_MIME_STR    "video/adhbinary"

/* These are the data types that are supported by the DS2 video servers. */
enum data_type { DATA_JPEG, DATA_JFIF, DATA_MPEG4I, DATA_MPEG4P, DATA_AUDIO_ADPCM, DATA_AUDIO_RAW, DATA_MINIMAL_MPEG4, DATA_MINIMAL_AUDIO_ADPCM, DATA_LAYOUT, DATA_INFO, DATA_H264I, DATA_H264P, DATA_XML_INFO, MAX_DATA_TYPE };
#define DATA_PLAINTEXT              (MAX_DATA_TYPE + 1)   /* This value is only used internally within the library DATA_PLAINTEXT blocks should not be exposed to the client */

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
    if (p->buf_size <= 6)
        return 0;

    // CS - There is no way this scheme of identification is strong enough. Probably should attempt
    // to parse a full frame out of the probedata buffer
    //0 DATA_JPEG, 
    //1 DATA_JFIF, 
    //2 DATA_MPEG4I, 
    //3 DATA_MPEG4P, 
    //4 DATA_AUDIO_ADPCM, 
    //5 DATA_AUDIO_RAW, 
    //6 DATA_MINIMAL_MPEG4, 
    //7 DATA_MINIMAL_AUDIO_ADPCM, 
    //8 DATA_LAYOUT, 
    //9 DATA_INFO, 
    //10 DATA_H264I, 
    //11 DATA_H264P, 
    //12 DATA_XML_INFO
    
	if ((p->buf[DATA_TYPE] <= DATA_XML_INFO) && (p->buf[DATA_CHANNEL] <= 32))  {
		unsigned long dataSize = (p->buf[DATA_SIZE_BYTE_0] << 24) + 
								 (p->buf[DATA_SIZE_BYTE_1] << 16) + 
								 (p->buf[DATA_SIZE_BYTE_2] << 8 ) + 
								 p->buf[DATA_SIZE_BYTE_3];
		if (dataSize <= 0xFFFF)  {
			return AVPROBE_SCORE_MAX;
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
    AVStream *              st = NULL;
    FrameData *             frameData = NULL;
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
    int                     manual_size = FALSE;
	int                     result = 0;
    
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

	result = skipInfoList(pb);
	if(result<0)
	{
		errorVal = ADPIC_FAILED_TO_PARSE_INFOLIST;
		goto cleanup;
	}
	else if(result>0)
	{
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
	}

    // Get info out of the separator
    memcpy(&size, &adpkt[DATA_SIZE_BYTE_0], 4);
    size = be2me_32(size);
    data_type = adpkt[DATA_TYPE];
    data_channel = (adpkt[DATA_CHANNEL]+1);

    // Prepare for video or audio read
    if( data_type == DATA_JPEG || data_type == DATA_JFIF || data_type == DATA_MPEG4I || data_type == DATA_MPEG4P 
        || data_type == DATA_MINIMAL_MPEG4 || data_type == DATA_H264I || data_type == DATA_H264P )
    {
        currentFrameType = NetVuVideo;

        video_data = av_mallocz( sizeof(NetVuImageData) );

        if( video_data == NULL )
        {
            errorVal = ADPIC_NETVU_IMAGE_DATA_ERROR;
            goto cleanup;
        }
    }
    else if( data_type == DATA_AUDIO_ADPCM || data_type == DATA_MINIMAL_AUDIO_ADPCM )
    {
        currentFrameType = NetVuAudio;

        audio_data = av_mallocz( sizeof(NetVuAudioData) );

        if( audio_data == NULL )
        {
            errorVal = ADPIC_NETVU_AUDIO_DATA_ERROR;
            goto cleanup;
        }
    }
    else if( data_type == DATA_INFO )
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
	    {
	        int header_size;
	        char jfif[2048], *ptr;
		    // Read the pic structure
		    if ((n=adpic_get_buffer(pb, (unsigned char*)video_data, sizeof (NetVuImageData))) != sizeof (NetVuImageData))
		    {
                av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading pic struct, expected %d, read %d\n", sizeof (NetVuImageData), n);
                errorVal = ADPIC_JPEG_IMAGE_DATA_READ_ERROR;
			    goto cleanup;
		    }
		    // Endian convert if necessary
		    adpic_network2host(video_data);
		    if (!pic_version_valid(video_data->version))
		    {
                av_log(s, AV_LOG_ERROR, "ADPIC: invalid pic version 0x%08X\n", video_data->version);
                errorVal = ADPIC_JPEG_PIC_VERSION_ERROR;
			    goto cleanup;
		    }
            
            /* Get the additional text block */
            text_data = av_malloc( video_data->start_offset + 1 );
            if( text_data == NULL )
            {
                errorVal = ADPIC_JPEG_ALOCATE_TEXT_BLOCK_ERROR;
                goto cleanup;
            }

		    /* Copy the additional text block */
		    if( (n = adpic_get_buffer( pb, text_data, video_data->start_offset )) != video_data->start_offset )
		    {
                av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading start_offset data, expected %d, read %d\n", size, n);
                errorVal = ADPIC_JPEG_READ_TEXT_BLOCK_ERROR;
			    goto cleanup;
		    }

            /* CS - somtimes the buffer seems to end with a NULL terminator, other times it doesn't. I'm adding a NULL temrinator here regardless */
            text_data[video_data->start_offset] = '\0';

		    // Use the pic struct to build a JFIF header 
		    if ((header_size = build_jpeg_header( jfif, video_data, FALSE, 2048))<=0)
		    {
                av_log(s, AV_LOG_ERROR, "ADPIC: adbinary_read_packet, build_jpeg_header failed\n");
                errorVal = ADPIC_JPEG_HEADER_ERROR;
			    goto cleanup;
		    }
		    // We now know the packet size required for the image, allocate it.
		    if ((status = adpic_new_packet(pkt, header_size+video_data->size+2))<0) // PRC 003
		    {
                errorVal = ADPIC_JPEG_NEW_PACKET_ERROR;
			    goto cleanup;
		    }
		    ptr = pkt->data;
		    // Copy the JFIF header into the packet
		    memcpy(ptr, jfif, header_size);
		    ptr += header_size;
		    // Now get the compressed JPEG data into the packet
		    // Read the pic structure
		    if ((n=adpic_get_buffer(pb, ptr, video_data->size)) != video_data->size)
		    {
                av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading pic body, expected %d, read %d\n", video_data->size, n);
                errorVal = ADPIC_JPEG_READ_BODY_ERROR;
			    goto cleanup;
		    }
		    ptr += video_data->size;
		    // Add the EOI marker
		    *ptr++ = 0xff;
		    *ptr++ = 0xd9;
	    }
	    break;

        case DATA_JFIF:
	    {
            if(!manual_size)
            {
		        if ((status = adpic_new_packet(pkt, size))<0) // PRC 003
		        {
                    av_log(s, AV_LOG_ERROR, "ADPIC: DATA_JFIF adpic_new_packet %d failed, status %d\n", size, status);
                    errorVal = ADPIC_JFIF_NEW_PACKET_ERROR;
			        goto cleanup;
		        }

		        if ((n=adpic_get_buffer(pb, pkt->data, size)) != size)
		        {
                    av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading jfif image, expected %d, read %d\n", size, n);
                    errorVal = ADPIC_JFIF_GET_BUFFER_ERROR;
			        goto cleanup;
		        }
            }
		    if ( parse_jfif_header( pkt->data, video_data, size, NULL, NULL, NULL, TRUE, &text_data ) <= 0)
		    {
                av_log(s, AV_LOG_ERROR, "ADPIC: adbinary_read_packet, parse_jfif_header failed\n");
                errorVal = ADPIC_JFIF_MANUAL_SIZE_ERROR;
			    goto cleanup;
		    }
	    }
	    break;

        case DATA_MPEG4I:
        case DATA_MPEG4P:
        case DATA_H264I:
        case DATA_H264P:
	    {
            if ((n=adpic_get_buffer(pb, (unsigned char*)video_data, sizeof (NetVuImageData))) != sizeof (NetVuImageData))
            {
                av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading pic struct, expected %d, read %d\n", sizeof (NetVuImageData), n);
                errorVal = ADPIC_MPEG4_GET_BUFFER_ERROR;
                goto cleanup;
            }
            adpic_network2host(video_data);
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
            if( (n = adpic_get_buffer( pb, text_data, video_data->start_offset )) != video_data->start_offset )
            {
                av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading start_offset data, expected %d, read %d\n", size, n);
                errorVal = ADPIC_MPEG4_GET_TEXT_BUFFER_ERROR;
                goto cleanup;
            }

            /* CS - somtimes the buffer seems to end with a NULL terminator, other times it doesn't. I'm adding a NULL temrinator here regardless */
            text_data[video_data->start_offset] = '\0';

            if ((status = adpic_new_packet(pkt, video_data->size))<0) // PRC 003
            {
                av_log(s, AV_LOG_ERROR, "ADPIC: DATA_MPEG4 adpic_new_packet %d failed, status %d\n", video_data->size, status);
                errorVal = ADPIC_MPEG4_NEW_PACKET_ERROR;
                goto cleanup;
            }
            if ((n=adpic_get_buffer(pb, pkt->data, video_data->size)) != video_data->size)
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
	        if( (n = adpic_get_buffer( pb, (unsigned char*)&videoHeader, sizeof(MinimalVideoHeader) )) != sizeof(MinimalVideoHeader) )
            {
                errorVal = ADPIC_MPEG4_MINIMAL_GET_BUFFER_ERROR;
		        goto cleanup;
            }

            videoHeader.t  = be2me_32(videoHeader.t);
            videoHeader.ms = be2me_16(videoHeader.ms);

            /* Copy pertinent data into generic video data structure */
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
	        if( (status = adpic_new_packet( pkt, dataSize )) < 0 )
            {
                errorVal = ADPIC_MPEG4_MINIMAL_NEW_PACKET_ERROR;
		        goto cleanup;
            }

	        if( (n = adpic_get_buffer( pb, pkt->data, dataSize )) != dataSize )
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
	        if( (n = adpic_get_buffer( pb, (unsigned char*)&audioHeader, sizeof(MinimalAudioHeader) )) != sizeof(MinimalAudioHeader) )
            {
                errorVal = ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR;
		        goto cleanup;
            }

            audioHeader.t    = be2me_32(audioHeader.t);
            audioHeader.ms   = be2me_16(audioHeader.ms);
            audioHeader.mode = be2me_16(audioHeader.mode);

            /* Copy pertinent data into generic audio data structure */
            audio_data->mode = audioHeader.mode;
            audio_data->seconds = audioHeader.t;
            audio_data->msecs = audioHeader.ms;

            /* Now get the main frame data into a new packet */
	        if( (status = adpic_new_packet( pkt, dataSize )) < 0 )
            {
                errorVal = ADPIC_MINIMAL_AUDIO_ADPCM_NEW_PACKET_ERROR;
		        goto cleanup;
            }

	        if( (n = adpic_get_buffer( pb, pkt->data, dataSize )) != dataSize )
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
            if( (n = adpic_get_buffer( pb, (unsigned char*)audio_data, sizeof(NetVuAudioData) - 4 )) != sizeof(NetVuAudioData) - 4 )
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

                if( (n = adpic_get_buffer( pb, audio_data->additionalData, audio_data->sizeOfAdditionalData )) != audio_data->sizeOfAdditionalData )
                {
                    errorVal = ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR2;
                    goto cleanup;
                }
            }

		    if( (status = adpic_new_packet( pkt, audio_data->sizeOfAudioData )) < 0 )
            {
                errorVal = ADPIC_AUDIO_ADPCM_MIME_NEW_PACKET_ERROR;
			    goto cleanup;
            }

            // Now get the actual audio data
            if( (n = adpic_get_buffer( pb, pkt->data, audio_data->sizeOfAudioData)) != audio_data->sizeOfAudioData )
            { 
                errorVal = ADPIC_AUDIO_ADPCM_MIME_GET_BUFFER_ERROR;
                goto cleanup;
            }
        }
        break;

        // CS - Data info extraction. A DATA_INFO frame simply contains a type byte followed by a text block. Due to its simplicity,
        // we'll just extract the whole block into the AVPacket's data structure and let the client deal with checking its type and
        // parsing its text
        case DATA_INFO:
        {
            // Allocate a new packet
            if( (status = adpic_new_packet( pkt, size )) < 0 )
            {
                errorVal = ADPIC_INFO_NEW_PACKET_ERROR;
                goto cleanup;
            }

            // Get the data
            if( (n = adpic_get_buffer( pb, pkt->data, size)) != size )
            {
                errorVal = ADPIC_INFO_GET_BUFFER_ERROR;
			    goto cleanup;
            }
        }
        break;

        case DATA_LAYOUT:
        {
            /* Allocate a new packet */
            if( (status = adpic_new_packet( pkt, size )) < 0 )
            {
                errorVal = ADPIC_LAYOUT_NEW_PACKET_ERROR;
                goto cleanup;
            }

            /* Get the data */
            if( (n = adpic_get_buffer( pb, pkt->data, size)) != size )
            {
                errorVal = ADPIC_LAYOUT_GET_BUFFER_ERROR;
			    goto cleanup;
            }
        }
        break;

        default:
        {
            av_log(s, AV_LOG_WARNING, "ADPIC: adbinary_read_packet, No handler for data_type=%d\n", data_type );
            errorVal = ADPIC_DEFAULT_ERROR;
	        goto cleanup;
        }
        break;
		
    }

    if( currentFrameType == NetVuVideo )
    {
	    // At this point We have a legal pic structure which we use to determine 
	    // which codec stream to use
	    if ( (st = ad_get_stream( s, video_data)) == NULL )
	    {
            av_log(s, AV_LOG_ERROR, "ADPIC: adbinary_read_packet, failed get_stream for video\n");
            errorVal = ADPIC_GET_STREAM_ERROR;
		    goto cleanup;
	    }
		else  {
			if (video_data->session_time > 0)  {
				pkt->pts = video_data->session_time;
				pkt->pts *= 1000ULL;
				pkt->pts += video_data->milliseconds % 1000;
			}
			else
				pkt->pts = AV_NOPTS_VALUE;
		}
    }
    else if( currentFrameType == NetVuAudio )
    {
        // Get the audio stream
        if ( (st = ad_get_audio_stream( s, audio_data )) == NULL )
        {
            av_log(s, AV_LOG_ERROR, "ADPIC: adbinary_read_packet, failed get_stream for audio\n");
            errorVal = ADPIC_GET_AUDIO_STREAM_ERROR;
		    goto cleanup;
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
    else if( currentFrameType == NetVuDataInfo || currentFrameType == NetVuDataLayout )
    {
        // Get or create a data stream
        if ( (st = ad_get_data_stream( s )) == NULL )
        {
            errorVal = ADPIC_GET_INFO_LAYOUT_STREAM_ERROR;
            goto cleanup;
        }
    }

    pkt->stream_index = st->index;

    frameData = av_malloc(sizeof(FrameData));

    if( frameData == NULL )
        goto cleanup;

    frameData->additionalData = NULL;
    frameData->frameType = currentFrameType;

    if( frameData->frameType == NetVuVideo )                // Video frame
    {
        frameData->frameData = video_data;
    }
    else if( frameData->frameType == NetVuAudio )           // Audio frame
    {
        frameData->frameData = audio_data;
    }
    else if( frameData->frameType == NetVuDataInfo || frameData->frameType == NetVuDataLayout )        // Data info
    {
        frameData->frameData = NULL;
    }
    else  // Shouldn't really get here...
    {
        frameData->frameType = FrameTypeUnknown;
        frameData->frameData = NULL;
    }

    if( (frameData->frameType == NetVuAudio || frameData->frameType == NetVuVideo) && text_data != NULL )
    {
        frameData->additionalData = text_data;
    }

    pkt->priv = frameData;

	pkt->duration = 0;

    errorVal = ADPIC_NO_ERROR;

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

static int skipInfoList(ByteIOContext * pb)
{
	int  ch;
    int read = 0;
	
	if(findTag("infolist", pb, 0, &read))
	{
		if(findTag("/infolist", pb, (4*1024), &read))
		{ 
			ch = url_fgetc(pb);
			if(ch==0x0D)//'/r'
			{ 
                ch = url_fgetc(pb);
				if(ch==0x0A)//'/n'
				{
					ch = url_fgetc(pb);
					return 1; 
				}
			}
		}
	}
	else
	{ 
		url_fseek(pb, -read, SEEK_CUR);
		return 0;
	}

	return -1;
}

static int findTag(const char *tag, ByteIOContext *pb, int lookAhead, int *read)
{
	//NOTE Looks for the folowing pattern "<infoList>" at the begining of the 
	//     buffer return 1 if its found and 0 if not

    int                         LookAheadPos = 0;
    unsigned char               buffer[TEMP_BUFFER_SIZE];
    unsigned char               *q = buffer;
    int                         ch;
	int                         endOfTag = 0;
    
    if(lookAhead < 0)
        lookAhead = 0;

	for(LookAheadPos=0; LookAheadPos <= lookAhead; LookAheadPos++)  {
		endOfTag = 0;
        ch = url_fgetc( pb );
        ++(*read);
        
        if (ch < 0)
        { return 0; }

        if (ch == '<' )
		{			
			while(endOfTag==0)
			{
				ch = url_fgetc( pb );
                ++(*read);

                if (ch < 0)
                { return 0; }

				if(ch == '>')
				{
					endOfTag = 1;
                    *q = '\0';
	                q = buffer;

					if(strcasecmp(buffer, tag)==0)
						return 1;
					else
						endOfTag = 1; 
				}
				else
				{
					if ((q - buffer) < sizeof(buffer) - 1)
			        { *q++ = ch; }
				}
			}
		}
    }
 
	return 0;
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
