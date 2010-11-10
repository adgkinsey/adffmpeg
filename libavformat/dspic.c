/*
 * Demuxer for AD-Holdings Digital Sprite stream format
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
#include "libavcodec/avcodec.h"
#include "libavutil/bswap.h"
#include "ds.h"

static int dspicProbe( AVProbeData *p );
static int dspicReadHeader( AVFormatContext *s, AVFormatParameters *ap );
static int dspicReadPacket( struct AVFormatContext *s, AVPacket *pkt );
static int dspicReadClose( AVFormatContext *s );
static AVStream * GetStream( struct AVFormatContext *s, int camera, int width, int height );
static int ReadNetworkMessageHeader( ByteIOContext *context, MessageHeader *header );
static DMImageData * parseDSJFIFHeader( uint8_t *data, int dataSize );
static int ExtractDSFrameData( uint8_t * buffer, DMImageData *frameData );
static int dspic_new_packet(AVPacket *pkt, int size);
static void dspic_release_packet( AVPacket *pkt );


static const long       DSPacketHeaderMagicNumber = DS_HEADER_MAGIC_NUMBER;
static const char *     DSApp0Identifier = "DigiSpr";


#define TRUE    1
#define FALSE   0


static int dspicProbe( AVProbeData *p )
{
    long        magicNumber = 0;

    if( p->buf_size <= sizeof(long) )
        return 0;

    /* Get what should be the magic number field of the first header */
    memcpy( &magicNumber, p->buf, sizeof(long) );
    /* Adjust the byte ordering */
    magicNumber = av_be2ne32(magicNumber);

    if( magicNumber == DSPacketHeaderMagicNumber )
        return 100;

    return 0;
}

static int dspicReadHeader( AVFormatContext *s, AVFormatParameters *ap )
{
    return 0;
}

static int dspicReadPacket( struct AVFormatContext *s, AVPacket *pkt )
{
    ByteIOContext *         ioContext = s->pb;
    int                     retVal = 0;
    MessageHeader           header;
    int                     dataSize = 0;
    DMImageData *           videoFrameData = NULL;
    AVStream *              stream = NULL;
    FrameData *             frameData = NULL;
    FrameType               frameType = FrameTypeUnknown;

    /* Attempt to read in a network message header */
    if( (retVal = ReadNetworkMessageHeader( ioContext, &header )) != 0 )
        return retVal;

    /* Validate the header */
    if( header.magicNumber == DSPacketHeaderMagicNumber ) {
        if( header.messageType == TCP_SRV_NUDGE ) {
            frameType = DMNudge;

            /* Read any extra bytes then try again */
            dataSize = header.length - (SIZEOF_MESSAGE_HEADER_IO - sizeof(unsigned long));

            if( (retVal = dspic_new_packet( pkt, dataSize )) < 0 )
                return retVal;

            if( get_buffer( ioContext, pkt->data, dataSize ) != dataSize )
                return AVERROR_IO;
        }
        else if( header.messageType == TCP_SRV_IMG_DATA ) {
            frameType = DMVideo;

            /* This should be followed by a jfif image */
            dataSize = header.length - (SIZEOF_MESSAGE_HEADER_IO - sizeof(unsigned long));

            /* Allocate packet data large enough for what we have */
            if( (retVal = dspic_new_packet( pkt, dataSize )) < 0 )
                return retVal;

            /* Read the jfif data out of the buffer */
            if( get_buffer( ioContext, pkt->data, dataSize ) != dataSize )
                return AVERROR_IO;

            /* Now extract the frame info that's in there */
            if( (videoFrameData = parseDSJFIFHeader( pkt->data, dataSize )) == NULL )
                return AVERROR_IO;

            /* if( audioFrameData != NULL ) frameType |= DS1_PACKET_TYPE_AUDIO; */

            if( (stream = GetStream( s, 0, 0, 0 )) == NULL )
                return AVERROR_IO;
        }
    }
    else
        return AVERROR_IO;

    /* Now create a wrapper to hold this frame's data which we'll store in the packet's private member field */
    if( (frameData = av_malloc( sizeof(FrameData) )) != NULL ) {
        frameData->frameType = frameType;
        frameData->frameData = videoFrameData;
        frameData->additionalData = NULL;

        pkt->priv = frameData;
    }
    else
        goto fail_mem;

    pkt->stream_index = ( stream != NULL ) ? stream->index : 0;
    pkt->duration =  ((int)(AV_TIME_BASE * 1.0));

    return retVal;

fail_mem:
    /* Make sure everything that might have been allocated is released before we return... */
    av_free( frameData );
    av_free( videoFrameData );
    return AVERROR_NOMEM;
}

