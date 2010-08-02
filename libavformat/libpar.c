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

#include "avformat.h"
#include "ds_exports.h"
#include "libpar.h"

#include <parreader.h>
#include <parreader_types.h>

typedef struct {
	DisplaySettings dispSet;
	FrameInfo frameInfo;
	int fileChanged;
	int frameCached;
} PARContext;



void libpar_packet_destroy(struct AVPacket *packet);
int createStream(AVFormatContext * avf, 
				 const FrameInfo *frameInfo, const DisplaySettings *dispSet);
void createPacket(AVFormatContext * avf, AVPacket *packet, int siz);


const unsigned int MAX_FRAMEBUFFER_SIZE = 256 * 1024;


//static int par_write_header(AVFormatContext * avf)
//{
//	//PARContext *p = avf->priv_data;
//	return 0;
//}
//static int par_write_packet(AVFormatContext * avf, AVPacket * pkt)
//{
//	//PARContext *p = avf->priv_data;
//	return 0;
//}
//static int par_write_trailer(AVFormatContext * avf)
//{
//	//PARContext *p = avf->priv_data;
//	return 0;
//}

void libpar_packet_destroy(struct AVPacket *packet)
{
	PARFrameExtra *fed = (PARFrameExtra*)packet->priv;
	
	if (packet->data == NULL) {
         return;
    }
	
	if (fed)  {
		av_free(fed->dsFrameData->frameData);
		fed->dsFrameData->frameData = NULL;
		av_free(fed->dsFrameData);
		fed->dsFrameData = NULL;
		
		parReader_freeIndexInfo(fed->indexInfo);
		fed->indexInfo = NULL;
		av_free(fed);
		fed = NULL;
	}
	packet->priv = NULL;
	packet->destruct = NULL;
	
	av_destruct_packet(packet);
}

int createStream(AVFormatContext * avf, 
				 const FrameInfo *frameInfo, const DisplaySettings *dispSet)
{
	//PARContext *p = avf->priv_data;
	
	int streamId = frameInfo->channel;
	AVStream * st = av_new_stream(avf, streamId);
	
	if (parReader_frameIsVideo(frameInfo))  {
		/// \todo Move this into parreader.dll
		const NetVuImageData *pic = frameInfo->frameBuffer;
		switch(pic->vid_format)  {
			case(FRAME_FORMAT_JPEG_422):
			case(FRAME_FORMAT_JPEG_411):
				st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
				st->codec->codec_id = CODEC_ID_MJPEG;
				st->codec->has_b_frames = 0;
				break;
			case(FRAME_FORMAT_MPEG4_411):
			case(FRAME_FORMAT_MPEG4_411_I):
			case(FRAME_FORMAT_MPEG4_411_GOV_P):
			case(FRAME_FORMAT_MPEG4_411_GOV_I):
				st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
				st->codec->codec_id = CODEC_ID_MPEG4;
				break;
			case(FRAME_FORMAT_RAW_422I):
			case(FRAME_FORMAT_H264_I):
			case(FRAME_FORMAT_H264_P):
				st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
				st->codec->codec_id = CODEC_ID_H264;
				break;
			default:
				st->codec->codec_type = AVMEDIA_TYPE_UNKNOWN;
				st->codec->codec_id = CODEC_ID_NONE;
				break;
		}
	}
	else if (parReader_frameIsAudio(frameInfo))  {
		/// \todo Move this into parreader.dll
		const NetVuAudioData *aud = frameInfo->frameBuffer;
		switch(aud->mode)  {
			case(FRAME_FORMAT_RAW):
			case(FRAME_FORMAT_ADPCM):
			case(FRAME_FORMAT_ADPCM_8000):
			case(FRAME_FORMAT_ADPCM_16000):
			case(FRAME_FORMAT_L16_44100):
			case(FRAME_FORMAT_ADPCM_11025):
			case(FRAME_FORMAT_ADPCM_22050):
			case(FRAME_FORMAT_ADPCM_32000):
			case(FRAME_FORMAT_ADPCM_44100):
			case(FRAME_FORMAT_ADPCM_48000):
			case(FRAME_FORMAT_L16_8000):
			case(FRAME_FORMAT_L16_11025):
			case(FRAME_FORMAT_L16_16000):
			case(FRAME_FORMAT_L16_22050):
			case(FRAME_FORMAT_L16_32000):
			case(FRAME_FORMAT_L16_48000):
			case(FRAME_FORMAT_L16_12000):
			case(FRAME_FORMAT_L16_24000):
				st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
				st->codec->codec_id = CODEC_ID_ADPCM_ADH;
				st->codec->channels = 1;
				st->codec->block_align = 0;
				break;
			default:
				st->codec->codec_type = AVMEDIA_TYPE_UNKNOWN;
				st->codec->codec_id = CODEC_ID_NONE;
				break;
		}
	}
	else  {
		st->codec->codec_type = AVMEDIA_TYPE_DATA;
	}
	
	if (AVMEDIA_TYPE_VIDEO == st->codec->codec_type)  {
		int w, h;
		
		parReader_getFrameSize(frameInfo, &w, &h);
		st->codec->width = w;
		st->codec->height = h;
		
		st->r_frame_rate = (AVRational){1,1000};	
		av_set_pts_info(st, 32, 1, 1000);
	
		//if( (w > 360) && (h <= 480) )  {
		//  // Need to double the height for aspect ratio calculations
		//  h *= 2;
		//}
		//st->sample_aspect_ratio = (AVRational){w,h};
		//st->codec->sample_aspect_ratio = (AVRational){w,h};
		
		/// \todo Generate from index
		//st->duration = 0;
		//st->start_time
	}
	
	return st->index;
}

