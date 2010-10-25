// $HDR$

//$Log:  126591: adpic.c 
//
//    Rev 1.1    13/12/2006 12:53:22  pcolbran
// Added error checking to get_buffer_calls. Don't free streams in
// adpic_read_close() this is done at the av_bif level
//
//    Rev 1.0    30/11/2006 08:12:36  pcolbran
// Decoder for AD format streams
/* ------------------------------------------------------------------------
*   Module name : pic.c
*   Description : AD picture format handling
*	Author  : PR Colbran
*	(c) Copyright Dedicated Micros Ltd.
*  ------------------------------------------------------------------------
   Version Initials   Date      Comments
   ------------------------------------------------------------------------

  	001		PRC		24/11/06	Initial creation 
  	002		PRC		13/12/06	Added error checking to get_buffer_calls.
  								Don't free streams in adpic_read_close() this
  								is done at the av_bif level
  	003		PRC		25/01/07	Switched lo logger debug. Added error checking to 
  								av_new_packet calls
	---------------------------------------------------------------------------
*/

#include "avformat.h"
#include "libavutil/bswap.h"

#include "adpic.h"
#include "jfif_img.h"
#include "netvu.h"


#define BINARY_PUSH_MIME_STR    "video/adhbinary"
#define TEMP_BUFFER_SIZE        1024

/* These are the data types that are supported by the DS2 video servers. */
enum data_type { DATA_JPEG, DATA_JFIF, DATA_MPEG4I, DATA_MPEG4P, DATA_AUDIO_ADPCM, DATA_AUDIO_RAW, DATA_MINIMAL_MPEG4, DATA_MINIMAL_AUDIO_ADPCM, DATA_LAYOUT, DATA_INFO, DATA_H264I, DATA_H264P, DATA_XML_INFO, MAX_DATA_TYPE };
#define DATA_PLAINTEXT              (MAX_DATA_TYPE + 1)   /* This value is only used internally within the library DATA_PLAINTEXT blocks should not be exposed to the client */


typedef struct {
	int isMIME;
    int utc_offset;
    int isBinary;
} AdpicContext;


static int adpicSkipInfoList(ByteIOContext * pb);
static int adpicFindTag(const char *Tag, ByteIOContext *pb, int MaxLookAhead);


static int adpic_parse_mime_header( ByteIOContext *pb, int *dataType, int *size, long *extra );
static int process_line( char *line, int* line_count, int *dataType, int *size, long *extra );
static int adpic_parse_mp4_text_data( unsigned char *mp4TextData, int bufferSize, NetVuImageData *video_data, char **additionalTextData );
static int process_mp4data_line( char *line, int line_count, NetVuImageData *video_data, struct tm *time, char ** additionalText );
static void adpic_release_packet( AVPacket *pkt );
static int adpic_is_valid_separator( unsigned char * buf, int bufLen );

static int adpic_new_packet( AVPacket *pkt, int size );
static int adpic_get_buffer(ByteIOContext *s, unsigned char *buf, int size);
static void audioheader_network2host( NetVuAudioData *hdr );
static int adpic_read_packet(struct AVFormatContext *s, AVPacket *pkt);


static void pic_network2host(NetVuImageData *pic);


/* CS - The following structure is used to extract 6 bytes from the incoming stream. It needs therefore to be aligned on a 
   2 byte boundary. Not sure whether this solution is portable though */
#pragma pack(push,2)
typedef struct _minimal_video_header	// PRC 002
{
	uint32_t            t;
	unsigned short      ms;
}MinimalVideoHeader;
#pragma pack(pop) /* Revert to the old alignment */

union _minimal_video	// PRC 002
{
	struct _minimal_video_header header;
	char payload[sizeof(uint32_t)+sizeof(unsigned short)];
};

typedef struct _minimal_audio_header	// PRC 002
{
	uint32_t t;
	unsigned short ms;
	unsigned short mode;
}MinimalAudioHeader;


static AdpicContext* lastProbedCtxt;


static void pic_network2host(NetVuImageData *pic)
{
	pic->version				= av_be2ne32(pic->version);
	pic->mode					= av_be2ne32(pic->mode);
	pic->cam					= av_be2ne32(pic->cam);
	pic->vid_format				= av_be2ne32(pic->vid_format);
	pic->start_offset			= av_be2ne32(pic->start_offset);
	pic->size					= av_be2ne32(pic->size);
	pic->max_size				= av_be2ne32(pic->max_size);
	pic->target_size			= av_be2ne32(pic->target_size);
	pic->factor					= av_be2ne32(pic->factor);
	pic->alm_bitmask_hi			= av_be2ne32(pic->alm_bitmask_hi);
	pic->status					= av_be2ne32(pic->status);
	pic->session_time			= av_be2ne32(pic->session_time);
	pic->milliseconds			= av_be2ne32(pic->milliseconds);
	pic->utc_offset				= av_be2ne32(pic->utc_offset);
	pic->alm_bitmask			= av_be2ne32(pic->alm_bitmask);
	pic->format.src_pixels		= av_be2ne16(pic->format.src_pixels);
	pic->format.src_lines		= av_be2ne16(pic->format.src_lines);
	pic->format.target_pixels	= av_be2ne16(pic->format.target_pixels);
	pic->format.target_lines	= av_be2ne16(pic->format.target_lines);
	pic->format.pixel_offset	= av_be2ne16(pic->format.pixel_offset);
	pic->format.line_offset		= av_be2ne16(pic->format.line_offset);
}

static void audioheader_network2host( NetVuAudioData *hdr )
{
    hdr->version				= av_be2ne32(hdr->version);
    hdr->mode					= av_be2ne32(hdr->mode);
    hdr->channel				= av_be2ne32(hdr->channel);
    hdr->sizeOfAdditionalData	= av_be2ne32(hdr->sizeOfAdditionalData);
    hdr->sizeOfAudioData		= av_be2ne32(hdr->sizeOfAudioData);
    hdr->seconds				= av_be2ne32(hdr->seconds);
    hdr->msecs					= av_be2ne32(hdr->msecs);
}

static const char *     MIME_BOUNDARY_PREFIX1 = "--0plm(";
static const char *     MIME_BOUNDARY_PREFIX2 = "Í-0plm(";
static const char *     MIME_BOUNDARY_PREFIX3 = "ÍÍ0plm(";
static const char *     MIME_BOUNDARY_PREFIX4 = "ÍÍÍplm(";
static const char *     MIME_BOUNDARY_PREFIX5 = "ÍÍÍÍlm(";
static const char *     MIME_BOUNDARY_PREFIX6 = "ÍÍÍÍÍm(";
static const char *     MIME_BOUNDARY_PREFIX7 = "/r--0plm(";

static const char *     MIME_BOUNDARY_SUFFIX = ":Server-Push:Boundary-String)1qaz";
static const char *     MIME_TYPE_JPEG = "image/jpeg";
static const char *     MIME_TYPE_MP4  = "image/admp4";
//static const char *     MIME_TYPE_MP4I = "image/admp4i";
//static const char *     MIME_TYPE_MP4P = "image/admp4p";
static const char *     MIME_TYPE_TEXT = "text/plain";
static const char *     MIME_TYPE_ADPCM = "audio/adpcm";
static const char *     MIME_TYPE_LAYOUT = "data/layout";

/****************************************************************************************************************
 * Function: adpic_probe
 * Desc: used to identify the stream as an ad stream 
 * Params: 
 * Return:
 *  AVPROBE_SCORE_MAX if this straem is identifide as a ad stream 0 if not 
 ****************************************************************************************************************/

