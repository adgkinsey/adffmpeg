/*
 * Copyright (c) 2011 Miroslav Slugeň <Thunder.m@seznam.cz>
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
#include "rtpdec_formats.h"
#include "adpic.h"
#include "ds_exports.h"


static int adaudio_init(AVFormatContext *s, int st_index, PayloadContext *data)
{
    if (s && (st_index < s->nb_streams))  {
        s->streams[st_index]->codec->block_align = 0;
        s->streams[st_index]->codec->bits_per_coded_sample = 4;
    }
    return 0;
}

static int adaudio_handle_packet(AVFormatContext *ctx,
                                 PayloadContext *data,
                                 AVStream *st,
                                 AVPacket * pkt,
                                 uint32_t * timestamp,
                                 const uint8_t * buf,
                                 int len, int flags)
{
    int ret = 0;
    
    if (buf && (len > 0))  {
        ret = av_new_packet(pkt, len);
        if (ret >= 0)  {
            audiodata_network2host(pkt->data, buf, len);
        }
    }
    return ret;
}


#define RTP_ADAUDIO_HANDLER(payloadid) \
RTPDynamicProtocolHandler ff_adaudio_ ## payloadid ## _dynamic_handler = { \
    .enc_name         = "ADAUDIO-" #payloadid, \
    .codec_type       = AVMEDIA_TYPE_AUDIO, \
    .codec_id         = CODEC_ID_ADPCM_IMA_WAV, \
    .static_payload_id  = payloadid, \
    .init             = adaudio_init, \
    .parse_packet     = adaudio_handle_packet, \
}

RTP_ADAUDIO_HANDLER(RTP_PAYLOAD_TYPE_8000HZ_ADPCM);
RTP_ADAUDIO_HANDLER(RTP_PAYLOAD_TYPE_11025HZ_ADPCM);
RTP_ADAUDIO_HANDLER(RTP_PAYLOAD_TYPE_16000HZ_ADPCM);
RTP_ADAUDIO_HANDLER(RTP_PAYLOAD_TYPE_22050HZ_ADPCM);