static DMImageData * parseDSJFIFHeader( uint8_t *data, int dataSize )
{
    DMImageData *            frameData = NULL;
    int                         i;
    unsigned short              length, marker;
    int                         sos = FALSE;

    i = 0;
    while( ((unsigned char)data[i] != 0xff) && (i < dataSize) )
        i++;

    if ( (unsigned char) data[++i] != 0xd8)
        return NULL;  /* Bad SOI */

    i++;

    while( !sos && (i < dataSize) ) {
        memcpy(&marker, &data[i], 2 );
        i += 2;
        memcpy(&length, &data[i], 2 );
        i += 2;
        marker = av_be2ne16(marker);
        length = av_be2ne16(length);

        switch (marker) {
            case 0xffe0 : {	// APP0
                /* Have a little look at the data in this block, see if it's what we're looking for */
                if( memcmp( &data[i], DSApp0Identifier, strlen(DSApp0Identifier) ) == 0 ) {
                    int         offset = i;

                    if( (frameData = av_mallocz( sizeof(DMImageData) )) != NULL ) {
                        /* Extract the values into a data structure */
                        if( ExtractDSFrameData( &data[offset], frameData ) < 0 ) {
                            av_free( frameData );
                            return NULL;
                        }
                    }
                }

                i += length - 2;
            }
            break;

            case 0xffdb :	// Q table
                i += length - 2;
                break;

            case 0xffc0 :	// SOF
                i += length - 2;
                break;

            case 0xffc4 :	// Huffman table
                i += length - 2;
                break;

            case 0xffda :	// SOS
                i += length - 2;
                sos = TRUE;
                break;

            case 0xffdd :	// DRI
                i += length - 2;
                break;

            case 0xfffe :	// Comment
                i += length - 2;
                break;

            default :
                /* Unknown marker encountered, better just skip past it */
                i += length - 2;	// JCB 026 skip past the unknown field
                break;
        }
    }

    return frameData;
}