static int adpic_probe(AVProbeData *p)
{
    if (p->buf_size <= 6)
        return 0;

    // CS - There is no way this scheme of identification is strong enough. Probably should attempt
    // to parse a full frame out of the probedata buffer
    //0 DATA_JPEG, 
    //1 DATA_JFIF, 
    //2 DATA_MPEG4I, 
    //3 DATA_MPEG4P, 
    //4 DATA_AUDIO_ADPCM, 
    //5 DATA_AUDIO_RAW, 
    //6 DATA_MINIMAL_MPEG4, 
    //7 DATA_MINIMAL_AUDIO_ADPCM, 
    //8 DATA_LAYOUT, 
    //9 DATA_INFO, 
    //10 DATA_H264I, 
    //11 DATA_H264P, 
    //12 DATA_XML_INFO
	
	if (lastProbedCtxt)
		av_free(lastProbedCtxt);
		
	lastProbedCtxt = av_malloc(sizeof(*lastProbedCtxt));
    
	if ((p->buf[DATA_TYPE] <= DATA_XML_INFO) && (p->buf[DATA_CHANNEL] <= 32))  {
		unsigned long dataSize = (p->buf[DATA_SIZE_BYTE_0] << 24) + 
								 (p->buf[DATA_SIZE_BYTE_1] << 16) + 
								 (p->buf[DATA_SIZE_BYTE_2] << 8 ) + 
								 p->buf[DATA_SIZE_BYTE_3];
		if (dataSize <= 0xFFFF)  {
			lastProbedCtxt->isBinary = TRUE;
			lastProbedCtxt->isMIME = FALSE;
			return AVPROBE_SCORE_MAX;
		}
	}
    else
    {
        int server_adjustment = 0;

        // This is nasty but it's got to go here as we don't want to try and deal with fixes for certain server nuances in the HTTP layer.
        // DS 2 servers seem to end their HTTP header section with the byte sequence, 0x0d, 0x0a, 0x0d, 0x0a, 0x0a
        // Eco 9 server ends its HTTP headers section with the sequence,              0x0d, 0x0a, 0x0d, 0x0a, 0x0d, 0x0a
        // Both of which are incorrect. We'll try and detect these cases here and make adjustments to the buffers so that a standard validation
        // routine can be called...
        // DS2 detection
        if( p->buf[0] == 0x0a )
        {
            server_adjustment = 1;
        }
        else if( p->buf[0] == 0x0d &&  p->buf[1] == 0x0a )  // Eco 9 detection
        {
            server_adjustment = 2;
        }


        /* Good start, Now check whether we have the start of a MIME boundary separator */
        if( adpic_is_valid_separator( &p->buf[server_adjustment], p->buf_size - server_adjustment ) > 0 )
        {
			lastProbedCtxt->isBinary = FALSE;
			lastProbedCtxt->isMIME = TRUE;
            return AVPROBE_SCORE_MAX;
        }
    }

    return 0;
}

/****************************************************************************************************************
 * Function: adpic_is_valid_separator
 * Desc: Validates a multipart MIME boundary separator against the convention used by NetVu video servers
 * Params: 
 *   buf - Buffer containing the boundary separator
 *   bufLen - Size of the buffer
 * Return:
 *  1 if boundary separator is valid, 0 if not
 ****************************************************************************************************************/
static int adpic_is_valid_separator( unsigned char * buf, int bufLen )
{
    int is_valid = 0;

    if( buf != NULL )
    {
        if( bufLen > strlen(MIME_BOUNDARY_PREFIX1) + strlen(MIME_BOUNDARY_SUFFIX) )
        {
            if( (strncmp( buf, MIME_BOUNDARY_PREFIX1, strlen(MIME_BOUNDARY_PREFIX1) ) == 0) ||
                (strncmp( buf, MIME_BOUNDARY_PREFIX2, strlen(MIME_BOUNDARY_PREFIX2) ) == 0) ||
                (strncmp( buf, MIME_BOUNDARY_PREFIX3, strlen(MIME_BOUNDARY_PREFIX3) ) == 0) ||
                (strncmp( buf, MIME_BOUNDARY_PREFIX4, strlen(MIME_BOUNDARY_PREFIX4) ) == 0) ||
                (strncmp( buf, MIME_BOUNDARY_PREFIX5, strlen(MIME_BOUNDARY_PREFIX5) ) == 0) ||
                (strncmp( buf, MIME_BOUNDARY_PREFIX6, strlen(MIME_BOUNDARY_PREFIX6) ) == 0) || 
                (strncmp( buf, MIME_BOUNDARY_PREFIX7, strlen(MIME_BOUNDARY_PREFIX7) ) == 0)    )
            {
                unsigned char *     b = &buf[strlen(MIME_BOUNDARY_PREFIX1)];

                /* We're getting closer. Now we have a server type string. We must skip past this */
                while( !isspace(*b) && *b != ':' && (b - buf) < bufLen )
                {
                    b++;
                }

                if( *b == ':' )
                {
                    if( (b - buf) + strlen(MIME_BOUNDARY_SUFFIX)  <= bufLen )
                    {
                        if( strncmp( b, MIME_BOUNDARY_SUFFIX, strlen(MIME_BOUNDARY_SUFFIX) ) == 0 )
                        {
                            is_valid = 1; /* Flag this fact so that we can adjust the parser later */
                            return AVPROBE_SCORE_MAX;
                        }
                    }
                }
            }
        }
    }

    return is_valid;
}

static int adpic_read_close(AVFormatContext *s)
{
    return 0;
}

static int adpic_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
	AdpicContext*   adpicContext = s->priv_data;
	ByteIOContext*	pb = s->pb;
	URLContext*		urlContext = pb->opaque;
	NetvuContext*	netvu = NULL;


	s->ctx_flags |= AVFMTCTX_NOHEADER;
	
	if (urlContext && urlContext->is_streamed)
		netvu = urlContext->priv_data;
	
	if (netvu)  {
		if (adpicContext)  {
			adpicContext->utc_offset	= netvu->utc_offset;
			adpicContext->isBinary		= netvu->isBinary;
			if (netvu->boundry)
				adpicContext->isMIME		= TRUE;
			else
				adpicContext->isMIME		= FALSE;
		}
		av_metadata_set2(&s->metadata, "server",		netvu->server, 		0);
		av_metadata_set2(&s->metadata, "content",		netvu->content, 	0);
		av_metadata_set2(&s->metadata, "resolution",	netvu->resolution, 	0);
		av_metadata_set2(&s->metadata, "compression",	netvu->compression, 0);
		av_metadata_set2(&s->metadata, "rate",			netvu->rate, 		0);
		av_metadata_set2(&s->metadata, "pps",			netvu->pps, 		0);
		av_metadata_set2(&s->metadata, "site_id",		netvu->site_id, 	0);
		//av_metadata_set2(&s->metadata, "boundry",		netvu->boundry, 	0);
	}
	else if (lastProbedCtxt) {
		*adpicContext = *lastProbedCtxt;
		av_free(lastProbedCtxt);
		lastProbedCtxt = NULL;
	}
					
	return 0;
}

