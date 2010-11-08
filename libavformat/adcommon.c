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
 #include "adpic.h"

 
void adpic_network2host(NetVuImageData *pic)
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

int adpic_new_packet(AVPacket *pkt, int size)
{
    int     retVal = av_new_packet( pkt, size );

    if( retVal >= 0 )
    {
        // Give the packet its own destruct function
        pkt->destruct = adpic_release_packet;
    }

    return retVal;
}

void adpic_release_packet( AVPacket *pkt )
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

int adpic_get_buffer(ByteIOContext *s, unsigned char *buf, int size)
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
