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

#include "libavutil/bswap.h"
#include "libavutil/avstring.h"
#include "adpic.h"
#include "jfif_img.h"
#include "netvu.h"

 
int ad_read_header(AVFormatContext *s, AVFormatParameters *ap, int *utcOffset)
{
    ByteIOContext*	    pb = s->pb;
	URLContext*		    urlContext = pb->opaque;
	NetvuContext*	    netvu = NULL;
	
	if (urlContext && urlContext->is_streamed)  {
		if ( av_stristart(urlContext->filename, "netvu://", NULL) == 1)
			netvu = urlContext->priv_data;
	}
	if (netvu)  {
        char temp[12];
        
        if (utcOffset)
            *utcOffset = netvu->utc_offset;
		av_metadata_set2(&s->metadata, "server",		netvu->server, 		0);
		av_metadata_set2(&s->metadata, "content",		netvu->content, 	0);
		av_metadata_set2(&s->metadata, "resolution",	netvu->resolution, 	0);
		av_metadata_set2(&s->metadata, "compression",	netvu->compression, 0);
		av_metadata_set2(&s->metadata, "rate",			netvu->rate, 		0);
		av_metadata_set2(&s->metadata, "pps",			netvu->pps, 		0);
		av_metadata_set2(&s->metadata, "site_id",		netvu->site_id, 	0);
		//av_metadata_set2(&s->metadata, "boundry",		netvu->boundry, 	0);
        snprintf(temp, sizeof(temp), "%d", netvu->utc_offset);
        av_metadata_set2(&s->metadata, "timezone",		temp,               0);
	}
    
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return 0;
}

void ad_network2host(NetVuImageData *pic)
{
	pic->version				= be2me_32(pic->version);
	pic->mode					= be2me_32(pic->mode);
	pic->cam					= be2me_32(pic->cam);
	pic->vid_format				= be2me_32(pic->vid_format);
	pic->start_offset			= be2me_32(pic->start_offset);
	pic->size					= be2me_32(pic->size);
	pic->max_size				= be2me_32(pic->max_size);
	pic->target_size			= be2me_32(pic->target_size);
	pic->factor					= be2me_32(pic->factor);
	pic->alm_bitmask_hi			= be2me_32(pic->alm_bitmask_hi);
	pic->status					= be2me_32(pic->status);
	pic->session_time			= be2me_32(pic->session_time);
	pic->milliseconds			= be2me_32(pic->milliseconds);
	pic->utc_offset				= be2me_32(pic->utc_offset);
	pic->alm_bitmask			= be2me_32(pic->alm_bitmask);
	pic->format.src_pixels		= be2me_16(pic->format.src_pixels);
	pic->format.src_lines		= be2me_16(pic->format.src_lines);
	pic->format.target_pixels	= be2me_16(pic->format.target_pixels);
	pic->format.target_lines	= be2me_16(pic->format.target_lines);
	pic->format.pixel_offset	= be2me_16(pic->format.pixel_offset);
	pic->format.line_offset		= be2me_16(pic->format.line_offset);
}


