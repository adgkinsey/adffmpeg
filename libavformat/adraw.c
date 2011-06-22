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
            if (ad_adFormatToCodecId(NULL, test.vid_format) == CODEC_ID_MJPEG)
                return AVPROBE_SCORE_MAX / 2;
        }
        --bufferSize;
        ++bufPtr;
    }
    return 0;
}

static int adraw_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    return ad_read_header(s, ap, NULL);
}

static int adraw_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext     *pb = s->pb;
    uint8_t         *buf = NULL;
    NetVuImageData  *vidDat = NULL;
    char            *txtDat = NULL;
    int             errorVal = 0;
    int             ii = 0;

    vidDat = av_malloc(sizeof(NetVuImageData));
    buf = av_malloc(sizeof(NetVuImageData));

    // Scan for 0xDECADE11 marker
    errorVal = avio_read(pb, buf, sizeof(NetVuImageData));
    while (errorVal > 0)  {
        ad_network2host(vidDat, buf);
        if (pic_version_valid(vidDat->version))  {
            break;
        }
        for(ii = 0; ii < (sizeof(NetVuImageData) - 1); ii++)  {
            buf[ii] = buf[ii+1];
        }
        errorVal = avio_read(pb, buf + sizeof(NetVuImageData) - 1, 1);
    }
    av_free(buf);

    if (errorVal > 0)  {
        switch (ad_adFormatToCodecId(s, vidDat->vid_format))  {
            case(CODEC_ID_MJPEG):
                errorVal = ad_read_jpeg(s, pb, pkt, vidDat, &txtDat);
                break;
            case(CODEC_ID_MPEG4):
            case(CODEC_ID_H264):
            default:
                //errorVal = adbinary_mpeg(s, pb, pkt, vidDat, &txtDat);
                av_log(s, AV_LOG_ERROR, "Unsupported format for adraw demuxer: "
                        "%d\n", vidDat->vid_format);
                break;
        }
    }
    if (errorVal >= 0)
        errorVal = ad_read_packet(s, pb, pkt, NetVuVideo, vidDat, txtDat, NULL);
    else  {
        // If there was an error, release any allocated memory
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


AVInputFormat ff_adraw_demuxer = {
    .name           = "adraw",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings video format (raw)"),
    .read_probe     = adraw_probe,
    .read_header    = adraw_read_header,
    .read_packet    = adraw_read_packet,
    .read_close     = adraw_read_close,
};