static AVStream *get_stream(struct AVFormatContext *s, NetVuImageData *pic)
{
    int xres = pic->format.target_pixels;
    int yres = pic->format.target_lines;
    int codec_type, codec_id, id;
    int i, found;
    AVStream *st;

	switch(pic->vid_format)
	{
	    case PIC_MODE_JPEG_422:
	    case PIC_MODE_JPEG_411:
		    codec_id = CODEC_ID_MJPEG;
		    codec_type = 0;
		break;

	    case PIC_MODE_MPEG4_411:
	    case PIC_MODE_MPEG4_411_I:
	    case PIC_MODE_MPEG4_411_GOV_P:
	    case PIC_MODE_MPEG4_411_GOV_I:
            codec_id = CODEC_ID_MPEG4;
		    codec_type = 1;
        break;

        case PIC_MODE_H264I:
        case PIC_MODE_H264P:
        case PIC_MODE_H264J:
		    codec_id = CODEC_ID_H264;
		    codec_type = 2;
		break;

	    default:
		    //logger(LOG_DEBUG,"ADPIC: get_stream, unrecognised vid_format %d\n", pic->vid_format);
		return NULL;
	}
	found = FALSE;
	id = (codec_type<<31)|((pic->cam-1)<<24)|((xres>>4)<<12)|((yres>>4)<<0);
	for (i=0; i<s->nb_streams; i++)
	{
		st = s->streams[i];
		if (st->id == id)
		{
			found = TRUE;
			break;
		}
	}
	if (!found)
	{
		st = av_new_stream( s, id);
		if (st)
		{
		    st->codec->codec_type = CODEC_TYPE_VIDEO;
		    st->codec->codec_id = codec_id;
			st->codec->width = pic->format.target_pixels;
			st->codec->height = pic->format.target_lines;
			st->index = i;
			
			// Set pixel aspect ratio, display aspect is (sar * width / height)
			// May get overridden by codec
			if( (st->codec->width  > 360) && (st->codec->height <= 480) )  {
				st->sample_aspect_ratio = (AVRational){1, 2};
			}
			else  {
				st->sample_aspect_ratio = (AVRational){1, 1};
			}
			
			// Use milliseconds as the time base
			st->r_frame_rate = (AVRational){1,1000};
			av_set_pts_info(st, 32, 1, 1000);
			st->codec->time_base = (AVRational){1,1000};
			
			av_metadata_set2(&st->metadata, "title", pic->title, 0);
		}
	}
	return st;
}

static AVStream *get_audio_stream( struct AVFormatContext *s, NetVuAudioData* audioHeader )
{
    int id;
    int i, found;
    AVStream *st;

	found = FALSE;

	id = AUDIO_STREAM_ID;

	for( i = 0; i < s->nb_streams; i++ )
	{
		st = s->streams[i];
		if( st->id == id )
		{
			found = TRUE;
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
            // Should probably fill in other known values here. Like bit rate etc.
			
			// Use milliseconds as the time base
			st->r_frame_rate = (AVRational){1,1000};
			av_set_pts_info(st, 32, 1, 1000);
			st->codec->time_base = (AVRational){1,1000};
			
			switch(audioHeader->mode)  {
				case(AUD_MODE_AUD_ADPCM_8000):
				case(AUD_MODE_AUD_L16_8000):
					st->codec->sample_rate = 8000;
					break;
				case(AUD_MODE_AUD_ADPCM_16000):
				case(AUD_MODE_AUD_L16_16000):
					st->codec->sample_rate = 16000;
					break;
				case(AUD_MODE_AUD_L16_44100):
				case(AUD_MODE_AUD_ADPCM_44100):
					st->codec->sample_rate = 441000;
					break;
				case(AUD_MODE_AUD_ADPCM_11025):
				case(AUD_MODE_AUD_L16_11025):
					st->codec->sample_rate = 11025;
					break;
				case(AUD_MODE_AUD_ADPCM_22050):
				case(AUD_MODE_AUD_L16_22050):
					st->codec->sample_rate = 22050;
					break;
				case(AUD_MODE_AUD_ADPCM_32000):
				case(AUD_MODE_AUD_L16_32000):
					st->codec->sample_rate = 32000;
					break;
				case(AUD_MODE_AUD_ADPCM_48000):
				case(AUD_MODE_AUD_L16_48000):
					st->codec->sample_rate = 48000;
					break;
				case(AUD_MODE_AUD_L16_12000):
					st->codec->sample_rate = 12000;
					break;
				case(AUD_MODE_AUD_L16_24000):
					st->codec->sample_rate = 24000;
					break;
				default:
					st->codec->sample_rate = 8000;
					break;
			}
			st->codec->codec_tag = 0x0012;

			st->index = i;
		}
	}

	return st;
}

/****************************************************************************************************************
 * Function: get_data_stream
 * Desc: Returns the data stream for associated with the current connection. If there isn't one already, a new 
 *       one will be created and added to the AVFormatContext passed in
 * Params: 
 *   s - Pointer to AVFormatContext associated with the active connection
 * Return:
 *  Pointer to the data stream on success, NULL on failure
 ****************************************************************************************************************/
static AVStream * get_data_stream( struct AVFormatContext *s ) 
{
    int id;
    int i, found;
    AVStream *st;

	found = FALSE;

	id = DATA_STREAM_ID;

	for( i = 0; i < s->nb_streams; i++ )
	{
		st = s->streams[i];
		if( st->id == id )
		{
			found = TRUE;
			break;
		}
	}

    // Did we find our audio stream? If not, create a new one
	if( !found )
	{
		st = av_new_stream( s, id );
		if (st)
		{
            st->codec->codec_type = CODEC_TYPE_DATA;
		    st->codec->codec_id = 0;
            st->codec->channels = 0;
            st->codec->block_align = 0;
            // Should probably fill in other known values here. Like bit rate, sample rate, num channels, etc

			st->index = i;
		}
	}

	return st;
}

static int adpic_parse_mime_header( ByteIOContext *pb, int *dataType, int *size, long *extra )
{
    unsigned char               buffer[TEMP_BUFFER_SIZE];
    unsigned char *             q = NULL;
    int                         ch, err, lineCount = 0;

    q = buffer;
    /* Try and parse the header */
    for(;;) 
    {
        /* ch = get_byte(pb); */
        ch = url_fgetc( pb );

        if (ch < 0)
        { return 1; }

        if (ch == '\n') 
        {
            /* process line */
            if (q > buffer && q[-1] == '\r')
                q--;
            *q = '\0';

            //#ifdef DEBUG
            //    printf("header='%s'\n", buffer);
            //#endif

            err = process_line( buffer, &lineCount, dataType, size, extra );
            /* First line contains a \n */
            if( !(err == 0 && lineCount == 0) )
            {
                if( err < 0 )
                    return err;

                if( err == 0 )
                {
                    return 0;
                }
                lineCount++;
            }

            q = buffer;
        } 
        else 
        {
            if ((q - buffer) < sizeof(buffer) - 1)
                *q++ = ch;
        }
    }

   
    return 1;
}

