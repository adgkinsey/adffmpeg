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
	ParDisplaySettings dispSet;
	ParFrameInfo frameInfo;
	int fileChanged;
	int frameCached;
} PARContext;


void libpar_packet_destroy(struct AVPacket *packet);
AVStream* createStream(AVFormatContext * avf, 
				       const ParFrameInfo *frameInfo);
void createPacket(AVFormatContext * avf, AVPacket *packet, int siz, int fChang);


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
	LibparFrameExtra *fed = (LibparFrameExtra*)packet->priv;
	
	if (packet->data == NULL) {
         return;
    }
	
	if (fed)  {
		if (fed->dsFrameData->frameData)
			av_free(fed->dsFrameData->frameData);
		if (fed->dsFrameData)
			av_free(fed->dsFrameData);
		
		if (fed->frameInfo->frameBuffer)
			av_free(fed->frameInfo->frameBuffer);
		if (fed->frameInfo)
			av_free(fed->frameInfo);
		
		if (fed->indexInfo)
			parReader_freeIndexInfo(fed->indexInfo);
			
		av_free(fed);
	}
	
	av_destruct_packet(packet);
}

AVStream* createStream(AVFormatContext * avf, 
				       const ParFrameInfo *frameInfo)
{
	//PARContext *p = avf->priv_data;
	const NetVuImageData *pic = NULL;
	const NetVuAudioData *aud = NULL;
	char textbuffer[128];
	int w, h;
	int streamId = -1;
	AVStream * st = NULL;
	
	if ( (NULL == avf) || (NULL == frameInfo) || (NULL == frameInfo->frameBuffer) )
		return NULL;
	
	streamId = frameInfo->channel;
	st = av_new_stream(avf, streamId);
	
	st->filename = av_strdup(avf->filename);
	st->nb_frames = 0;
	st->start_time = frameInfo->indexTime * 1000 + frameInfo->indexMS;
	
	if (parReader_frameIsVideo(frameInfo))  {
		st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
		
		pic = frameInfo->frameBuffer;
		switch(pic->vid_format)  {
			case(FRAME_FORMAT_JPEG_422):
			case(FRAME_FORMAT_JPEG_411):
				st->codec->codec_id = CODEC_ID_MJPEG;
				st->codec->has_b_frames = 0;
				break;
			case(FRAME_FORMAT_MPEG4_411):
			case(FRAME_FORMAT_MPEG4_411_I):
			case(FRAME_FORMAT_MPEG4_411_GOV_P):
			case(FRAME_FORMAT_MPEG4_411_GOV_I):
				st->codec->codec_id = CODEC_ID_MPEG4;
				break;
			case(FRAME_FORMAT_RAW_422I):
			case(FRAME_FORMAT_H264_I):
			case(FRAME_FORMAT_H264_P):
				st->codec->codec_id = CODEC_ID_H264;
				break;
			default:
				// Set unknown types to data so we don't try and play them
				st->codec->codec_type = AVMEDIA_TYPE_DATA;
				st->codec->codec_id = CODEC_ID_NONE;
				break;
		}
		
		if (AVMEDIA_TYPE_VIDEO == st->codec->codec_type)  {
			parReader_getFrameSize(frameInfo, &w, &h);
			st->codec->width = w;
			st->codec->height = h;
			st->start_time = frameInfo->imageTime * 1000 + frameInfo->imageMS;
			
			// Set timebase to 1 millisecond, and min frame rate to 1 / timebase
			av_set_pts_info(st, 32, 1, 1000);
			st->r_frame_rate = (AVRational){1,1};	
		
			// Set pixel aspect ratio, display aspect is (sar * width / height)
			/// \todo Could set better values here by checking resolutions and 
			/// assuming PAL/NTSC aspect
			if( (w > 360) && (h <= 480) )  {
				st->sample_aspect_ratio = (AVRational){1, 2};
			}
			else  {
				st->sample_aspect_ratio = (AVRational){1, 1};
			}
			
			parReader_getStreamName(frameInfo->frameBuffer, 
									frameInfo->frameBufferSize, 
									textbuffer, 
									sizeof(textbuffer));
			av_metadata_set2(&st->metadata, "title", textbuffer, 0);
			
			parReader_getStreamDate(frameInfo, textbuffer, sizeof(textbuffer));
			av_metadata_set2(&st->metadata, "date", textbuffer, 0);
		}
	}
	else if (parReader_frameIsAudio(frameInfo))  {
		st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
		st->codec->codec_id = CODEC_ID_ADPCM_ADH;
		st->codec->channels = 1;
		st->codec->block_align = 0;
		st->start_time = frameInfo->imageTime * 1000 + frameInfo->imageMS;
		
		aud = frameInfo->frameBuffer;
		switch(aud->mode)  {
			case(FRAME_FORMAT_AUD_ADPCM_8000):
				st->codec->sample_rate = 8000;
				break;
			case(FRAME_FORMAT_AUD_ADPCM_16000):
				st->codec->sample_rate = 16000;
				break;
			case(FRAME_FORMAT_AUD_L16_44100):
				st->codec->sample_rate = 441000;
				break;
			case(FRAME_FORMAT_AUD_ADPCM_11025):
				st->codec->sample_rate = 11025;
				break;
			case(FRAME_FORMAT_AUD_ADPCM_22050):
				st->codec->sample_rate = 22050;
				break;
			case(FRAME_FORMAT_AUD_ADPCM_32000):
				st->codec->sample_rate = 32000;
				break;
			case(FRAME_FORMAT_AUD_ADPCM_44100):
				st->codec->sample_rate = 44100;
				break;
			case(FRAME_FORMAT_AUD_ADPCM_48000):
				st->codec->sample_rate = 48000;
				break;
			case(FRAME_FORMAT_AUD_L16_8000):
				st->codec->sample_rate = 8000;
				break;
			case(FRAME_FORMAT_AUD_L16_11025):
				st->codec->sample_rate = 11025;
				break;
			case(FRAME_FORMAT_AUD_L16_16000):
				st->codec->sample_rate = 16000;
				break;
			case(FRAME_FORMAT_AUD_L16_22050):
				st->codec->sample_rate = 22050;
				break;
			case(FRAME_FORMAT_AUD_L16_32000):
				st->codec->sample_rate = 32000;
				break;
			case(FRAME_FORMAT_AUD_L16_48000):
				st->codec->sample_rate = 48000;
				break;
			case(FRAME_FORMAT_AUD_L16_12000):
				st->codec->sample_rate = 12000;
				break;
			case(FRAME_FORMAT_AUD_L16_24000):
				st->codec->sample_rate = 24000;
				break;
			default:
				st->codec->sample_rate = 8000;
				break;
		}
		st->codec->codec_tag = 0x0012;
	}
	else  {
		st->codec->codec_type = AVMEDIA_TYPE_DATA;
	}
	
	// Don't set the st->duration since we don't really know it, so we use
	// avf->duration
	
	return st;
}

