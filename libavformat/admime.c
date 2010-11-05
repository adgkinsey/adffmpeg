/*
 * AD-Holdings demuxer for AD stream format (binary)
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

#include <strings.h>

#include "avformat.h"
#include "libavutil/avstring.h"
#include "libavutil/bswap.h"

#include "adpic.h"
#include "jfif_img.h"
#include "netvu.h"


#define TEMP_BUFFER_SIZE        1024

/* These are the data types that are supported by the DS2 video servers. */
enum data_type { DATA_JPEG, DATA_JFIF, DATA_MPEG4I, DATA_MPEG4P, DATA_AUDIO_ADPCM, DATA_AUDIO_RAW, DATA_MINIMAL_MPEG4, DATA_MINIMAL_AUDIO_ADPCM, DATA_LAYOUT, DATA_INFO, DATA_H264I, DATA_H264P, DATA_XML_INFO, MAX_DATA_TYPE };
#define DATA_PLAINTEXT              (MAX_DATA_TYPE + 1)   /* This value is only used internally within the library DATA_PLAINTEXT blocks should not be exposed to the client */


static int adpic_parse_mime_header( ByteIOContext *pb, int *dataType, int *size, long *extra );
static int process_line( char *line, int* line_count, int *dataType, int *size, long *extra );
static int adpic_parse_mp4_text_data( unsigned char *mp4TextData, int bufferSize, NetVuImageData *video_data, char **additionalTextData );
static int process_mp4data_line( char *line, int line_count, NetVuImageData *video_data, struct tm *time, char ** additionalText );
static int adpic_is_valid_separator( unsigned char * buf, int bufLen );


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
static const char *     MIME_TYPE_TEXT = "text/plain";
static const char *     MIME_TYPE_ADPCM = "audio/adpcm";
static const char *     MIME_TYPE_LAYOUT = "data/layout";

/****************************************************************************************************************
 * Function: admime_probe
 * Desc: used to identify the stream as an ad stream 
 * Params: 
 * Return:
 *  AVPROBE_SCORE_MAX if this straem is identifide as a ad stream 0 if not 
 ****************************************************************************************************************/