AVStream * ad_get_stream(struct AVFormatContext *s, NetVuImageData *pic)
{
    int xres = pic->format.target_pixels;
    int yres = pic->format.target_lines;
    int codec_type, codec_id, id;
    int i, found;
	char textbuffer[4];
    AVStream *st;

	switch(pic->vid_format)
	{
	    case PIC_MODE_JPEG_422:
	    case PIC_MODE_JPEG_411:
		    codec_id = CODEC_ID_MJPEG;
		    codec_type = 0;
		break;

	    case PIC_MODE_MPEG4_411:
	    case PIC_MODE_MPEG4_411_I:
	    case PIC_MODE_MPEG4_411_GOV_P:
	    case PIC_MODE_MPEG4_411_GOV_I:
            codec_id = CODEC_ID_MPEG4;
		    codec_type = 1;
        break;

        case PIC_MODE_H264I:
        case PIC_MODE_H264P:
        case PIC_MODE_H264J:
		    codec_id = CODEC_ID_H264;
		    codec_type = 2;
		break;

	    default:
		    //logger(LOG_DEBUG,"ADPIC: get_stream, unrecognised vid_format %d\n", pic->vid_format);
		return NULL;
	}
	found = FALSE;
	id = (codec_type<<31)|((pic->cam-1)<<24)|((xres>>4)<<12)|((yres>>4)<<0);
	for (i=0; i<s->nb_streams; i++)
	{
		st = s->streams[i];
		if (st->id == id)
		{
			found = TRUE;
			break;
		}
	}
	if (!found)
	{
		st = av_new_stream( s, id);
		if (st)
		{
		    st->codec->codec_type = CODEC_TYPE_VIDEO;
		    st->codec->codec_id = codec_id;
			st->codec->width = pic->format.target_pixels;
			st->codec->height = pic->format.target_lines;
			st->index = i;
			
			// Set pixel aspect ratio, display aspect is (sar * width / height)
			// May get overridden by codec
			if( (st->codec->width  > 360) && (st->codec->height <= 480) )  {
				st->sample_aspect_ratio = (AVRational){1, 2};
			}
			else  {
				st->sample_aspect_ratio = (AVRational){1, 1};
			}
			
			// Use milliseconds as the time base
			st->r_frame_rate = (AVRational){1,1000};
			av_set_pts_info(st, 32, 1, 1000);
			st->codec->time_base = (AVRational){1,1000};
			
			av_metadata_set2(&st->metadata, "title", pic->title, 0);
			snprintf(textbuffer, sizeof(textbuffer), "%d", pic->cam);
			av_metadata_set2(&st->metadata, "camera", textbuffer, 0);
		}
	}
	return st;
}

AVStream * ad_get_audio_stream( struct AVFormatContext *s, NetVuAudioData* audioHeader )
{
    int id;
    int i, found;
    AVStream *st;

	found = FALSE;

	id = AUDIO_STREAM_ID;

	for( i = 0; i < s->nb_streams; i++ )
	{
		st = s->streams[i];
		if( st->id == id )
		{
			found = TRUE;
			break;
		}
	}

    // Did we find our audio stream? If not, create a new one
	if( !found )
	{
		st = av_new_stream( s, id );
		if (st)
		{
		    st->codec->codec_type = CODEC_TYPE_AUDIO;
		    st->codec->codec_id = CODEC_ID_ADPCM_ADH;
            st->codec->channels = 1;
            st->codec->block_align = 0;
            // Should probably fill in other known values here. Like bit rate etc.
			
			// Use milliseconds as the time base
			st->r_frame_rate = (AVRational){1,1000};
			av_set_pts_info(st, 32, 1, 1000);
			st->codec->time_base = (AVRational){1,1000};
			
			switch(audioHeader->mode)  {
				case(AUD_MODE_AUD_ADPCM_8000):
				case(AUD_MODE_AUD_L16_8000):
					st->codec->sample_rate = 8000;
					break;
				case(AUD_MODE_AUD_ADPCM_16000):
				case(AUD_MODE_AUD_L16_16000):
					st->codec->sample_rate = 16000;
					break;
				case(AUD_MODE_AUD_L16_44100):
				case(AUD_MODE_AUD_ADPCM_44100):
					st->codec->sample_rate = 441000;
					break;
				case(AUD_MODE_AUD_ADPCM_11025):
				case(AUD_MODE_AUD_L16_11025):
					st->codec->sample_rate = 11025;
					break;
				case(AUD_MODE_AUD_ADPCM_22050):
				case(AUD_MODE_AUD_L16_22050):
					st->codec->sample_rate = 22050;
					break;
				case(AUD_MODE_AUD_ADPCM_32000):
				case(AUD_MODE_AUD_L16_32000):
					st->codec->sample_rate = 32000;
					break;
				case(AUD_MODE_AUD_ADPCM_48000):
				case(AUD_MODE_AUD_L16_48000):
					st->codec->sample_rate = 48000;
					break;
				case(AUD_MODE_AUD_L16_12000):
					st->codec->sample_rate = 12000;
					break;
				case(AUD_MODE_AUD_L16_24000):
					st->codec->sample_rate = 24000;
					break;
				default:
					st->codec->sample_rate = 8000;
					break;
			}
			st->codec->codec_tag = 0x0012;

			st->index = i;
		}
	}

	return st;
}