void createPacket(AVFormatContext * avf, AVPacket *pkt, int siz)
{
	PARContext *p = avf->priv_data;
	const FrameInfo *fInfo = &p->frameInfo;
	PARFrameExtra *extraData = NULL;
	int streamIndex = -1;
	int id = fInfo->channel;
	for(int ii = 0; ii < avf->nb_streams; ii++)  {
		if ( (NULL != avf->streams[ii]) && (avf->streams[ii]->id == id) )  {
			streamIndex = ii;
			break;
		}
	}
	if (-1 == streamIndex)  {
		streamIndex = createStream(avf, &p->frameInfo, &p->dispSet);
	}
	av_new_packet(pkt, siz);
	pkt->stream_index = streamIndex;
	memcpy(pkt->data, fInfo->frameData, siz);
	
	pkt->pts = fInfo->imageTime;
	pkt->pts *= 1000ULL;
	pkt->pts += fInfo->imageMS;
	
	if (parReader_frameIsVideo(&p->frameInfo))  {
		/// \todo Move this into parreader.dll
		const NetVuImageData *pic = p->frameInfo.frameBuffer;
		switch(pic->vid_format)  {
			case(FRAME_FORMAT_JPEG_422):
			case(FRAME_FORMAT_JPEG_411):
			case(FRAME_FORMAT_MPEG4_411_I):
			case(FRAME_FORMAT_MPEG4_411_GOV_I):
			case(FRAME_FORMAT_RAW_422I):
			case(FRAME_FORMAT_H264_I):
				pkt->flags |= AV_PKT_FLAG_KEY;
				break;
			default:
				break;
		}
	}
	
	/// \todo Detect frame type and do the right thing
	extraData = av_malloc(sizeof(PARFrameExtra));
	extraData->dsFrameData = av_malloc(sizeof(FrameData));
	extraData->dsFrameData->frameType = NetVuVideo;
	extraData->dsFrameData->frameData = av_malloc(sizeof(NetVuImageData));
	memcpy(extraData->dsFrameData->frameData, fInfo->frameBuffer, 
			sizeof(NetVuImageData));
	extraData->dsFrameData->additionalData = NULL;
	extraData->indexInfoCount = parReader_getIndexInfo(&extraData->indexInfo);
	
	pkt->priv = extraData;
	
	pkt->destruct = libpar_packet_destroy;
	
	p->frameCached = 0;
}