static int admime_probe(AVProbeData *p)
{
    int server_adjustment = 0;
    
    if (p->buf_size <= sizeof(MIME_BOUNDARY_PREFIX1))
        return 0;

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
        return AVPROBE_SCORE_MAX;
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

static int admime_read_close(AVFormatContext *s)
{
    return 0;
}

static int admime_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
	ByteIOContext*	pb = s->pb;
	URLContext*		urlContext = pb->opaque;
	NetvuContext*	netvu = NULL;


	s->ctx_flags |= AVFMTCTX_NOHEADER;
	
	if (urlContext && urlContext->is_streamed)  {
		if ( av_stristart(urlContext->filename, "netvu://", NULL) == 1)
			netvu = urlContext->priv_data;
	}
	
	if (netvu)  {
		av_metadata_set2(&s->metadata, "server",		netvu->server, 		0);
		av_metadata_set2(&s->metadata, "content",		netvu->content, 	0);
		av_metadata_set2(&s->metadata, "resolution",	netvu->resolution, 	0);
		av_metadata_set2(&s->metadata, "compression",	netvu->compression, 0);
		av_metadata_set2(&s->metadata, "rate",			netvu->rate, 		0);
		av_metadata_set2(&s->metadata, "pps",			netvu->pps, 		0);
		av_metadata_set2(&s->metadata, "site_id",		netvu->site_id, 	0);
		//av_metadata_set2(&s->metadata, "boundry",		netvu->boundry, 	0);
	}
					
	return 0;
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
            if( strcasecmp(p, MIME_TYPE_JPEG ) == 0 )
            {
                *dataType = DATA_JFIF;
            }
            else if( strncasecmp(p, MIME_TYPE_MP4, strlen(MIME_TYPE_MP4) ) == 0 ) /* Or if it starts image/mp4  - this covers all the supported mp4 variations (i and p frames) */
            {
                *dataType = DATA_MPEG4P; /* P for now - as they are both processed the same subsequently, this is sufficient */
            }
            else if( strcasecmp(p, MIME_TYPE_TEXT ) == 0 )
            {
                *dataType = DATA_PLAINTEXT;
            }
            else if( strcasecmp(p, MIME_TYPE_LAYOUT ) == 0 )
            {
                *dataType = DATA_LAYOUT;
            }
            else if( strncasecmp(p, MIME_TYPE_ADPCM, strlen(MIME_TYPE_ADPCM) ) == 0 )
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

static int admime_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *         pb = s->pb;
    AVStream *              st = NULL;
    FrameData *             frameData = NULL;
    NetVuAudioData *        audio_data = NULL;
    NetVuImageData *        video_data = NULL;
    char *                  text_data = NULL;
    unsigned char           ch, ch1;
    int                     data_type = MAX_DATA_TYPE;
    int                     n, size = -1;
    int                     status;
    int                     i =0, BuffSize;
    int                     found = FALSE;
    //char                    scratch[1024];
    long                    extra = 0;
    int                     errorVal = ADPIC_UNKNOWN_ERROR;
    FrameType               currentFrameType = FrameTypeUnknown;
    unsigned char *         restore;
    char *                  ptr;
    int                     manual_size = FALSE;
    //int loop = 0;
    
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

            if (BuffSize <= 0)
            {
                errorVal = ADPIC_NEW_PACKET_ERROR;
                goto cleanup;
            }
            
            if ((status = adpic_new_packet(pkt, BuffSize))<0) // PRC 003
            {
                //logger(LOG_DEBUG,"ADPIC: DATA_JFIF adpic_new_packet %d failed, status %d\n", size, status);
                errorVal = ADPIC_NEW_PACKET_ERROR;
                goto cleanup;
            }

            pb->buf_ptr = restore;
            n = get_buffer(pb, pkt->data, BuffSize);
                
            if(!found)
            {
                //end of image not found in buffer to read further 
                for( i = 0, ch = 0, ch1 = 0, ptr = pkt->data; 
                     !(ch==0xFF && ch1==0xD9) && 
                     (pb->buf_ptr < pb->buf_end) && 
                     (i < pkt->size);
                     i++, ptr++)
                {
                    ch = ch1;
                    ch1 = *ptr;
                }
            }
            size = i;
            manual_size = TRUE;
        }
        else
        { 
            errorVal = ADPIC_PARSE_MIME_HEADER_ERROR;
            goto cleanup;
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
		    adpic_network2host(video_data);
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
			    //logger(LOG_DEBUG,"ADPIC: admime_read_packet, build_jpeg_header failed\n");
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
			    //logger(LOG_DEBUG,"ADPIC: admime_read_packet, parse_jfif_header failed\n");
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
	    break;

        // CS - Support for ADPCM frames
        case DATA_AUDIO_ADPCM:
        {
            /* No presentation information is sent with audio frames in a mime stream so there's not a lot we can do here other than
               ensure the struct contains the size of the audio data */
            audio_data->sizeOfAudioData = size;
            audio_data->mode = extra;
            audio_data->seconds = 0;
            audio_data->msecs = 0;

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
	        //logger(LOG_DEBUG,"ADPIC: admime_read_packet, No handler for data_type=%d\n", data_type );
            errorVal = ADPIC_DEFAULT_ERROR;
	        goto cleanup;
        }
        break;
		
    }

    if( currentFrameType == NetVuVideo )
    {
	    // At this point We have a legal pic structure which we use to determine 
	    // which codec stream to use
	    if ( (st = ad_get_stream( s, video_data)) == NULL )
	    {
		    //logger(LOG_DEBUG,"ADPIC: admime_read_packet, failed get_stream for video\n");
            errorVal = ADPIC_GET_STREAM_ERROR;
		    goto cleanup;
	    }
		else  {
			if (video_data->session_time > 0)  {
				pkt->pts = video_data->session_time;
				pkt->pts *= 1000ULL;
				pkt->pts += video_data->milliseconds % 1000;
			}
			else
				pkt->pts = AV_NOPTS_VALUE;
		}
    }
    else if( currentFrameType == NetVuAudio )
    {
        // Get the audio stream
        if ( (st = ad_get_audio_stream( s, audio_data )) == NULL )
        {
		    //logger(LOG_DEBUG,"ADPIC: admime_read_packet, failed get_stream for audio\n");
            errorVal = ADPIC_GET_AUDIO_STREAM_ERROR;
		    goto cleanup;
        }
		else  {
			if (audio_data->seconds > 0)  {
				pkt->pts = audio_data->seconds;
				pkt->pts *= 1000ULL;
				pkt->pts += audio_data->msecs % 1000;
			}
			else
				pkt->pts = AV_NOPTS_VALUE;
		}
    }
    else if( currentFrameType == NetVuDataInfo || currentFrameType == NetVuDataLayout )
    {
        // Get or create a data stream
        if ( (st = ad_get_data_stream( s )) == NULL )
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


AVInputFormat admime_demuxer = {
    .name           = "admime",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings video format (MIME)"), 
	.priv_data_size = 0,
    .read_probe     = admime_probe,
    .read_header    = admime_read_header,
    .read_packet    = admime_read_packet,
    .read_close     = admime_read_close,
};