static int process_line( char *line, int *line_count, int *dataType, int *size, long *extra )
{
    char        *tag, *p = NULL;
    int         http_code = 0;

    /* end of header */
    if (line[0] == '\0')
        return 0;

    p = line;

    //NOTE the boundry string is missing some times so check for the HTTP header and scip to line 1
    if(*line_count == 0 && 'H' == *(p) && 'T' == *(p+1) && 'T' == *(p+2) && 'P' == *(p+3))
    {
        *line_count = 1;
    }
    
    //if(*line_count == 0 && (0xFF && *(p)) && (0xD8 && *(p+1)) && (0xFF && *(p+2)) && (0xE0 && *(p+3)))
    //{
    //    return -1;
    //}

    /* The first valid line will be the boundary string - validate this here */
    if( *line_count == 0 )
    {
        if( adpic_is_valid_separator( p, strlen(p) ) == FALSE )
            return -1;
    }
    else if( *line_count == 1 ) /* The second line will contain the HTTP status code */
    {
        while (!isspace(*p) && *p != '\0')
            p++;
        while (isspace(*p))
            p++;
        http_code = strtol(p, NULL, 10);
        //#ifdef DEBUG
        //        printf("http_code=%d\n", http_code);
        //#endif
    } 
    else /* Any other line we are just looking for particular headers - if we find them, we fill in the appropriate output data */
    {
        while (*p != '\0' && *p != ':')
            p++;
        if (*p != ':')
            return 1;

        *p = '\0';
        tag = line;
        p++;
        while (isspace(*p))
            p++;

        if (!strcmp(tag, "Content-length")) {
            *size = strtol(p, NULL, 10); /* Get the size */

            if( size == 0 )
                return -1;
        }

        if (!strcmp(tag, "Content-type")) {
            /* Work out what type we actually have */
            if( strcmp( av_strlwr(p), MIME_TYPE_JPEG ) == 0 )
            {
                *dataType = DATA_JFIF;
            }
            else if( memcmp( av_strlwr(p), MIME_TYPE_MP4, strlen(MIME_TYPE_MP4) ) == 0 ) /* Or if it starts image/mp4  - this covers all the supported mp4 variations (i and p frames) */
            {
                *dataType = DATA_MPEG4P; /* P for now - as they are both processed the same subsequently, this is sufficient */
            }
            else if( strcmp( av_strlwr(p), MIME_TYPE_TEXT ) == 0 )
            {
                *dataType = DATA_PLAINTEXT;
            }
            else if( strcmp( av_strlwr(p), MIME_TYPE_LAYOUT ) == 0 )
            {
                *dataType = DATA_LAYOUT;
            }
            else if( memcmp( av_strlwr(p), MIME_TYPE_ADPCM, strlen(MIME_TYPE_ADPCM) ) == 0 )
            {
                *dataType = DATA_AUDIO_ADPCM;

                // If we find audio in a mime header, we need to extract the mode out. The header takes the form,
                // Content-Type: audio/adpcm;rate=<mode>
                while (*p != '\0' && *p != ';')
                    p++;

                if( *p != ';' )
                    return 1;

                p++;
                while (isspace(*p))
                    p++;

                // p now pointing at the rate. Look for the first '='
                while (*p != '\0' && *p != '=')
                    p++;
                if (*p != '=')
                    return 1;

                p++;

                tag = p;

                while( *p != '\0' && !isspace(*p) )
                    p++;

                if( *p != '\0' )
                    *p = '\0';

                // p pointing at the rate
                *extra = strtol(tag, NULL, 10); /* convert the rate string into a long */

                // Map the rate to a RTP payload value for consitency - the other audio headers all contain mode values in this format.
                if( *extra == 8000 )
                    *extra = RTP_PAYLOAD_TYPE_8000HZ_ADPCM;
                else if( *extra == 11025 )
                    *extra = RTP_PAYLOAD_TYPE_11025HZ_ADPCM;
                else if( *extra == 16000 )
                    *extra = RTP_PAYLOAD_TYPE_16000HZ_ADPCM;
                else if( *extra == 22050 )
                    *extra = RTP_PAYLOAD_TYPE_22050HZ_ADPCM;
                else if( *extra == 32000 )
                    *extra = RTP_PAYLOAD_TYPE_32000HZ_ADPCM;
                else if( *extra == 44100 )
                    *extra = RTP_PAYLOAD_TYPE_44100HZ_ADPCM;
                else if( *extra == 48000 )
                    *extra = RTP_PAYLOAD_TYPE_48000HZ_ADPCM;
                else
                    *extra = RTP_PAYLOAD_TYPE_8000HZ_ADPCM; // Default
            }
        }
    }
    return 1;
}

static int adpic_parse_mp4_text_data( unsigned char *mp4TextData, int bufferSize, NetVuImageData *video_data, char **additionalTextData )
{
    unsigned char               buffer[TEMP_BUFFER_SIZE];
    int                         ch, err;
    unsigned char *             q = NULL;
    int                         lineCount = 0;
    unsigned char *             currentChar = mp4TextData;
    struct tm                   time;

    memset( &time, 0, sizeof(struct tm) );

    /* Try and parse the header */
    q = buffer;
    for(;;) {
        ch = *currentChar++;

        if (ch < 0)
            return 1;

        if (ch == '\n') 
        {
            /* process line */
            if (q > buffer && q[-1] == '\r')
                q--;
            *q = '\0';

            err = process_mp4data_line( buffer, lineCount, video_data, &time, additionalTextData );

            if( err < 0 )
                return err;

            if( err == 0 )
                return 0;

            /* Check we're not at the end of the buffer. If the following statement is true and we haven't encountered an error then we've finished parsing the buffer */
            if( err == 1 )
            {
                /* CS - Not particularly happy with this code but it seems there's little consistency in the way these buffers end. This block catches all of those
                   variations. The variations that indicate the end of a MP4 MIME text block are:

                   1. The amount of buffer parsed successfully is equal to the total buffer size

                   2. The buffer ends with two NULL characters

                   3. The buffer ends with the sequence \r\n\0
                */

                if( currentChar - mp4TextData == bufferSize )
                    return 0;

                /* CS - I *think* lines should end either when we've processed all the buffer OR it's padded with 0s */
                /* Is detection of a NULL character here sufficient? */
                if( *currentChar == '\0' )
                    return 0;

/* The following code is how the original implementation was. See the comment above for indications of the buffer endings I've seen. I've since then seen one that ends
   \r\n\0\0\0. Some server code just dumping a memory buffer perhaps and therefore leaving padding bytes in? */
#if 0
                /* CS - This is horrible but has to go in as there are discrepancies between servers. Some servers appended two 0 bytes after the last \r\n which screws up the generic line extraction routine.
                   This code catches that case and returns as appropriate */
                /* Actually, I don't think the above statement is true, more likely that the discrepancy is between packets ^ */
                if( currentChar - mp4TextData == bufferSize - 2 && *currentChar == '\0' && *(currentChar+1) == '\0' )
                    return 0;

                /* CS - Another variation on the end of buffer - This one detects the case where the buffer is terminated with the sequence \r\n\0*/
                if( currentChar - mp4TextData == bufferSize - 1 && *currentChar == '\0' && *(currentChar-1) == '\n' && *(currentChar-2) == '\r' )
                    return 0;
#endif
            }

            lineCount++;
            q = buffer;
        } 
        else 
        {
            if ((q - buffer) < sizeof(buffer) - 1)
                *q++ = ch;
        }
    }

    return 1;
}

static int process_mp4data_line( char *line, int line_count, NetVuImageData *video_data, struct tm *time, char ** additionalText )
{
    char        *tag = NULL, *p = NULL;
    int         lineLen = 0;

    /* end of header */
    if (line[0] == '\0')
        return 0;

    p = line;

 
    while (*p != '\0' && *p != ':')
        p++;
    if (*p != ':')
        return 1;

    /* *p = '\0';          CS - Removing this additional of NULL terminator so that whole string can be copied later with ease 
                           subsequent string operations have been changed to memory ops to remove the dependency on NULL temrinator */

    tag = line;
    p++;

    while( *p != '\0' && *p == ' '  )  /* While the current char is a space */
        p++;

    if (*p == '\0')
        return 1;
    else
    {
        char * temp = p;

        /* Get the length of the rest of the line */
        while( *temp != '\0' )
            temp++;

        lineLen = temp - line;
    }

    if( !memcmp( tag, "Number", strlen( "Number" ) ) )
    {
        video_data->cam = strtol(p, NULL, 10); /* Get the camera number */
    }
    else if( !memcmp( tag, "Name", strlen( "Name" ) ) )
    {
        memcpy( video_data->title, p, FFMIN( TITLE_LENGTH, strlen(p) ) );
    }
    else if( !memcmp( tag, "Version", strlen( "Version" ) ) )
    {
        video_data->version = strtol(p, NULL, 10); /* Get the version number */
    }
    else if( !memcmp( tag, "Date", strlen( "Date" ) ) )
    {
		sscanf( p, "%d/%d/%d", &time->tm_mday, &time->tm_mon, &time->tm_year );
		time->tm_year -= 1900; /* Windows uses 1900, not 1970 */
		time->tm_mon--;

        if( time->tm_sec != 0 || time->tm_min != 0 || time->tm_hour != 0 )
            video_data->session_time = mktime( time );
    }
    else if( !memcmp( tag, "Time", strlen( "Time" ) ) )
    {
        sscanf( p, "%d:%d:%d", &time->tm_hour, &time->tm_min, &time->tm_sec );

        if( time->tm_year != 0 )
            video_data->session_time = mktime( time );
    }
    else if( !memcmp( tag, "MSec", strlen( "MSec" ) ) )
    {
        video_data->milliseconds = strtol(p, NULL, 10); /* Get the millisecond offset */
    }
    else if( !memcmp( tag, "Locale", strlen( "Locale" ) ) )
    {
        /* Get the locale */
        memcpy( video_data->locale, p, FFMIN( MAX_NAME_LEN, strlen(p) ) );
    }
    else if( !memcmp( tag, "UTCoffset", strlen( "UTCoffset" ) ) )
    {
        video_data->utc_offset = strtol(p, NULL, 10); /* Get the timezone */
    }
    else
    {
        /* Any lines that aren't part of the pic struct, tag onto the additional text block */
        if( additionalText != NULL && lineLen > 0 )
        {
#define LINE_END_LEN        3
            int             strLen  = 0;
            const char      lineEnd[LINE_END_LEN] = { '\r', '\n', '\0' };

            /* Get the length of the existing text block if it exists */
            if( *additionalText != NULL )
                strLen = strlen( *additionalText );

            /* Ok, now allocate some space to hold the new string */
            *additionalText = av_realloc( *additionalText, strLen + lineLen + LINE_END_LEN );

            /* Copy the line into the text block */
            memcpy( &(*additionalText)[strLen], line, lineLen );

            /* Add a NULL terminator */
            memcpy( &(*additionalText)[strLen + lineLen], lineEnd, LINE_END_LEN );
        }
    }

    return 1;
}

