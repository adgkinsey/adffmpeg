#include "libavcodec/avcodec.h"
#include "libavutil/bswap.h"
#include "dspic.h"
#include "jfif_img.h"

static int dspicProbe( AVProbeData *p );
static int dspicReadHeader( AVFormatContext *s, AVFormatParameters *ap );
static int dspicReadPacket( struct AVFormatContext *s, AVPacket *pkt );
static int dspicReadClose( AVFormatContext *s );
static int dspicReadSeek( AVFormatContext *s, int stream_index, int64_t timestamp, int flags );
static int64_t dspicReadPts( AVFormatContext *s, int stream_index, int64_t *ppos, int64_t pos_limit );
static AVStream * GetStream( struct AVFormatContext *s, int camera, int width, int height );
static int ReadNetworkMessageHeader( ByteIOContext *context, MessageHeader *header );
static DMImageData * parseDSJFIFHeader( uint8_t *data, int dataSize );
static int ExtractDSFrameData( uint8_t * buffer, DMImageData *frameData );

static const long       DSPacketHeaderMagicNumber = 0xfaced0ff;
static const char *     DSApp0Identifier = "DigiSpr";

AVInputFormat dspic_demuxer = {
    "dspic",
    "dspic format",
    sizeof(DSPicFormat),
    dspicProbe,
    dspicReadHeader,
    dspicReadPacket,
    dspicReadClose,
    dspicReadSeek,
    dspicReadPts,
};