void createPacket(AVFormatContext * avf, AVPacket *pkt, int siz, int fChang)
{
	PARContext *avfp = avf->priv_data;
	LibparFrameExtra *pktExt = NULL;

	int streamIndex = -1;
	int id = avfp->frameInfo.channel;
	for(int ii = 0; ii < avf->nb_streams; ii++)  {
		if ( (NULL != avf->streams[ii]) && (avf->streams[ii]->id == id) )  {
			streamIndex = ii;
			break;
		}
	}
	if (-1 == streamIndex)  {
        AVStream *str = createStream(avf, &avfp->frameInfo);
		if (str)
			streamIndex = str->index;
		else
			return;
	}
	av_new_packet(pkt, siz);
	
	if (NULL == pkt->data)  {
		pkt->size = 0;
		return;
	}
	
	pkt->destruct = libpar_packet_destroy;
		
	pkt->stream_index = streamIndex;
	
	pktExt = av_malloc(sizeof(LibparFrameExtra));
	if (NULL == pktExt)  {
		pkt->size = 0;
		return;
	}
	pkt->priv = pktExt;
	
	pktExt->fileChanged = fChang;
	pktExt->dsFrameData = av_malloc(sizeof(FrameData));
	if (NULL == pktExt->dsFrameData)  {
		pkt->size = 0;
		return;
	}
	pktExt->indexInfoCount = parReader_getIndexInfo(&pktExt->indexInfo);
	
	memcpy(pkt->data, avfp->frameInfo.frameData, siz);
	
	pktExt->frameInfo = av_malloc(sizeof(ParFrameInfo));	
	if (NULL == pktExt->frameInfo)  {
		pkt->size = 0;
		return;
	}
	
	if (parReader_frameIsVideo(&avfp->frameInfo))  {
		const NetVuImageData *pic = avfp->frameInfo.frameBuffer;
		
		pktExt->frameInfo->frameBufferSize = sizeof(NetVuImageData);
		pktExt->dsFrameData->frameType = NetVuVideo;
		pktExt->dsFrameData->frameData = av_malloc(sizeof(NetVuImageData));
		if (NULL == pktExt->dsFrameData->frameData)  {
			pkt->size = 0;
			return;
		}
		memcpy(pktExt->dsFrameData->frameData, avfp->frameInfo.frameBuffer, 
				sizeof(NetVuImageData));
		pktExt->dsFrameData->additionalData = NULL;
		
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
	else if (parReader_frameIsAudio(&avfp->frameInfo))  {
		//const NetVuAudioData *aud = avfp->frameInfo.frameBuffer;
		pktExt->frameInfo->frameBufferSize = sizeof(NetVuAudioData);
		pktExt->dsFrameData->frameType = NetVuAudio;
		pktExt->dsFrameData->frameData = av_malloc(sizeof(NetVuAudioData));
		if (NULL == pktExt->dsFrameData->frameData)  {
			pkt->size = 0;
			return;
		}
		memcpy(pktExt->dsFrameData->frameData, avfp->frameInfo.frameBuffer, 
				sizeof(NetVuAudioData));
		pktExt->dsFrameData->additionalData = NULL;
	}
	else  {
		/// \todo Do something with data frames
	}
	
	if (pktExt->frameInfo->frameBufferSize > 0)  {
		// Save frameBufferSize as it's about to be overwritten by memcpy
		int fbs = pktExt->frameInfo->frameBufferSize;
		memcpy(pktExt->frameInfo, &(avfp->frameInfo), sizeof(ParFrameInfo));
		pktExt->frameInfo->frameBufferSize = fbs;
		pktExt->frameInfo->frameBuffer = av_malloc(fbs);
		if (NULL == pktExt->frameInfo->frameBuffer)  {
			pkt->size = 0;
			return;
		}
		memcpy(pktExt->frameInfo->frameBuffer,avfp->frameInfo.frameBuffer, fbs);
		pktExt->frameInfo->frameData = pkt->data;
	}

	if (avfp->frameInfo.imageTime > 0)  {
		pkt->pts = avfp->frameInfo.imageTime;
		pkt->pts *= 1000ULL;
		pkt->pts += avfp->frameInfo.imageMS;
	}
	else if (avfp->frameInfo.indexTime > 0)  {
		pkt->pts = avfp->frameInfo.indexTime;
		pkt->pts *= 1000ULL;
		pkt->pts += avfp->frameInfo.indexMS;
	}
	else  {
		pkt->pts = AV_NOPTS_VALUE;
	}
	
	avfp->frameCached = 0;
}

static int par_probe(AVProbeData *p)
{
	int res;
	ParFrameInfo fInfo;
	fInfo.frameBuffer = p->buf;
	fInfo.frameBufferSize = p->buf_size;
	
	parReader_setLogLevel(PARREADER_LOG_WARNING);
	//parReader_setLogLevel(PARREADER_LOG_DEBUG);
	
    av_log(NULL, AV_LOG_DEBUG, "par_probe:  %s\n", p->filename);
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
	char **filelist;
	int seqLen;
    int64_t seconds = 0;
    AVStream *strm = NULL;
    AVRational secondsTB = {1,1};

	p->frameInfo.frameBufferSize = MAX_FRAMEBUFFER_SIZE;
	p->frameInfo.frameBuffer = av_malloc(p->frameInfo.frameBufferSize);
	
	parReader_setDisplaySettingsDefaults(&p->dispSet);
	
	seqLen = parReader_getFilelist(&filelist);
	if (1 == seqLen)  {
		int frameNumber, frameCount;
		unsigned long start, end;
		if (parReader_getIndexData(&frameNumber, &frameCount, &start, &end))  {
            seconds = end - start;
		}
	}
	else  {
		int res, frameNumber, frameCount;
		unsigned long start, end, realStart, realEnd;
		if (parReader_getIndexData(&frameNumber, &frameCount, &start, &end))  {
			realStart = start;
            av_log(NULL, AV_LOG_DEBUG, "par_read_header:  %s (%d)\n", filelist[0], seqLen - 1);
			res = parReader_loadParFile(NULL, filelist[0], seqLen - 1, &p->frameInfo, 0);
			if (parReader_getIndexData(&frameNumber, &frameCount, &start, &end))  {
				realEnd = end;
				seconds = realEnd - realStart;
			}
            av_log(NULL, AV_LOG_DEBUG, "par_read_header:  %s (%d)\n", filelist[0], -1);
			res = parReader_loadParFile(NULL, filelist[0], -1, &p->frameInfo, 0);
		}
	}
	
	siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &p->fileChanged);
	p->frameCached = siz;
	
	// Reading the header opens the file, so ignore this file change notifier
	p->fileChanged = 0;
	
	parReader_getFilename(avf->filename, sizeof(avf->filename));
    
	strm = createStream(avf, &p->frameInfo);
	if (strm)  {
		// Note: Do not set avf->start_time, ffmpeg computes it from AVStream values
		avf->duration = av_rescale_q(seconds, secondsTB, strm->time_base);
	}
	
	avf->ctx_flags |= AVFMTCTX_NOHEADER;
	
	return 0;
}

