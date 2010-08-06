#include "adaudio.h"
#include "ds_exports.h"

static AVStream *get_audio_stream( struct AVFormatContext *s );

int adaudio_read_packet(struct AVFormatContext *s, AVPacket *pkt);


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
       )
    {
        return 100;
    }

    return 0;
}

static int adaudio_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    s->ctx_flags |= AVFMTCTX_NOHEADER;
					
	return 0;
}

int adaudio_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *         ioContext = s->pb;
    int                     retVal = AVERROR_IO;
    int                     packetSize = 0;
    int                     sampleSize = 0;
    AVStream *              st = NULL;
	FrameData *             frameData = NULL;
	int					    isPacketAlloced = 0;

    /* Get the next packet */
    if( (packetSize = ioContext->read_packet( ioContext->opaque, ioContext->buf_ptr, ioContext->buffer_size )) > 0 )
    {
        /* Validate the 12 byte RTP header as best we can */
        if( ioContext->buf_ptr[1] == RTP_PAYLOAD_TYPE_8000HZ_ADPCM || 
            ioContext->buf_ptr[1] == RTP_PAYLOAD_TYPE_11025HZ_ADPCM ||
            ioContext->buf_ptr[1] == RTP_PAYLOAD_TYPE_16000HZ_ADPCM ||
            ioContext->buf_ptr[1] == RTP_PAYLOAD_TYPE_22050HZ_ADPCM
          )
        {
            /* Calculate the size of the sample data */
            sampleSize = packetSize - SIZEOF_RTP_HEADER;

            /* Create a new AVPacket */
            if( av_new_packet( pkt, sampleSize ) >= 0 )
            {
				isPacketAlloced = 1;

                /* Copy data into packet */
                memcpy( pkt->data, &ioContext->buf_ptr[SIZEOF_RTP_HEADER], sampleSize );

                /* Configure stream info */
                if( (st = get_audio_stream( s )) != NULL )
                {
                    pkt->stream_index = st->index;
	                pkt->duration =  ((int)(AV_TIME_BASE * 1.0));

					if( (frameData = av_malloc( sizeof(FrameData) )) != NULL )
					{
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
	if( retVal < 0 && isPacketAlloced != 0 )
	{
		av_free_packet( pkt );
	}

    return retVal;
}

static int adaudio_read_close(AVFormatContext *s)
{
    return 0;
}

static int adaudio_read_seek( AVFormatContext *s, int stream_index, int64_t timestamp, int flags )
{
    return -1;
}

static int64_t adaudio_read_pts(AVFormatContext *s, int stream_index, int64_t *ppos, int64_t pos_limit)
{
	return -1;
}

AVInputFormat adaudio_demuxer = {
    "adaudio",
    "adaudio format",
    sizeof(ADAudioContext),
    adaudio_probe,
    adaudio_read_header,
    adaudio_read_packet,
    adaudio_read_close,
    adaudio_read_seek,
    adaudio_read_pts,
};

static AVStream *get_audio_stream( struct AVFormatContext *s )
{
    int id;
    int i, found;
    AVStream *st;

	found = 0;

	id = AD_AUDIO_STREAM_ID;

	for( i = 0; i < s->nb_streams; i++ )
	{
		st = s->streams[i];
		if( st->id == id )
		{
			found = 1;
			break;
		}
	}

    // Did we find our audio stream? If not, create a new one
	if( !found )
	{
		st = av_new_stream( s, id );
		if (st)
		{
		    st->codec->codec_type = CODEC_TYPE_AUDIO;
		    st->codec->codec_id = CODEC_ID_ADPCM_ADH;
            st->codec->channels = 1;
            st->codec->block_align = 0;
			st->index = i;
		}
	}

	return st;
}