static int dspicProbe( AVProbeData *p )
{
    long        magicNumber = 0;

    if( p->buf_size <= sizeof(long) )
        return 0;

    /* Get what should be the magic number field of the first header */
    memcpy( &magicNumber, p->buf, sizeof(long) );
    /* Adjust the byte ordering */
    magicNumber = NTOH32(magicNumber);

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
    if( header.magicNumber == DSPacketHeaderMagicNumber )
    {
        if( header.messageType == TCP_SRV_NUDGE )
        {
            frameType = DMNudge;

            /* Read any extra bytes then try again */
            dataSize = header.length - (SIZEOF_MESSAGE_HEADER_IO - sizeof(unsigned long));

            if( (retVal = adpic_new_packet( pkt, dataSize )) < 0 )
                return retVal;

            if( get_buffer( ioContext, pkt->data, dataSize ) != dataSize )
                return AVERROR_IO;
        }
        else if( header.messageType == TCP_SRV_IMG_DATA )
        {
            frameType = DMVideo;

            /* This should be followed by a jfif image */
            dataSize = header.length - (SIZEOF_MESSAGE_HEADER_IO - sizeof(unsigned long));

            /* Allocate packet data large enough for what we have */
            if( (retVal = adpic_new_packet( pkt, dataSize )) < 0 )
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
    if( (frameData = av_malloc( sizeof(FrameData) )) != NULL )
    {
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

	while( !sos && (i < dataSize) )
	{
		memcpy(&marker, &data[i], 2 );
		i+= 2;
		memcpy(&length, &data[i], 2 );
		i+= 2;
		network2host16(marker);
		network2host16(length);

		switch (marker)
		{
		case 0xffe0 :	// APP0
            {
                /* Have a little look at the data in this block, see if it's what we're looking for */
                if( memcmp( &data[i], DSApp0Identifier, strlen(DSApp0Identifier) ) == 0 )
                {
                    int         offset = i;

                    if( (frameData = av_mallocz( sizeof(DMImageData) )) != NULL )
                    {
                        /* Extract the values into a data structure */
                        if( ExtractDSFrameData( &data[offset], frameData ) < 0 )
                        {
                            av_free( frameData );
                            return NULL;
                        }
                    }
                }

			    i += length - 2;
            }
			break;

		case 0xffdb :	// Q table
			i += length-2;
			break;

		case 0xffc0 :	// SOF
			i += length-2;
			break;

		case 0xffc4 :	// Huffman table
			i += length-2;
			break;

		case 0xffda :	// SOS
			i += length-2;
			sos = TRUE;
			break;

		case 0xffdd :	// DRI
			i += length-2;
			break;

		case 0xfffe :	// Comment
			i += length-2;
			break;

		default :
            /* Unknown marker encountered, better just skip past it */
			i += length-2;	// JCB 026 skip past the unknown field
			break;
		}
	}

    return frameData;
}

static int ExtractDSFrameData( uint8_t * buffer, DMImageData *frameData )
{
    int         retVal = AVERROR_IO;
    int         bufIdx = 0;

    if( buffer != NULL )
    {
        memcpy( frameData->identifier, &buffer[bufIdx], ID_LENGTH );
        bufIdx += ID_LENGTH;

        memcpy( &frameData->jpegLength, &buffer[bufIdx], sizeof(unsigned long) );
        bufIdx += sizeof(unsigned long);
        frameData->jpegLength = NTOH32(frameData->jpegLength);

        memcpy( &frameData->imgSeq, &buffer[bufIdx], sizeof(long64) );
        bufIdx += sizeof(long64);
        frameData->imgSeq = NTOH64(frameData->imgSeq);

        memcpy( &frameData->imgTime, &buffer[bufIdx], sizeof(long64) );
        bufIdx += sizeof(long64);

        memcpy( &frameData->camera, &buffer[bufIdx], sizeof(unsigned char) );
        bufIdx += sizeof(unsigned char);

        memcpy( &frameData->status, &buffer[bufIdx], sizeof(unsigned char) );
        bufIdx += sizeof(unsigned char);

        memcpy( &frameData->activity, &buffer[bufIdx], sizeof(unsigned short) * NUM_ACTIVITIES );
        bufIdx += sizeof(unsigned short) * NUM_ACTIVITIES;

        memcpy( &frameData->QFactor, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->QFactor = NTOH16(frameData->QFactor);

        memcpy( &frameData->height, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->height = NTOH16(frameData->height);

        memcpy( &frameData->width, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->width = NTOH16(frameData->width);

        memcpy( &frameData->resolution, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->resolution = NTOH16(frameData->resolution);

        memcpy( &frameData->interlace, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->interlace = NTOH16(frameData->interlace);

        memcpy( &frameData->subHeaderMask, &buffer[bufIdx], sizeof(unsigned short) );
        bufIdx += sizeof(unsigned short);
        frameData->subHeaderMask = NTOH16(frameData->subHeaderMask);

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

static int dspicReadSeek( AVFormatContext *s, int stream_index, int64_t timestamp, int flags )
{
    /* Unsupported */
    return -1;
}

static int64_t dspicReadPts( AVFormatContext *s, int stream_index, int64_t *ppos, int64_t pos_limit )
{
    return -1;
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
	taggedID = (codecType<<31) | ((camera-1)<<24) | ((width>>4)<<12) | ((height>>4)<<0);

	for( i = 0; i < s->nb_streams; i++ )
	{
		stream = s->streams[i];
		if( stream->id == taggedID )
		{
			found = TRUE;
			break;
		}
	}

	if( !found )
	{ 
		stream = av_new_stream( s, taggedID );

		if( stream )
		{
		    stream->codec->codec_type = CODEC_TYPE_VIDEO;
		    stream->codec->codec_id = codecID;
			stream->codec->width = width;
			stream->codec->height = height;
			stream->index = i;
		}
	}

	return stream;
}

int long64ToTimeValues( const long64 *timeValue, time_t * time, unsigned short *ms, unsigned short *flags )
{
    int         retVal = AVERROR_IO;
    const uint8_t *   bufPtr = NULL;

    /* Format of the input 64 bit block is as follows:
       Bits               |             value
       -------------------------------------------------------
       0-15               |             flags
       16-31              |             milliseconds offset
       32-63              |             time_t struct

       NOTE: We're not using structs here as we need portability and cannot guarantee how different compilers will align/pack structures.
    */

    if( time != NULL && ms != NULL && flags != NULL )
    {
        bufPtr = (const uint8_t*)timeValue;

        memcpy( time, bufPtr, sizeof(time_t) );
        bufPtr += sizeof(time_t);
        *time = NTOH32( *time );

        memcpy( ms, bufPtr, sizeof(unsigned short) );
        bufPtr += sizeof(unsigned short);
        *ms = NTOH16( *ms );

        memcpy( flags, bufPtr, sizeof(unsigned short) );
        *flags = NTOH16( *flags );
#if 0
        memcpy( flags, bufPtr, sizeof(unsigned short) );
        bufPtr += sizeof(unsigned short);

        memcpy( ms, bufPtr, sizeof(unsigned short) );
        bufPtr += sizeof(unsigned short);

        memcpy( time, bufPtr, sizeof(time_t) );
#endif

        retVal = 0;
    }

    return retVal;
}

long64 TimeTolong64( time_t time )
{
    long64          timeOut = 0;
    unsigned short  flags = 0;
    unsigned short  ms = 0;
    uint8_t *       bufPtr = NULL;

    /* For now, we're saying we don't know the time zone */
    SET_FLAG_ZONE_UNKNOWN(flags);

    bufPtr = (uint8_t*)&timeOut;

    memcpy( bufPtr, &flags, sizeof(unsigned short) );
    bufPtr += sizeof(unsigned short);

    memcpy( bufPtr, &ms, sizeof(unsigned short) );
    bufPtr += sizeof(unsigned short);

    memcpy( bufPtr, &time, sizeof(time_t) );

    return timeOut;
}