/****************************************************************************************************************
 * Function: get_data_stream
 * Desc: Returns the data stream for associated with the current connection. If there isn't one already, a new 
 *       one will be created and added to the AVFormatContext passed in
 * Params: 
 *   s - Pointer to AVFormatContext associated with the active connection
 * Return:
 *  Pointer to the data stream on success, NULL on failure
 ****************************************************************************************************************/
AVStream * ad_get_data_stream( struct AVFormatContext *s ) 
{
    int id;
    int i, found;
    AVStream *st;

	found = FALSE;

	id = DATA_STREAM_ID;

	for( i = 0; i < s->nb_streams; i++ )
	{
		st = s->streams[i];
		if( st->id == id )
		{
			found = TRUE;
			break;
		}
	}

    // Did we find our audio stream? If not, create a new one
	if( !found )
	{
		st = av_new_stream( s, id );
		if (st)
		{
            st->codec->codec_type = CODEC_TYPE_DATA;
		    st->codec->codec_id = 0;
            st->codec->channels = 0;
            st->codec->block_align = 0;
            // Should probably fill in other known values here. Like bit rate, sample rate, num channels, etc

			st->index = i;
		}
	}

	return st;
}

int ad_new_packet(AVPacket *pkt, int size)
{
    int     retVal = av_new_packet( pkt, size );

    if( retVal >= 0 )
    {
        // Give the packet its own destruct function
        pkt->destruct = ad_release_packet;
    }

    return retVal;
}

void ad_release_packet( AVPacket *pkt )
{
    if( pkt != NULL )
    {
        if( pkt->priv != NULL )
        {
            // Have a look what type of frame we have and then delete anything inside as appropriate
            FrameData *     frameData = (FrameData *)pkt->priv;

            if( frameData->frameType == NetVuAudio )
            {
                NetVuAudioData *   audioHeader = (NetVuAudioData *)frameData->frameData;

                if( audioHeader->additionalData )
                {
                    av_free( audioHeader->additionalData );
                    audioHeader->additionalData = NULL;
                }
            }

            // Nothing else has nested allocs so just delete the frameData if it exists
            if( frameData->frameData != NULL )
            {
                av_free( frameData->frameData );
                frameData->frameData = NULL;
            }

            if( frameData->additionalData != NULL )
            {
                av_free( frameData->additionalData );
                frameData->additionalData = NULL;
            }
        }

        av_free( pkt->priv );
        pkt->priv = NULL;

        // Now use the default routine to release the rest of the packet's resources
        av_destruct_packet( pkt );
    }
}

int ad_get_buffer(ByteIOContext *s, unsigned char *buf, int size)
{
    int TotalDataRead = 0;
    int DataReadThisTime = 0;
    int RetryBoundry = 200;
    //clock_t TimeOut = clock () + (4.0 * CLOCKS_PER_SEC);
    int retrys = 0;

    //get data while ther is no time out and we still need data
    while(TotalDataRead < size && retrys<RetryBoundry)
    {
        DataReadThisTime += get_buffer(s, buf, (size-TotalDataRead));

        //if we retreave some data keep trying untill we get the required data or we have mutch longer time out 
        if(DataReadThisTime>0 && RetryBoundry<1000)
        {
            RetryBoundry += 10;   
        }

        TotalDataRead += DataReadThisTime;
        retrys++;
    }

    return TotalDataRead;
}

int initADData(int data_type, FrameType *frameType, 
               NetVuImageData **vidDat, NetVuAudioData **audDat)
{
    int errorVal = 0;
    
    if( (data_type == DATA_JPEG)          || (data_type == DATA_JFIF)   || 
        (data_type == DATA_MPEG4I)        || (data_type == DATA_MPEG4P) ||
        (data_type == DATA_H264I)         || (data_type == DATA_H264P)  ||
        (data_type == DATA_MINIMAL_MPEG4)                               )
    {
        *frameType = NetVuVideo;
        *vidDat = av_malloc( sizeof(NetVuImageData) );
        if( *vidDat == NULL )
        {
            errorVal = ADPIC_NETVU_IMAGE_DATA_ERROR;
            return errorVal;
        }
    }
    else if ( (data_type == DATA_AUDIO_ADPCM) || 
              (data_type == DATA_MINIMAL_AUDIO_ADPCM) )
    {
        *frameType = NetVuAudio;
        *audDat = av_malloc( sizeof(NetVuAudioData) );
        if( *audDat == NULL )
        {
            errorVal = ADPIC_NETVU_AUDIO_DATA_ERROR;
            return errorVal;
        }
    }
    else if ( (data_type == DATA_INFO) || (data_type == DATA_XML_INFO) )
        *frameType = NetVuDataInfo;
    else if( data_type == DATA_LAYOUT )
        *frameType = NetVuDataLayout;
    else
        *frameType = FrameTypeUnknown;
    
    return errorVal;
}