static int adpicSkipInfoList(ByteIOContext * pb)
{
	int  ch;
	char *restore;
	restore = pb->buf_ptr;
	
	if(adpicFindTag("infolist", pb, 0))
	{
		if(adpicFindTag("/infolist", pb, (4*1024)))
		{ 
			ch = url_fgetc(pb);
			if(ch==0x0D)//'/r'
			{ 
                ch = url_fgetc(pb);
				if(ch==0x0A)//'/n'
				{
					ch = url_fgetc(pb);
					return 1; 
				}
			}
		}
	}
	else
	{ 
		pb->buf_ptr = restore; 
		return 0;
	}

	return -1;
}


static int adpicFindTag(const char *Tag, ByteIOContext *pb, int MaxLookAhead)
{
	//NOTE Looks for the folowing pattern "<infoList>" at the begining of the 
	//     buffer return 1 if its found and 0 if not

    int                         LookAheadPos = 0;
    unsigned char               buffer[TEMP_BUFFER_SIZE];
    unsigned char               *q = buffer;
    int                         ch;
	int                         endOfTag = 0;
    
    if(MaxLookAhead<0)
	{ MaxLookAhead = 0; }

	for(LookAheadPos=0; LookAheadPos<=MaxLookAhead; LookAheadPos++) 
    {
		endOfTag = 0;
        ch = url_fgetc( pb );

        if (ch < 0)
        { return 0; }

        if (ch == '<' )
		{			
			while(endOfTag==0)
			{
				ch = url_fgetc( pb );

                if (ch < 0)
                { return 0; }

				if(ch == '>')
				{
					endOfTag = 1;
                    *q = '\0';
	                q = buffer;

					if(strcmp(av_strlwr(buffer), Tag)==0)
					{ 
						return 1;
					}
					else
					{ 
						endOfTag = 1; 
					}
				}
				else
				{
					if ((q - buffer) < sizeof(buffer) - 1)
			        { *q++ = ch; }
				}
			}
		}
    }
 
	return 0;
}