static int par_probe(AVProbeData *p)
{
	int res;
	FrameInfo fInfo;
	fInfo.frameBuffer = p->buf;
	fInfo.frameBufferSize = p->buf_size;
	
	res = parReader_loadParFile(NULL, p->filename, -1, &fInfo, 0);
	//parReader_closeParFile();
	if (1 == res)
		return AVPROBE_SCORE_MAX;
	else
		return 0;
}

static int par_read_header(AVFormatContext * avf, AVFormatParameters * ap)
{
	int siz;
	PARContext *p = avf->priv_data;
	//ByteIOContext *pb = avf->pb;
	//KeyValuePair *index;

	p->frameInfo.frameBufferSize = MAX_FRAMEBUFFER_SIZE;
	p->frameInfo.frameBuffer = av_malloc(p->frameInfo.frameBufferSize);
	
	parReader_setDisplaySettingsDefaults(&p->dispSet);
	siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &p->fileChanged);
	p->frameCached = siz;
	
	createStream(avf, &p->frameInfo, &p->dispSet);
	
	avf->ctx_flags |= AVFMTCTX_NOHEADER;
	
	return 0;
}

static int par_read_packet(AVFormatContext * avf, AVPacket * pkt)
{
	PARContext *p = avf->priv_data;
	
	int siz = 0;
	if (p->frameCached)
		siz = p->frameCached;
	else
		siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &p->fileChanged);
	
	if (siz > 0)  {
		if (NULL != p->frameInfo.frameData)  {
			createPacket(avf, pkt, siz);
			return 0;
		}
		else  {
			return AVERROR(EAGAIN);
		}
	}
	else  {
		p->frameCached = 0;
		pkt->size = 0;
		return AVERROR_EOF;
	}
}

static int par_read_seek(AVFormatContext *avf, int stream, 
						 int64_t target, int flags)
{
	PARContext *p = avf->priv_data;
	int siz = 0;
	
	// Don't seek beyond the file
	p->dispSet.fileLock = 1;
	
	if (flags & AVSEEK_FLAG_BACKWARD)
		p->dispSet.playMode = RWND;
	
	if ( ! (flags & AVSEEK_FLAG_ANY) )
		p->dispSet.iFramesOnly = 1;
	
	if (flags & AVSEEK_FLAG_FRAME)
		p->dispSet.frameNumber = target;
	else
		p->dispSet.timestamp = target / 1000;
	
	p->dispSet.cameraNum = avf->streams[stream]->id;
	siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &p->fileChanged);
	p->dispSet.playMode = PLAY;
	p->dispSet.iFramesOnly = 0;
	while ((stream>=0) && (avf->streams[stream]->id != p->frameInfo.channel))  {
		siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &p->fileChanged);
	}
	p->frameCached = siz;
	
	p->dispSet.fileLock = 0;		
	return p->frameInfo.imageTime;
}

static int par_read_close(AVFormatContext * avf)
{
	PARContext *p = avf->priv_data;
	
	av_free(p->frameInfo.frameBuffer);
	p->frameInfo.frameBuffer = NULL;
	p->frameInfo.frameBufferSize = 0;
	
	return 0;
}

//AVOutputFormat libparreader_muxer = {
//    "libpar",
//    "AD-Holdings PAR format",
//    "video/adhbinary",
//    "par",
//    sizeof(PARContext),
//    CODEC_ID_MJPEG,
//    CODEC_ID_MPEG4,
//    par_write_header,
//    par_write_packet,
//    par_write_trailer,
//    .flags = AVFMT_GLOBALHEADER,
//};

AVInputFormat libparreader_demuxer = {
    "libpar",
    NULL_IF_CONFIG_SMALL("AD-Holdings PAR format"),
    sizeof(PARContext),
    par_probe,
    par_read_header,
    par_read_packet,
    par_read_close,
    par_read_seek,
    //.extensions = "par",
};