int ad_read_jpeg(AVFormatContext *s, ByteIOContext *pb, 
                    AVPacket *pkt, 
                    NetVuImageData *video_data, char **text_data)
{
    int header_size;
    char jfif[2048], *ptr;
    int n, errorVal = 0;
    int status;
    
    // Read the pic structure
    if ((n=ad_get_buffer(pb, (unsigned char*)video_data, sizeof (NetVuImageData))) != sizeof (NetVuImageData))
    {
        av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading pic struct, expected %d, read %d\n", sizeof (NetVuImageData), n);
        errorVal = ADPIC_JPEG_IMAGE_DATA_READ_ERROR;
        return errorVal;
    }
    // Endian convert if necessary
    ad_network2host(video_data);
    if (!pic_version_valid(video_data->version))
    {
        av_log(s, AV_LOG_ERROR, "ADPIC: invalid pic version 0x%08X\n", video_data->version);
        errorVal = ADPIC_JPEG_PIC_VERSION_ERROR;
        return errorVal;
    }
    
    /* Get the additional text block */
    *text_data = av_malloc( video_data->start_offset + 1 );
    if( *text_data == NULL )
    {
        av_log(s, AV_LOG_ERROR, "ADPIC: text_data allocation failed (%d bytes)", video_data->start_offset + 1);
        errorVal = ADPIC_JPEG_ALOCATE_TEXT_BLOCK_ERROR;
        return errorVal;
    }

    /* Copy the additional text block */
    if( (n = ad_get_buffer( pb, *text_data, video_data->start_offset )) != video_data->start_offset )
    {
        av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading start_offset data, expected %d, read %d\n", video_data->start_offset, n);
        errorVal = ADPIC_JPEG_READ_TEXT_BLOCK_ERROR;
        return errorVal;
    }

    /* CS - somtimes the buffer seems to end with a NULL terminator, other times it doesn't. I'm adding a NULL temrinator here regardless */
    (*text_data)[video_data->start_offset] = '\0';

    // Use the pic struct to build a JFIF header 
    if ((header_size = build_jpeg_header( jfif, video_data, FALSE, 2048))<=0)
    {
        av_log(s, AV_LOG_ERROR, "ADPIC: ad_read_packet, build_jpeg_header failed\n");
        errorVal = ADPIC_JPEG_HEADER_ERROR;
        return errorVal;
    }
    // We now know the packet size required for the image, allocate it.
    if ((status = ad_new_packet(pkt, header_size+video_data->size+2))<0) // PRC 003
    {
        av_log(s, AV_LOG_ERROR, "ADPIC: DATA_JPEG ad_new_packet %d failed, status %d\n", header_size+video_data->size+2, status);
        errorVal = ADPIC_JPEG_NEW_PACKET_ERROR;
        return errorVal;
    }
    ptr = pkt->data;
    // Copy the JFIF header into the packet
    memcpy(ptr, jfif, header_size);
    ptr += header_size;
    // Now get the compressed JPEG data into the packet
    // Read the pic structure
    if ((n=ad_get_buffer(pb, ptr, video_data->size)) != video_data->size)
    {
        av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading pic body, expected %d, read %d\n", video_data->size, n);
        errorVal = ADPIC_JPEG_READ_BODY_ERROR;
        return errorVal;
    }
    ptr += video_data->size;
    // Add the EOI marker
    *ptr++ = 0xff;
    *ptr++ = 0xd9;
    
    return errorVal;
}