static int adpic_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
	AdpicContext*			adpicContext = s->priv_data;
    ByteIOContext *         pb = s->pb;
    AVStream *              st = NULL;
    FrameData *             frameData = NULL;
    NetVuAudioData *        audio_data = NULL;
    NetVuImageData *        video_data = NULL;
    char *                  text_data = NULL;
    unsigned char           ch, ch1;
    unsigned char           adpkt[SEPARATOR_SIZE];
    int                     data_type = MAX_DATA_TYPE;
    int                     data_channel = 0;
    int                     n, size = -1;
    int                     status;
    int                     i =0, BuffSize;
    int                     found = FALSE;
    //char                    scratch[1024];
    long                    extra = 0;
    int                     errorVal = ADPIC_UNKNOWN_ERROR;
    FrameType               currentFrameType = FrameTypeUnknown;
    char *                  restore;
    char *                  ptr;
    int                     manual_size = FALSE;
	int                     result = 0;
    //int loop = 0;
    
    // First read the 6 byte separator
	if ((n=get_buffer(pb, &adpkt[0], SEPARATOR_SIZE)) != SEPARATOR_SIZE)
	{
        if(pb->eof_reached)
        {
            errorVal = ADPIC_END_OF_STREAM;
        }
        else
        {
		    //logger(LOG_DEBUG,"ADPIC: short of data reading seperator, expected %d, read %d\n", SEPARATOR_SIZE, n);
            errorVal = ADPIC_READ_6_BYTE_SEPARATOR_ERROR;
        }
		goto cleanup;
	}

	result = adpicSkipInfoList(pb);
	if(result<0)
	{
		errorVal = ADPIC_FAILED_TO_PARSE_INFOLIST;
		goto cleanup;
	}
	else if(result>0)
	{
		if ((n=get_buffer(pb, &adpkt[0], SEPARATOR_SIZE)) != SEPARATOR_SIZE)
		{
			if(pb->eof_reached)
			{
				errorVal = ADPIC_END_OF_STREAM;
			}
			else
			{
				//logger(LOG_DEBUG,"ADPIC: short of data reading seperator, expected %d, read %d\n", SEPARATOR_SIZE, n);
				errorVal = ADPIC_READ_6_BYTE_SEPARATOR_ERROR;
			}
			goto cleanup;
		}
	}

    // BMOJ - yep not strong enough.
    // CS - There is no way this scheme of identification is strong enough. Probably should attempt
    // to parse a full frame out of the data
    //if ( ((adpkt[0] == 9) && (adpkt[2] == 0) && (adpkt[3] == 0)) || 
    //     ((adpkt[0] >= 0 && adpkt[0] <= 7) && (adpkt[2] == 0) && (adpkt[3] == 0)) ||
    //     ((adpkt[0] >= 0 && adpkt[0] <= 7) && (adpkt[2] == 0) && (adpkt[3] == 1))    )
    if(adpicContext->isBinary)
    {
        // Get info out of the separator
	    memcpy(&size, &adpkt[DATA_SIZE_BYTE_0], 4);
	    size = av_be2ne32(size);
	    data_type = adpkt[DATA_TYPE];
	    data_channel = (adpkt[DATA_CHANNEL]+1);
    }
    else
    {
        pb->buf_ptr -= SEPARATOR_SIZE;
        restore = pb->buf_ptr;
       
        if(adpic_parse_mime_header( pb, &data_type, &size, &extra ) != 0 )
        {
            pb->buf_ptr=restore;

            //NOTE Invalid mime header howeve some times the header is missing and then there is a valid image
            if( (pb->buf_ptr[0] == 0xff) && (pb->buf_ptr[1] == 0xD8))
            {
                //Set the data type
                data_type = DATA_JFIF;
 
                //look ahead through the buffer for end of image
                ch = ch1 = 0;
                i=0;
                while (!found && pb->buf_ptr < pb->buf_end)
                {
                    if(ch==0xFF && ch1 == 0xD9)
                    {
                        found = TRUE;
                    }

                    i++;
                    ch = ch1;
                    ch1 = url_fgetc( pb );
                }

                if(found)
                {
                    BuffSize = i;
                }
                else
                {
                    BuffSize = (pb->buf_ptr - pb->buffer);
                    BuffSize = pb->buffer_size - BuffSize;
                }

                if ((status = adpic_new_packet(pkt, BuffSize))<0) // PRC 003
		        {
			        //logger(LOG_DEBUG,"ADPIC: DATA_JFIF adpic_new_packet %d failed, status %d\n", size, status);
                    errorVal = ADPIC_NEW_PACKET_ERROR;
			        goto cleanup;
		        }

                pb->buf_ptr = restore;
                n=get_buffer(pb, pkt->data, BuffSize);
                    
                if(!found)
                {
                    //end of image not found in buffer to read further 
                    ptr = pkt->data;

                  
                    ch = ch1 = 0;
                    i=0;
                    while(!(ch==0xFF && ch1==0xD9) && (pb->buf_ptr < pb->buf_end))
                    {
                        i++;
                        ptr++;

                        ch = ch1;
                        ch1 = *ptr;
                    }
                }
                size = i;
                manual_size = TRUE;
            }
            else
            { 

                //FILE* fpw = fopen("c:\\adpicLog.txt", "a");
                //fprintf(fpw, "Start of log\n");

                //for(loop=0; loop<10; loop++)
                //{
                //    fprintf(fpw, "%c", pb->buf_ptr[loop]);
                //    fflush(fpw);   
                //}
                //fprintf(fpw, "\nEnd of log\n");

                //fflush(fpw);
                //fclose(fpw); 


                errorVal = ADPIC_PARSE_MIME_HEADER_ERROR;
                goto cleanup;
            }
        }
    }

    // Prepare for video or audio read
    if( data_type == DATA_JPEG || data_type == DATA_JFIF || data_type == DATA_MPEG4I || data_type == DATA_MPEG4P 
        || data_type == DATA_MINIMAL_MPEG4 || data_type == DATA_H264I || data_type == DATA_H264P )
    {
        currentFrameType = NetVuVideo;

        video_data = av_mallocz( sizeof(NetVuImageData) );

        if( video_data == NULL )
        {
            errorVal = ADPIC_NETVU_IMAGE_DATA_ERROR;
            goto cleanup;
        }
    }
    else if( data_type == DATA_AUDIO_ADPCM || data_type == DATA_MINIMAL_AUDIO_ADPCM )
    {
        currentFrameType = NetVuAudio;

        audio_data = av_mallocz( sizeof(NetVuAudioData) );

        if( audio_data == NULL )
        {
            errorVal = ADPIC_NETVU_AUDIO_DATA_ERROR;
            goto cleanup;
        }
    }
    else if( data_type == DATA_INFO )
    {
        currentFrameType = NetVuDataInfo;
    }
    else if( data_type == DATA_LAYOUT )
    {
        currentFrameType = NetVuDataLayout;
    }

    // Proceed based on the type of data in this frame
    switch(data_type)
    {
        case DATA_JPEG:
	    {
	        int header_size;
	        char jfif[2048], *ptr;
		    // Read the pic structure
		    if ((n=adpic_get_buffer(pb, (unsigned char*)video_data, sizeof (NetVuImageData))) != sizeof (NetVuImageData))
		    {
			    //logger(LOG_DEBUG,"ADPIC: short of data reading pic struct, expected %d, read %d\n", sizeof (NetVuImageData), n);
                errorVal = ADPIC_JPEG_IMAGE_DATA_READ_ERROR;
			    goto cleanup;
		    }
		    // Endian convert if necessary
		    pic_network2host(video_data);
		    if (!pic_version_valid(video_data->version))
		    {
			    //logger(LOG_DEBUG,"ADPIC: invalid pic version 0x%08X\n", video_data->version);
                errorVal = ADPIC_JPEG_PIC_VERSION_ERROR;
			    goto cleanup;
		    }
            
            /* Get the additional text block */
            text_data = av_malloc( video_data->start_offset + 1 );
            if( text_data == NULL )
            {
                errorVal = ADPIC_JPEG_ALOCATE_TEXT_BLOCK_ERROR;
                goto cleanup;
            }

		    /* Copy the additional text block */
		    if( (n = adpic_get_buffer( pb, text_data, video_data->start_offset )) != video_data->start_offset )
		    {
			    //logger(LOG_DEBUG,"ADPIC: short of data reading start_offset data, expected %d, read %d\n", size, n);
                errorVal = ADPIC_JPEG_READ_TEXT_BLOCK_ERROR;
			    goto cleanup;
		    }

            /* CS - somtimes the buffer seems to end with a NULL terminator, other times it doesn't. I'm adding a NULL temrinator here regardless */
            text_data[video_data->start_offset] = '\0';

		    // Use the pic struct to build a JFIF header 
		    if ((header_size = build_jpeg_header( jfif, video_data, FALSE, 2048))<=0)
		    {
			    //logger(LOG_DEBUG,"ADPIC: adpic_read_packet, build_jpeg_header failed\n");
                errorVal = ADPIC_JPEG_HEADER_ERROR;
			    goto cleanup;
		    }
		    // We now know the packet size required for the image, allocate it.
		    if ((status = adpic_new_packet(pkt, header_size+video_data->size+2))<0) // PRC 003
		    {
                errorVal = ADPIC_JPEG_NEW_PACKET_ERROR;
			    goto cleanup;
		    }
		    ptr = pkt->data;
		    // Copy the JFIF header into the packet
		    memcpy(ptr, jfif, header_size);
		    ptr += header_size;
		    // Now get the compressed JPEG data into the packet
		    // Read the pic structure
		    if ((n=adpic_get_buffer(pb, ptr, video_data->size)) != video_data->size)
		    {
			    //logger(LOG_DEBUG,"ADPIC: short of data reading pic body, expected %d, read %d\n", video_data->size, n);
                errorVal = ADPIC_JPEG_READ_BODY_ERROR;
			    goto cleanup;
		    }
		    ptr += video_data->size;
		    // Add the EOI marker
		    *ptr++ = 0xff;
		    *ptr++ = 0xd9;
	    }
	    break;

        case DATA_JFIF:
	    {
            if(!manual_size)
            {
		        if ((status = adpic_new_packet(pkt, size))<0) // PRC 003
		        {
			        //logger(LOG_DEBUG,"ADPIC: DATA_JFIF adpic_new_packet %d failed, status %d\n", size, status);
                    errorVal = ADPIC_JFIF_NEW_PACKET_ERROR;
			        goto cleanup;
		        }

		        if ((n=adpic_get_buffer(pb, pkt->data, size)) != size)
		        {
			        //logger(LOG_DEBUG,"ADPIC: short of data reading jfif image, expected %d, read %d\n", size, n);
                    errorVal = ADPIC_JFIF_GET_BUFFER_ERROR;
			        goto cleanup;
		        }
            }
		    if ( parse_jfif_header( pkt->data, video_data, size, NULL, NULL, NULL, TRUE, &text_data ) <= 0)
		    {
			    //logger(LOG_DEBUG,"ADPIC: adpic_read_packet, parse_jfif_header failed\n");
                errorVal = ADPIC_JFIF_MANUAL_SIZE_ERROR;
			    goto cleanup;
		    }
	    }
	    break;

        case DATA_MPEG4I:
        case DATA_MPEG4P:
        case DATA_H264I:
        case DATA_H264P:
	    {
            /* We have to parse the data for this frame differently depending on whether we're getting a MIME or binary stream */
            if( TRUE == adpicContext->isMIME )
            {
                int mimeBlockType = 0;

                /* Allocate a new packet to hold the frame's image data */
		        if( (status = adpic_new_packet( pkt, size )) < 0 )
                {
                    errorVal = ADPIC_MPEG4_MIME_NEW_PACKET_ERROR;
                    goto cleanup;
                }

                /* Now read the frame data into the packet */
                if( (n = adpic_get_buffer( pb, pkt->data, size )) != size )
                {
                    errorVal = ADPIC_MPEG4_MIME_GET_BUFFER_ERROR;
                    goto cleanup;
                }

                /* Now we should have a text block following this which contains the frame data that we can place in a _image_data struct */
                if(  adpic_parse_mime_header( pb, &mimeBlockType, &size, &extra ) != 0 )
                {
                    errorVal = ADPIC_MPEG4_MIME_PARSE_HEADER_ERROR;
                    goto cleanup;
                }

                /* Validate the data type and then extract the text buffer */
                if( mimeBlockType == DATA_PLAINTEXT )
                {
                    unsigned char *         textBuffer = av_malloc( size );

                    if( textBuffer != NULL )
                    {
                        if( (n = adpic_get_buffer( pb, textBuffer, size )) == size )
                        {
                            /* Now parse the text buffer and populate the _image_data struct */
                            if( adpic_parse_mp4_text_data( textBuffer, size, video_data, &text_data ) != 0 )
                            {
                                errorVal = ADPIC_MPEG4_MIME_PARSE_TEXT_DATA_ERROR;
                                av_free( textBuffer );
                                goto cleanup;
                            }

                            /* Remember to set the format so that the right codec is loaded for this stream */
                            video_data->vid_format = PIC_MODE_MPEG4_411;
                        }
                        else
                        {
                            av_free( textBuffer );
                            errorVal = ADPIC_MPEG4_MIME_GET_TEXT_BUFFER_ERROR;
                            goto cleanup;
                        }

                        av_free( textBuffer );
                    }
                    else
                    {
                        errorVal = ADPIC_MPEG4_MIME_ALOCATE_TEXT_BUFFER_ERROR;
                        goto cleanup;
                    }
                }
            }
            else
            {
		        if ((n=adpic_get_buffer(pb, (unsigned char*)video_data, sizeof (NetVuImageData))) != sizeof (NetVuImageData))
		        {
			        //logger(LOG_DEBUG,"ADPIC: short of data reading pic struct, expected %d, read %d\n", sizeof (NetVuImageData), n);
                    errorVal = ADPIC_MPEG4_GET_BUFFER_ERROR;
			        goto cleanup;
		        }
		        pic_network2host(video_data);
		        if (!pic_version_valid(video_data->version))
		        {
                    //logger(LOG_DEBUG,"ADPIC: invalid pic version 0x%08X\n", video_data->version);
                    errorVal = ADPIC_MPEG4_PIC_VERSION_VALID_ERROR;
			        goto cleanup;
		        }

                /* Get the additional text block */
                text_data = av_malloc( video_data->start_offset + 1 );

                if( text_data == NULL )
                {
                    errorVal = ADPIC_MPEG4_ALOCATE_TEXT_BUFFER_ERROR;
                    goto cleanup;
                }

		        /* Copy the additional text block */
		        if( (n = adpic_get_buffer( pb, text_data, video_data->start_offset )) != video_data->start_offset )
		        {
			        //logger(LOG_DEBUG,"ADPIC: short of data reading start_offset data, expected %d, read %d\n", size, n);
                    errorVal = ADPIC_MPEG4_GET_TEXT_BUFFER_ERROR;
			        goto cleanup;
		        }

                /* CS - somtimes the buffer seems to end with a NULL terminator, other times it doesn't. I'm adding a NULL temrinator here regardless */
                text_data[video_data->start_offset] = '\0';

		        if ((status = adpic_new_packet(pkt, video_data->size))<0) // PRC 003
		        {
			        //logger(LOG_DEBUG,"ADPIC: DATA_MPEG4 adpic_new_packet %d failed, status %d\n", video_data->size, status);
                    errorVal = ADPIC_MPEG4_NEW_PACKET_ERROR;
			        goto cleanup;
		        }
		        if ((n=adpic_get_buffer(pb, pkt->data, video_data->size)) != video_data->size)
		        {
			        //logger(LOG_DEBUG,"ADPIC: short of data reading pic body, expected %d, read %d\n", video_data->size, n);
                    errorVal = ADPIC_MPEG4_PIC_BODY_ERROR;
			        goto cleanup;
		        }
            }
	    }	
	    break;

        /* CS - Support for minimal MPEG4 */
        case DATA_MINIMAL_MPEG4:
        {
            MinimalVideoHeader      videoHeader;
            int                     dataSize = size - sizeof(MinimalVideoHeader);

            /* Get the minimal video header */
	        if( (n = adpic_get_buffer( pb, (unsigned char*)&videoHeader, sizeof(MinimalVideoHeader) )) != sizeof(MinimalVideoHeader) )
            {
                errorVal = ADPIC_MPEG4_MINIMAL_GET_BUFFER_ERROR;
		        goto cleanup;
            }

            videoHeader.t  = av_be2ne32(videoHeader.t);
            videoHeader.ms = av_be2ne16(videoHeader.ms);

            /* Copy pertinent data into generic video data structure */
            video_data->cam = data_channel;
            video_data->title[0] = 'C';
            video_data->title[1] = 'a';
            video_data->title[2] = 'm';
            video_data->title[3] = 'e';
            video_data->title[4] = 'r';
            video_data->title[5] = 'a';
            video_data->title[6] = '\n';
            video_data->session_time = videoHeader.t;
            video_data->milliseconds = videoHeader.ms;
			video_data->utc_offset = adpicContext->utc_offset;
			
            /* Remember to identify the type of frame data we have - this ensures the right codec is used to decode this frame */
            video_data->vid_format = PIC_MODE_MPEG4_411;

            /* Now get the main frame data into a new packet */
	        if( (status = adpic_new_packet( pkt, dataSize )) < 0 )
            {
                errorVal = ADPIC_MPEG4_MINIMAL_NEW_PACKET_ERROR;
		        goto cleanup;
            }

	        if( (n = adpic_get_buffer( pb, pkt->data, dataSize )) != dataSize )
            {
                errorVal = ADPIC_MPEG4_MINIMAL_NEW_PACKET_ERROR2;
		        goto cleanup;
            }
        }
        break;

        /* Support for minimal audio frames */
        case DATA_MINIMAL_AUDIO_ADPCM:
        {
            MinimalAudioHeader      audioHeader;
            int                     dataSize = size - sizeof(MinimalAudioHeader);

            /* Get the minimal video header */
	        if( (n = adpic_get_buffer( pb, (unsigned char*)&audioHeader, sizeof(MinimalAudioHeader) )) != sizeof(MinimalAudioHeader) )
            {
                errorVal = ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR;
		        goto cleanup;
            }

            audioHeader.t    = av_be2ne32(audioHeader.t);
            audioHeader.ms   = av_be2ne16(audioHeader.ms);
            audioHeader.mode = av_be2ne16(audioHeader.mode);

            /* Copy pertinent data into generic audio data structure */
            audio_data->mode = audioHeader.mode;
            audio_data->seconds = audioHeader.t;
            audio_data->msecs = audioHeader.ms;

            /* Now get the main frame data into a new packet */
	        if( (status = adpic_new_packet( pkt, dataSize )) < 0 )
            {
                errorVal = ADPIC_MINIMAL_AUDIO_ADPCM_NEW_PACKET_ERROR;
		        goto cleanup;
            }

	        if( (n = adpic_get_buffer( pb, pkt->data, dataSize )) != dataSize )
            {
                errorVal = ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR2;
		        goto cleanup;
            }
        }
        break;

        // CS - Support for ADPCM frames
        case DATA_AUDIO_ADPCM:
        {
            if( FALSE == adpicContext->isMIME )
            {
                // Get the fixed size portion of the audio header
                if( (n = adpic_get_buffer( pb, (unsigned char*)audio_data, sizeof(NetVuAudioData) - 4 )) != sizeof(NetVuAudioData) - 4 )
                {
                    errorVal = ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR;
                    goto cleanup;
                }

                // endian fix it...
                audioheader_network2host( audio_data );

                // Now get the additional bytes
                if( audio_data->sizeOfAdditionalData > 0 )
                {
                    audio_data->additionalData = av_malloc( audio_data->sizeOfAdditionalData );
                    if( audio_data->additionalData == NULL )
                    {
                        errorVal = ADPIC_AUDIO_ADPCM_ALOCATE_ADITIONAL_ERROR;
                        goto cleanup;
                    }

                    //ASSERT(audio_data->additionalData);

                    if( (n = adpic_get_buffer( pb, audio_data->additionalData, audio_data->sizeOfAdditionalData )) != audio_data->sizeOfAdditionalData )
                    {
                        errorVal = ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR2;
                        goto cleanup;
                    }
                }
            }
            else
            {
                /* No presentation information is sent with audio frames in a mime stream so there's not a lot we can do here other than
                   ensure the struct contains the size of the audio data */
                audio_data->sizeOfAudioData = size;
                audio_data->mode = extra;
                audio_data->seconds = 0;
                audio_data->msecs = 0;
            }

		    if( (status = adpic_new_packet( pkt, audio_data->sizeOfAudioData )) < 0 )
            {
                errorVal = ADPIC_AUDIO_ADPCM_MIME_NEW_PACKET_ERROR;
			    goto cleanup;
            }

            // Now get the actual audio data
            if( (n = adpic_get_buffer( pb, pkt->data, audio_data->sizeOfAudioData)) != audio_data->sizeOfAudioData )
            { 
                errorVal = ADPIC_AUDIO_ADPCM_MIME_GET_BUFFER_ERROR;
                goto cleanup;
            }
        }
        break;

        // CS - Data info extraction. A DATA_INFO frame simply contains a type byte followed by a text block. Due to its simplicity,
        // we'll just extract the whole block into the AVPacket's data structure and let the client deal with checking its type and
        // parsing its text
        case DATA_INFO:
        {
            // Allocate a new packet
            if( (status = adpic_new_packet( pkt, size )) < 0 )
            {
                errorVal = ADPIC_INFO_NEW_PACKET_ERROR;
                goto cleanup;
            }

            // Get the data
            if( (n = adpic_get_buffer( pb, pkt->data, size)) != size )
            {
                errorVal = ADPIC_INFO_GET_BUFFER_ERROR;
			    goto cleanup;
            }
        }
        break;

        case DATA_LAYOUT:
        {
            /* Allocate a new packet */
            if( (status = adpic_new_packet( pkt, size )) < 0 )
            {
                errorVal = ADPIC_LAYOUT_NEW_PACKET_ERROR;
                goto cleanup;
            }

            /* Get the data */
            if( (n = adpic_get_buffer( pb, pkt->data, size)) != size )
            {
                errorVal = ADPIC_LAYOUT_GET_BUFFER_ERROR;
			    goto cleanup;
            }
        }
        break;

        default:
        {
	        //logger(LOG_DEBUG,"ADPIC: adpic_read_packet, No handler for data_type=%d\n", data_type );
            errorVal = ADPIC_DEFAULT_ERROR;
	        goto cleanup;
        }
        break;
		
    }

    if( currentFrameType == NetVuVideo )
    {
	    // At this point We have a legal pic structure which we use to determine 
	    // which codec stream to use
	    if ( (st = get_stream( s, video_data)) == NULL )
	    {
		    //logger(LOG_DEBUG,"ADPIC: adpic_read_packet, failed get_stream for video\n");
            errorVal = ADPIC_GET_STREAM_ERROR;
		    goto cleanup;
	    }
		else  {
			if (video_data->session_time > 0)  {
				pkt->pts = video_data->session_time;
				pkt->pts *= 1000ULL;
				pkt->pts += video_data->milliseconds & 0x03FF;
			}
			else
				pkt->pts = AV_NOPTS_VALUE;
		}
    }
    else if( currentFrameType == NetVuAudio )
    {
        // Get the audio stream
        if ( (st = get_audio_stream( s, audio_data )) == NULL )
        {
		    //logger(LOG_DEBUG,"ADPIC: adpic_read_packet, failed get_stream for audio\n");
            errorVal = ADPIC_GET_AUDIO_STREAM_ERROR;
		    goto cleanup;
        }
		else  {
			if (audio_data->seconds > 0)  {
				pkt->pts = audio_data->seconds;
				pkt->pts *= 1000ULL;
				pkt->pts += audio_data->msecs & 0x03FF;
			}
			else
				pkt->pts = AV_NOPTS_VALUE;
		}
    }
    else if( currentFrameType == NetVuDataInfo || currentFrameType == NetVuDataLayout )
    {
        // Get or create a data stream
        if ( (st = get_data_stream( s )) == NULL )
        {
            errorVal = ADPIC_GET_INFO_LAYOUT_STREAM_ERROR;
            goto cleanup;
        }
    }

    pkt->stream_index = st->index;

    frameData = av_malloc(sizeof(FrameData));

    if( frameData == NULL )
        goto cleanup;

    frameData->additionalData = NULL;
    frameData->frameType = currentFrameType;

    if( frameData->frameType == NetVuVideo )                // Video frame
    {
        frameData->frameData = video_data;
    }
    else if( frameData->frameType == NetVuAudio )           // Audio frame
    {
        frameData->frameData = audio_data;
    }
    else if( frameData->frameType == NetVuDataInfo || frameData->frameType == NetVuDataLayout )        // Data info
    {
        frameData->frameData = NULL;
    }
    else  // Shouldn't really get here...
    {
        frameData->frameType = FrameTypeUnknown;
        frameData->frameData = NULL;
    }

    if( (frameData->frameType == NetVuAudio || frameData->frameType == NetVuVideo) && text_data != NULL )
    {
        frameData->additionalData = text_data;
    }

    pkt->priv = frameData;

	pkt->duration = 0;

    errorVal = ADPIC_NO_ERROR;