static int ExtractDSFrameData( uint8_t * buffer, DMImageData *frameData )
{
    int         retVal = AVERROR_IO;
    int         bufIdx = 0;

    if( buffer != NULL ) {
        memcpy( frameData->identifier, &buffer[bufIdx], ID_LENGTH );
        bufIdx += ID_LENGTH;

        memcpy( &frameData->jpegLength, &buffer[bufIdx], sizeof(unsigned long) );
        bufIdx += sizeof(unsigned long);
        frameData->jpegLength = av_be2ne32(frameData->jpegLength);

        memcpy( &frameData->imgSeq, &buffer[bufIdx], sizeof(int64_t) );
        bufIdx += sizeof(int64_t);
        frameData->imgSeq = av_be2ne64(frameData->imgSeq);

        memcpy( &frameData->imgTime, &buffer[bufIdx], sizeof(int64_t) );
        bufIdx += sizeof(int64_t);

        memcpy( &frameData->camera, &buffer[bufIdx], sizeof(unsigned char) );
        bufIdx += sizeof(unsigned char);

        memcpy( &frameData->status, &buffer[bufIdx], sizeof(unsigned char) );
        bufIdx += sizeof(unsigned char);

        memcpy( &frameData->activity, &buffer[bufIdx], sizeof(unsigned short) * NUM_ACTIVITIES );
        bufIdx += sizeof(unsigned short) * NUM_ACTIVITIES;

        memcpy( &frameData->QFactor, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->QFactor = av_be2ne16(frameData->QFactor);

        memcpy( &frameData->height, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->height = av_be2ne16(frameData->height);

        memcpy( &frameData->width, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->width = av_be2ne16(frameData->width);

        memcpy( &frameData->resolution, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->resolution = av_be2ne16(frameData->resolution);

        memcpy( &frameData->interlace, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->interlace = av_be2ne16(frameData->interlace);

        memcpy( &frameData->subHeaderMask, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->subHeaderMask = av_be2ne16(frameData->subHeaderMask);

        memcpy( frameData->camTitle, &buffer[bufIdx], sizeof(char) * CAM_TITLE_LENGTH );
        bufIdx += sizeof(char) * CAM_TITLE_LENGTH;

        memcpy( frameData->alarmText, &buffer[bufIdx], sizeof(char) * ALARM_TEXT_LENGTH );
        bufIdx += sizeof(char) * ALARM_TEXT_LENGTH;

        retVal = 0;
    }

    return retVal;
}

static int ReadNetworkMessageHeader( ByteIOContext *context, MessageHeader *header )
{
    /* Read the header in a piece at a time... */
    if( get_buffer( context, (uint8_t *)&header->magicNumber, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR_IO;

    if( get_buffer( context, (uint8_t *)&header->length, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR_IO;

    if( get_buffer( context, (uint8_t *)&header->channelID, sizeof(long) ) != sizeof(long) )
        return AVERROR_IO;

    if( get_buffer( context, (uint8_t *)&header->sequence, sizeof(long) ) != sizeof(long) )
        return AVERROR_IO;

    if( get_buffer( context, (uint8_t *)&header->messageVersion, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR_IO;

    if( get_buffer( context, (uint8_t *)&header->checksum, sizeof(long) ) != sizeof(long) )
        return AVERROR_IO;

    if( get_buffer( context, (uint8_t *)&header->messageType, sizeof(long) ) != sizeof(long) )
        return AVERROR_IO;

    /* Now adjust the endianess */
    NToHMessageHeader( header );

    return 0;
}

static int dspicReadClose( AVFormatContext *s )
{
    return 0;
}

static AVStream * GetStream( struct AVFormatContext *s, int camera, int width, int height )
{
    int                 codecType = 0;
    int                 codecID = CODEC_ID_MJPEG;
    int                 taggedID;
    int                 i, found;
    AVStream *          stream = NULL;

    found = FALSE;

    /* Generate the ID for this codec */
    taggedID = (codecType << 31) | ((camera - 1) << 24) | ((width >> 4) << 12) | ((height >> 4) << 0);

    for( i = 0; i < s->nb_streams; i++ ) {
        stream = s->streams[i];
        if( stream->id == taggedID ) {
            found = TRUE;
            break;
        }
    }

    if( !found ) {
        stream = av_new_stream( s, taggedID );

        if( stream ) {
            stream->codec->codec_type = CODEC_TYPE_VIDEO;
            stream->codec->codec_id = codecID;
            stream->codec->width = width;
            stream->codec->height = height;
            stream->index = i;
        }
    }

    return stream;
}

static int dspic_new_packet(AVPacket *pkt, int size)
{
    int     retVal = av_new_packet( pkt, size );

    if( retVal >= 0 ) {
        // Give the packet its own destruct function
        pkt->destruct = dspic_release_packet;
    }

    return retVal;
}

static void dspic_release_packet( AVPacket *pkt )
{
    if (pkt != NULL) {
        if (pkt->priv != NULL) {
            // Have a look what type of frame we have and then delete anything inside as appropriate
            FrameData *     frameData = (FrameData *)pkt->priv;

            // Nothing else has nested allocs so just delete the frameData if it exists
            if( frameData->frameData != NULL ) {
                av_free( frameData->frameData );
                frameData->frameData = NULL;
            }

            if( frameData->additionalData != NULL ) {
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


AVInputFormat dspic_demuxer = {
    .name           = "dspic",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings Digital-Sprite format"),
    .read_probe     = dspicProbe,
    .read_header    = dspicReadHeader,
    .read_packet    = dspicReadPacket,
    .read_close     = dspicReadClose,
};
