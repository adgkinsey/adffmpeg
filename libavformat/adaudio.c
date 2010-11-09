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

#include "avformat.h"
#include "ds_exports.h"


#define SIZEOF_RTP_HEADER       12
#define AD_AUDIO_STREAM_ID      0x212ED83E


static AVStream *get_audio_stream( struct AVFormatContext *s );
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

static int adaudio_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    s->ctx_flags |= AVFMTCTX_NOHEADER;

    return 0;
}

static int adaudio_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *         ioContext = s->pb;
    int                     retVal = AVERROR_IO;
    int                     packetSize = 0;
    int                     sampleSize = 0;
    AVStream *              st = NULL;
    FrameData *             frameData = NULL;
    int					    isPacketAlloced = 0;

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

                /* Configure stream info */
                if( (st = get_audio_stream( s )) != NULL ) {
                    pkt->stream_index = st->index;
                    pkt->duration =  ((int)(AV_TIME_BASE * 1.0));

                    if( (frameData = av_malloc( sizeof(FrameData) )) != NULL ) {
                        /* Set the frame info up */
                        frameData->frameType = RTPAudio;
                        frameData->frameData = (void*)(&ioContext->buf_ptr[1]);
                        frameData->additionalData = NULL;

                        pkt->priv = (void*)frameData;

                        retVal = 0;
                    }
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

static AVStream *get_audio_stream( struct AVFormatContext *s )
{
    int id;
    int i, found;
    AVStream *st;

    found = 0;

    id = AD_AUDIO_STREAM_ID;

    for( i = 0; i < s->nb_streams; i++ ) {
        st = s->streams[i];
        if( st->id == id ) {
            found = 1;
            break;
        }
    }

    // Did we find our audio stream? If not, create a new one
    if( !found ) {
        st = av_new_stream( s, id );
        if (st) {
            st->codec->codec_type = CODEC_TYPE_AUDIO;
            st->codec->codec_id = CODEC_ID_ADPCM_ADH;
            st->codec->channels = 1;
            st->codec->block_align = 0;
            st->index = i;
        }
    }

    return st;
}


AVInputFormat adaudio_demuxer = {
    .name           = "adaudio",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings audio format"),
    .read_probe     = adaudio_probe,
    .read_header    = adaudio_read_header,
    .read_packet    = adaudio_read_packet,
};