int ad_read_jfif(AVFormatContext *s, ByteIOContext *pb, 
                 AVPacket *pkt, int imgLoaded, int size, 
                 NetVuImageData *video_data, char **text_data)
{
    int n, status, errorVal = 0;
    
    if(!imgLoaded)
    {
        if ((status = ad_new_packet(pkt, size))<0) // PRC 003
        {
            av_log(s, AV_LOG_ERROR, "ADPIC: DATA_JFIF ad_new_packet %d failed, status %d\n", size, status);
            errorVal = ADPIC_JFIF_NEW_PACKET_ERROR;
            return errorVal;
        }

        if ((n=ad_get_buffer(pb, pkt->data, size)) != size)
        {
            av_log(s, AV_LOG_ERROR, "ADPIC: short of data reading jfif image, expected %d, read %d\n", size, n);
            errorVal = ADPIC_JFIF_GET_BUFFER_ERROR;
            return errorVal;
        }
    }
    if ( parse_jfif_header( pkt->data, video_data, size, NULL, NULL, NULL, TRUE, text_data ) <= 0)
    {
        av_log(s, AV_LOG_ERROR, "ADPIC: ad_read_packet, parse_jfif_header failed\n");
        errorVal = ADPIC_JFIF_MANUAL_SIZE_ERROR;
        return errorVal;
    }
    return errorVal;
}

/**
 * Data info extraction. A DATA_INFO frame simply contains a 
 * type byte followed by a text block. Due to its simplicity, we'll 
 * just extract the whole block into the AVPacket's data structure and 
 * let the client deal with checking its type and parsing its text
 */
int ad_read_info(AVFormatContext *s, ByteIOContext *pb, 
                    AVPacket *pkt, int size)
{
    int n, status, errorVal = 0;

    // Allocate a new packet
    if( (status = ad_new_packet( pkt, size )) < 0 )
    {
        errorVal = ADPIC_INFO_NEW_PACKET_ERROR;
        return errorVal;
    }

    // Get the data
    if( (n = ad_get_buffer( pb, pkt->data, size)) != size )
    {
        errorVal = ADPIC_INFO_GET_BUFFER_ERROR;
        return errorVal;
    }
    return errorVal;
}

int ad_read_layout(AVFormatContext *s, ByteIOContext *pb, 
                      AVPacket *pkt, int size)
{
    int n, status, errorVal = 0;

    // Allocate a new packet
    if( (status = ad_new_packet( pkt, size )) < 0 )
    {
        errorVal = ADPIC_LAYOUT_NEW_PACKET_ERROR;
        return errorVal;
    }

    // Get the data
    if( (n = ad_get_buffer( pb, pkt->data, size)) != size )
    {
        errorVal = ADPIC_LAYOUT_GET_BUFFER_ERROR;
        return errorVal;
    }
    return errorVal;
}

int ad_read_packet(AVFormatContext *s, ByteIOContext *pb, AVPacket *pkt, 
                      FrameType currentFrameType, void *data, char *text_data)
{
    int       errorVal   = 0;
    AVStream  *st        = NULL;
    FrameData *frameData = NULL;
    
    if( currentFrameType == NetVuVideo )
    {
	    // At this point We have a legal pic structure which we use to determine 
	    // which codec stream to use
        NetVuImageData *video_data = (NetVuImageData *)data;
	    if ( (st = ad_get_stream( s, video_data)) == NULL )
	    {
            av_log(s, AV_LOG_ERROR, "ADPIC: ad_read_packet, failed get_stream for video\n");
            errorVal = ADPIC_GET_STREAM_ERROR;
		    return errorVal;
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
        NetVuAudioData *audio_data = (NetVuAudioData *)data;
        if ( (st = ad_get_audio_stream( s, audio_data )) == NULL )
        {
            av_log(s, AV_LOG_ERROR, "ADPIC: ad_read_packet, failed get_stream for audio\n");
            errorVal = ADPIC_GET_AUDIO_STREAM_ERROR;
		    return errorVal;
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
            return errorVal;
        }
    }

    pkt->stream_index = st->index;

    frameData = av_malloc(sizeof(FrameData));

    if( frameData == NULL )
        return errorVal;

    frameData->additionalData = NULL;
    frameData->frameType = currentFrameType;

    if ( (frameData->frameType == NetVuVideo) || (frameData->frameType == NetVuAudio) )
        frameData->frameData = data;
    else if( frameData->frameType == NetVuDataInfo || frameData->frameType == NetVuDataLayout )        // Data info
        frameData->frameData = NULL;
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

    return(ADPIC_NO_ERROR);
}
