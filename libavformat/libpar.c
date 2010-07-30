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
} PARContext;



void libpar_packet_destroy(struct AVPacket *packet);

//static int par_write_header(AVFormatContext * avf)
//{
//	//PARContext *priv = avf->priv_data;
//	return 0;
//}
//static int par_write_packet(AVFormatContext * avf, AVPacket * pkt)
//{
//	//PARContext *priv = avf->priv_data;
//	return 0;
//}
//static int par_write_trailer(AVFormatContext * avf)
//{
//	//PARContext *priv = avf->priv_data;
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
		av_free(fed->indexInfo);
		fed->indexInfo = NULL;
		av_free(fed);
		fed = NULL;
	}
	packet->priv = NULL;
	packet->destruct = NULL;
	
	av_destruct_packet(packet);
}

static int par_probe(AVProbeData *p)
{
	int res;
	FrameInfo fInfo;
	
	res = parReader_loadParFile(NULL, p->filename, -1, &fInfo, 0);
	//parReader_closeParFile();
	if (1 == res)
		return AVPROBE_SCORE_MAX;
	else
		return 0;
}

static int createStream(AVFormatContext * avf, 
						const FrameInfo *frameInfo, 
						const DisplaySettings *dispSet)
{
	int streamId = frameInfo->channel - 1;
	AVStream * st = av_new_stream(avf, streamId);
	
	/// \todo Move this into parreader.dll
	st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
	switch(dispSet->format)  {
		case(0):
		case(1):
		case(2):
			st->codec->codec_id = CODEC_ID_MJPEG;
			st->codec->has_b_frames = 0;
			break;
		case(3):
		case(4):
			st->codec->codec_id = CODEC_ID_MPEG4;
			break;
		default:
			st->codec->codec_id = CODEC_ID_NONE;
			break;
	}
	//st->codec->width = format.src_pixels;
	//st->codec->height = format.src_pixels;
	parReader_getFrameSize(frameInfo, &st->codec->width, &st->codec->height);
	
	//st->codec->time_base = (AVRational){1,1000};
	//st->r_frame_rate = (AVRational){1,1};
	//st->avg_frame_rate = (AVRational){100,1};
	
	//st->first_dts = 1;
	
	av_set_pts_info(st, 64, 1, 1000);
	
	st->sample_aspect_ratio = (AVRational){2,1};
	
	return st->index;
}

static int par_read_header(AVFormatContext * avf, AVFormatParameters * ap)
{
	//int res;
	PARContext *priv = avf->priv_data;
	//ByteIOContext *pb = avf->pb;
	int fileChanged = 0;
	KeyValuePair *index;

	priv->frameInfo.frameBufferSize = 128 * 1024;
	priv->frameInfo.frameBuffer = av_malloc(priv->frameInfo.frameBufferSize);
	
	parReader_setDisplaySettingsDefaults(&priv->dispSet);
	parReader_loadFrame(&priv->frameInfo, &priv->dispSet, &fileChanged);
	
	//int indexItems = parReader_getIndexInfo(&index);
	
	createStream(avf, &priv->frameInfo, &priv->dispSet);
	
	avf->ctx_flags |= AVFMTCTX_NOHEADER;
	
	//parReader_freeIndexInfo(index);
	
	return 0;
}

static int par_read_packet(AVFormatContext * avf, AVPacket * pkt)
{
	PARContext *priv = avf->priv_data;
	const FrameInfo *fInfo = &priv->frameInfo;
	//static int pts = 1;
	
	int fileChang = 0;
	int siz = parReader_loadFrame(&priv->frameInfo, &priv->dispSet, &fileChang);
	
	if ( (siz > 0) && (NULL != fInfo->frameData) )  {
		PARFrameExtra *extraData = NULL;
		int streamIndex = -1;
		int id = fInfo->channel - 1;
		for(int ii = 0; ii < avf->nb_streams; ii++)  {
			if ( (NULL != avf->streams[ii]) && (avf->streams[ii]->id == id) )  {
				streamIndex = ii;
				break;
			}
		}
		if (-1 == streamIndex)  {
			streamIndex = createStream(avf, &priv->frameInfo, &priv->dispSet);
		}
		av_new_packet(pkt, siz);
		pkt->stream_index = streamIndex;
		memcpy(pkt->data, fInfo->frameData, siz);
		
		pkt->pts = fInfo->imageTime;
		pkt->pts *= 1000ULL;
		pkt->pts += fInfo->imageMS;
		
		extraData = av_malloc(sizeof(PARFrameExtra));
		extraData->dsFrameData = av_malloc(sizeof(FrameData));
		extraData->dsFrameData->frameType = NetVuVideo;
		extraData->dsFrameData->frameData = av_malloc(sizeof(NetVuImageData));
		memcpy(extraData->dsFrameData->frameData, fInfo->frameBuffer, sizeof(NetVuImageData));
		extraData->dsFrameData->additionalData = NULL;
		extraData->indexInfo = av_malloc(sizeof(KeyValuePair) * 1);
		
		pkt->priv = extraData;
		
		pkt->destruct = libpar_packet_destroy;
		return 0;
	}
	else  {
		pkt->size = 0;
		return -1;
	}
}

static int par_read_seek(AVFormatContext * avf, int stream_index, int64_t target_ts, int flags)
{
	//PARContext *priv = avf->priv_data;
	return 0;
}

static int par_read_close(AVFormatContext * avf)
{
	PARContext *priv = avf->priv_data;
	
	av_free(priv->frameInfo.frameBuffer);
	priv->frameInfo.frameBuffer = NULL;
	priv->frameInfo.frameBufferSize = 0;
	
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
    .extensions = "par",
};