cleanup:
    if( errorVal < 0 )
    {
        /* If there was an error, make sure we release any memory that might have been allocated */
        if( video_data != NULL )
            av_free( video_data );

        if( audio_data != NULL )
            av_free( audio_data );

        if( text_data != NULL )
            av_free( text_data );
    }

	return errorVal;
}


static int adpic_new_packet(AVPacket *pkt, int size)
{
    int     retVal = av_new_packet( pkt, size );

    if( retVal >= 0 )
    {
        // Give the packet its own destruct function
        pkt->destruct = adpic_release_packet;
    }

    return retVal;
}

static void adpic_release_packet( AVPacket *pkt )
{
    if( pkt != NULL )
    {
        if( pkt->priv != NULL )
        {
            // Have a look what type of frame we have and then delete anything inside as appropriate
            FrameData *     frameData = (FrameData *)pkt->priv;

            if( frameData->frameType == NetVuAudio )
            {
                NetVuAudioData *   audioHeader = (NetVuAudioData *)frameData->frameData;

                if( audioHeader->additionalData )
                {
                    av_free( audioHeader->additionalData );
                    audioHeader->additionalData = NULL;
                }
            }

            // Nothing else has nested allocs so just delete the frameData if it exists
            if( frameData->frameData != NULL )
            {
                av_free( frameData->frameData );
                frameData->frameData = NULL;
            }

            if( frameData->additionalData != NULL )
            {
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

static int adpic_get_buffer(ByteIOContext *s, unsigned char *buf, int size)
{
    int TotalDataRead = 0;
    int DataReadThisTime = 0;
    int RetryBoundry = 200;
    //clock_t TimeOut = clock () + (4.0 * CLOCKS_PER_SEC);
    int retrys = 0;

    //get data while ther is no time out and we still need data
    while(TotalDataRead < size && retrys<RetryBoundry)
    {
		// This shouldn't happen, but sometimes does for some reason, needs investigation
		if (s->buf_ptr >= s->buf_end)
			break;
		
        DataReadThisTime += get_buffer(s, buf, (size-TotalDataRead));

        //if we retreave some data keep trying untill we get the required data or we have mutch longer time out 
        if(DataReadThisTime>0 && RetryBoundry<1000)
        {
            RetryBoundry += 10;   
        }

        TotalDataRead += DataReadThisTime;
        retrys++;
    }

    return TotalDataRead;
}


AVInputFormat adpic_demuxer = {
    .name           = "adpic",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings video format"), 
	.priv_data_size = sizeof(AdpicContext),
    .read_probe     = adpic_probe,
    .read_header    = adpic_read_header,
    .read_packet    = adpic_read_packet,
    .read_close     = adpic_read_close,
};
