/*
 * AD-Holdings demuxer for AD stream format (raw)
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
 * AD-Holdings demuxer for AD stream format (raw)
 */

#include "avformat.h"
#include "adpic.h"


/**
 * Identify if the stream as an AD raw stream
 */
static int adraw_probe(AVProbeData *p)
{
    int bufferSize = p->buf_size;
    uint8_t *bufPtr = p->buf;

    while (bufferSize >= NetVuImageDataHeaderSize)  {
        NetVuImageData test;
        ad_network2host(&test, bufPtr);
        if (pic_version_valid(test.version))  {
            return AVPROBE_SCORE_MAX / 2;
        }
        --bufferSize;
        ++bufPtr;
    }
    return 0;
}

static int adraw_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    int utc;
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return ad_read_header(s, ap, &utc);
}

static int adraw_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext   *pb = s->pb;
    uint8_t         *buf = NULL;
    NetVuImageData  *vidDat = NULL;
    char            *txtDat = NULL;
    int             errorVal = 0;
    ADFrameType     frameType;
    int             ii = 0;

    vidDat = av_malloc(sizeof(NetVuImageData));
    buf = av_malloc(sizeof(NetVuImageData));

    // Scan for 0xDECADE11 marker
    errorVal = get_buffer(pb, buf, sizeof(NetVuImageData));
    while (errorVal > 0)  {
        ad_network2host(vidDat, buf);
        if (pic_version_valid(vidDat->version))  {
            break;
        }
        for(ii = 0; ii < (sizeof(NetVuImageData) - 1); ii++)  {
            buf[ii] = buf[ii+1];
        }
        errorVal = get_buffer(pb, buf + sizeof(NetVuImageData) - 1, 1);
    }
    av_free(buf);

    if (errorVal > 0)  {
        // Prepare for video or audio read
        errorVal = initADData(DATA_JPEG, &frameType, &vidDat, NULL);
    }
    if (errorVal >= 0)
        errorVal = ad_read_jpeg(s, pb, pkt, vidDat, &txtDat);
    if (errorVal >= 0)
        errorVal = ad_read_packet(s, pb, pkt, frameType, vidDat, txtDat);

    if (errorVal < 0)  {
        // If there was an error, release any memory has been allocated
        if( vidDat != NULL )
            av_free( vidDat );

        if( txtDat != NULL )
            av_free( txtDat );
    }

    return errorVal;
}

static int adraw_read_close(AVFormatContext *s)
{
    return 0;
}


AVInputFormat adraw_demuxer = {
    .name           = "adraw",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings video format (raw)"),
    .read_probe     = adraw_probe,
    .read_header    = adraw_read_header,
    .read_packet    = adraw_read_packet,
    .read_close     = adraw_read_close,
};
