/*
 * AD-Holdings demuxer for AD audio stream format
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
 * AD-Holdings demuxer for AD audio stream format
 */

#include "avformat.h"
#include "ds_exports.h"
#include "adpic.h"


#define SIZEOF_RTP_HEADER       12
#define AD_AUDIO_STREAM_ID      0x212ED83E


static int adaudio_probe(AVProbeData *p);
static int adaudio_read_header(AVFormatContext *s);
static int adaudio_read_packet(struct AVFormatContext *s, AVPacket *pkt);


static int adaudio_probe(AVProbeData *p)
{
    if( p->buf_size < 4 )
        return 0;

    /* Check the value of the first byte and the payload byte */
    if(
        p->buf[0] == 0x80 &&
        (
            p->buf[1] == RTP_PAYLOAD_TYPE_8000HZ_ADPCM ||
            p->buf[1] == RTP_PAYLOAD_TYPE_11025HZ_ADPCM ||
            p->buf[1] == RTP_PAYLOAD_TYPE_16000HZ_ADPCM ||
            p->buf[1] == RTP_PAYLOAD_TYPE_22050HZ_ADPCM
        )
    ) {
        return AVPROBE_SCORE_MAX;
    }

    return 0;
}

static int adaudio_read_header(AVFormatContext *s)
{
    s->ctx_flags |= AVFMTCTX_NOHEADER;

    return 0;
}

static int adaudio_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *         ioContext = s->pb;
    int                   retVal = AVERROR(EIO);
    int                   packetSize = 0;
    int                   sampleSize = 0;
    AVStream *            st = NULL;
    int                   isPacketAlloced = 0;
#ifdef AD_NO_SIDEDATA
    struct ADFrameData *    frameData = NULL;
#endif

    /* Get the next packet */
    if( (packetSize = ioContext->read_packet( ioContext->opaque, ioContext->buf_ptr, ioContext->buffer_size )) > 0 ) {
        /* Validate the 12 byte RTP header as best we can */
        if( ioContext->buf_ptr[1] == RTP_PAYLOAD_TYPE_8000HZ_ADPCM ||
            ioContext->buf_ptr[1] == RTP_PAYLOAD_TYPE_11025HZ_ADPCM ||
            ioContext->buf_ptr[1] == RTP_PAYLOAD_TYPE_16000HZ_ADPCM ||
            ioContext->buf_ptr[1] == RTP_PAYLOAD_TYPE_22050HZ_ADPCM
          ) {
            /* Calculate the size of the sample data */
            sampleSize = packetSize - SIZEOF_RTP_HEADER;

            /* Create a new AVPacket */
            if( av_new_packet( pkt, sampleSize ) >= 0 ) {
                isPacketAlloced = 1;

                /* Copy data into packet */
                memcpy( pkt->data, &ioContext->buf_ptr[SIZEOF_RTP_HEADER], sampleSize );

                audiodata_network2host(pkt->data, sampleSize);

                /* Configure stream info */
                if( (st = ad_get_audio_stream(s, NULL)) != NULL ) {
                    pkt->stream_index = st->index;
                    pkt->duration =  ((int)(AV_TIME_BASE * 1.0));

#ifdef AD_NO_SIDEDATA
                    if( (frameData = av_malloc(sizeof(*frameData))) != NULL )  {
                        /* Set the frame info up */
                        frameData->frameType = RTPAudio;
                        frameData->frameData = (void*)(&ioContext->buf_ptr[1]);
                        frameData->additionalData = NULL;

                        pkt->priv = (void*)frameData;
                        retVal = 0;
                    }
                    else
                        retVal = AVERROR(ENOMEM);
#endif
                }
            }
        }
    }

    /* Check whether we need to release the packet data we allocated */
    if( retVal < 0 && isPacketAlloced != 0 ) {
        av_free_packet( pkt );
    }

    return retVal;
}


AVInputFormat ff_adaudio_demuxer = {
    .name           = "adaudio",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings audio format"),
    .read_probe     = adaudio_probe,
    .read_header    = adaudio_read_header,
    .read_packet    = adaudio_read_packet,
};