static int par_read_packet(AVFormatContext * avf, AVPacket * pkt)
{
	PARContext *p = avf->priv_data;
	
	int siz = 0;
	int fileChanged = 0;
	if (p->frameCached)  {
		siz = p->frameCached;
		fileChanged = p->fileChanged;
	}
	else
		siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &fileChanged);
	
	if (siz < 0)  {
		p->frameCached = 0;
		p->fileChanged = 0;
		pkt->size = 0;
		return AVERROR_EOF;
	}
	
	if ( (siz == 0) || (NULL == p->frameInfo.frameData) )  {
		p->frameCached = 0;
		return AVERROR(EAGAIN);
	}
	
	if (fileChanged)
		parReader_getFilename(avf->filename, sizeof(avf->filename));
	
	createPacket(avf, pkt, siz, fileChanged);
	
	return 0;		
}

static int par_read_seek(AVFormatContext *avf, int stream, 
						 int64_t target, int flags)
{
	PARContext *p = avf->priv_data;
	int siz = 0;
	AVStream *st = avf->streams[stream];
	int streamId = st->id;
	int isKeyFrame = 0;
	int step;
	
    av_log(NULL, AV_LOG_DEBUG, "par_read_seek target    = %"PRId64"\n", target);
    
	p->dispSet.cameraNum = streamId;
	if (flags & AVSEEK_FLAG_BACKWARD)
		p->dispSet.playMode = RWND;
	
	do  {
		if ( (flags & AVSEEK_FLAG_FRAME) && (target < 0) )   {
			p->dispSet.fileSeqNo = (-target)-1;
			p->dispSet.frameNumber = 0;
		}
		else  {
			if (flags & AVSEEK_FLAG_FRAME)  {
				// Don't seek beyond the file
				p->dispSet.fileLock = 1;
				p->dispSet.frameNumber = target;
			}
			else  {
				p->dispSet.timestamp = target / 1000LL;
                p->dispSet.millisecs = target % 1000;
            }
		}
		
		do  {
			siz = parReader_loadFrame(&p->frameInfo, &p->dispSet, &p->fileChanged);
			
			// If this frame is not acceptable we want to just iterate through
			// until we find one that is, not seek again, so reset targets
			p->dispSet.frameNumber = -1;
			p->dispSet.timestamp = 0;
			p->dispSet.millisecs = 0;
	
			if (siz < 0)
				break;
			
			if (parReader_frameIsVideo(&p->frameInfo))  {
				if (flags & AVSEEK_FLAG_ANY)
					isKeyFrame = 1;
				else
					isKeyFrame = parReader_isIFrame(&p->frameInfo);
			}
			else  {
				// Always seek to a video frame
				isKeyFrame = 0;
			}
		} while ( (streamId != p->frameInfo.channel) || (0 == isKeyFrame) );
		
		if (siz <= 0)  {
			// If we have failed to load a frame then try again with (target - 1)
			if (target >= 0)  {
				if (flags & AVSEEK_FLAG_FRAME)
					step = 1;
				else
					step = 1000;
				
				if (RWND == p->dispSet.playMode)
					target = target + step;
				else
					target = target - step;
			}
			else  {
				// Jumping to file failed, should never happen
				break;
			}
		}
		else  {
			p->frameCached = siz;
			st->codec->frame_number = p->frameInfo.frameNumber;
		}
	} while (siz <= 0);
	
	p->dispSet.fileLock = 0;
	p->dispSet.playMode = PLAY;
    
    av_log(NULL, AV_LOG_DEBUG, "par_read_seek seek_done = %lu\n", p->frameInfo.imageTime);
    
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
