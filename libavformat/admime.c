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

#include "avformat.h"
#include "adpic.h"


#define TEMP_BUFFER_SIZE        1024
#define MAX_IMAGE_SIZE          (256 * 1024)

// This value is only used internally within the library DATA_PLAINTEXT blocks
// should not be exposed to the client
#define DATA_PLAINTEXT          (MAX_DATA_TYPE + 1)


static int parse_mime_header( ByteIOContext *pb,
                              int *dataType, int *size, long *extra );
static int process_line( char *line, int* line_count, int *dataType,
                         int *size, long *extra );
static int parse_mp4_text_data( unsigned char *mp4TextData, int bufferSize,
                                NetVuImageData *vidDat, char **txtDat );
static int process_mp4data_line(char *line, int line_count,
                                NetVuImageData *vidDat, struct tm *time,
                                char ** txtDat );
static int is_valid_separator( unsigned char * buf, int bufLen );
static int ad_read_mpeg(AVFormatContext *s, ByteIOContext *pb,
                        AVPacket *pkt, int size, long *extra,
                        NetVuImageData *vidDat, char **txtDat);
static int ad_read_audio(AVFormatContext *s, ByteIOContext *pb,
                         AVPacket *pkt, int size, long extra,
                         NetVuAudioData *audDat);
static int handleInvalidMime(AVFormatContext *s, ByteIOContext *pb,
                             AVPacket *pkt, int *data_type,
                             int *size, int *imgLoaded);


static const char *     BOUNDARY_PREFIX1 = "--0plm(";
static const char *     BOUNDARY_PREFIX2 = "Í-0plm(";
static const char *     BOUNDARY_PREFIX3 = "ÍÍ0plm(";
static const char *     BOUNDARY_PREFIX4 = "ÍÍÍplm(";
static const char *     BOUNDARY_PREFIX5 = "ÍÍÍÍlm(";
static const char *     BOUNDARY_PREFIX6 = "ÍÍÍÍÍm(";
static const char *     BOUNDARY_PREFIX7 = "/r--0plm(";
static const char *     BOUNDARY_SUFFIX  = ":Server-Push:Boundary-String)1qaz";

static const char *     MIME_TYPE_JPEG   = "image/jpeg";
static const char *     MIME_TYPE_MP4    = "image/admp4";
static const char *     MIME_TYPE_TEXT   = "text/plain";
static const char *     MIME_TYPE_XML    = "text/xml";
static const char *     MIME_TYPE_ADPCM  = "audio/adpcm";
static const char *     MIME_TYPE_LAYOUT = "data/layout";

/*******************************************************************************
 * Function: admime_probe
 * Desc: used to identify the stream as an ad stream
 * Params:
 * Return:
 *  AVPROBE_SCORE_MAX if this straem is identifide as a ad stream 0 if not
 ******************************************************************************/
static int admime_probe(AVProbeData *p)
{
    int offset = 0;

    if (p->buf_size <= sizeof(BOUNDARY_PREFIX1))
        return 0;

    // This is nasty but it's got to go here as we don't want to try and deal
    // with fixes for certain server nuances in the HTTP layer.
    // DS2 servers seem to end their HTTP header section with the byte sequence,
    // 0x0d, 0x0a, 0x0d, 0x0a, 0x0a
    // Eco9 server ends its HTTP headers section with the sequence,
    // 0x0d, 0x0a, 0x0d, 0x0a, 0x0d, 0x0a
    // Both of which are incorrect. We'll try and detect these cases here and
    // make adjustments to the buffers so that a standard validation routine
    // can be called...

    if( p->buf[0] == 0x0a )                             // DS2 detection
        offset = 1;
    else if( p->buf[0] == 0x0d &&  p->buf[1] == 0x0a )  // Eco 9 detection
        offset = 2;

    // Now check whether we have the start of a MIME boundary separator
    if( is_valid_separator( &p->buf[offset], p->buf_size - offset ) > 0 )
        return AVPROBE_SCORE_MAX;

    return 0;
}

/*******************************************************************************
 * Function: is_valid_separator
 * Desc: Validates a multipart MIME boundary separator against the convention
 *       used by NetVu video servers
 * Params:
 *   buf - Buffer containing the boundary separator
 *   bufLen - Size of the buffer
 * Return:
 *  1 if boundary separator is valid, 0 if not
 ******************************************************************************/
static int is_valid_separator( unsigned char * buf, int bufLen )
{
    if( buf == NULL )
        return FALSE;

    if( bufLen < strlen(BOUNDARY_PREFIX1) + strlen(BOUNDARY_SUFFIX) )
        return FALSE;

    if( (strncmp(buf, BOUNDARY_PREFIX1, strlen(BOUNDARY_PREFIX1)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX2, strlen(BOUNDARY_PREFIX2)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX3, strlen(BOUNDARY_PREFIX3)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX4, strlen(BOUNDARY_PREFIX4)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX5, strlen(BOUNDARY_PREFIX5)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX6, strlen(BOUNDARY_PREFIX6)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX7, strlen(BOUNDARY_PREFIX7)) == 0)    ) {
        unsigned char *     b = &buf[strlen(BOUNDARY_PREFIX1)];

        // Now we have a server type string. We must skip past this
        while( !isspace(*b) && *b != ':' && (b - buf) < bufLen ) {
            b++;
        }

        if( *b == ':' ) {
            if( (b - buf) + strlen(BOUNDARY_SUFFIX)  <= bufLen ) {
                if( strncmp(b, BOUNDARY_SUFFIX, strlen(BOUNDARY_SUFFIX)) == 0 )
                    return TRUE;
            }
        }
    }

    return FALSE;
}

static int admime_read_close(AVFormatContext *s)
{
    return 0;
}

static int admime_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return ad_read_header(s, ap, NULL);
}

static int parse_mime_header(ByteIOContext *pb,
                             int *dataType, int *size, long *extra)
{
    unsigned char               buffer[TEMP_BUFFER_SIZE];
    unsigned char *             q = NULL;
    int                         ch, err, lineCount = 0;

    q = buffer;
    // Try and parse the header
    for(;;) {
        ch = url_fgetc( pb );

        if (ch < 0) {
            return 1;
        }

        if (ch == '\n') {
            // process line
            if (q > buffer && q[-1] == '\r')
                q--;
            *q = '\0';

            err = process_line( buffer, &lineCount, dataType, size, extra );
            // First line contains a \n
            if( !(err == 0 && lineCount == 0) ) {
                if( err < 0 )
                    return err;

                if( err == 0 )
                    return 0;
                lineCount++;
            }

            q = buffer;
        }
        else {
            if ((q - buffer) < sizeof(buffer) - 1)
                *q++ = ch;
        }
    }

    return 1;
}

static int process_line(char *line, int *line_count, int *dataType,
                        int *size, long *extra )
{
    char        *tag, *p = NULL;
    int         http_code = 0;

    // end of header
    if (line[0] == '\0')
        return 0;

    p = line;

    // The boundry string is missing sometimes so check for the HTTP header
    // and skip to line 1
    if(*line_count == 0 && 'H' == *(p) && 'T' == *(p + 1) && 'T' == *(p + 2) && 'P' == *(p + 3))
        *line_count = 1;

    // The first valid line will be the boundary string - validate this here
    if( *line_count == 0 ) {
        if( is_valid_separator( p, strlen(p) ) == FALSE )
            return -1;
    }
    else if( *line_count == 1 ) { // Second line will contain the HTTP status code
        while (!isspace(*p) && *p != '\0')
            p++;
        while (isspace(*p))
            p++;
        http_code = strtol(p, NULL, 10);
    }
    else {
        // Any other line we are just looking for particular headers
        // if we find them, we fill in the appropriate output data
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
            *size = strtol(p, NULL, 10);

            if( size == 0 )
                return -1;
        }

        if (!strcmp(tag, "Content-type")) {
            // Work out what type we actually have
            if( strcasecmp(p, MIME_TYPE_JPEG ) == 0 )
                *dataType = DATA_JFIF;
            // Or if it starts image/mp4 - this covers all the supported mp4
            // variations (i and p frames)
            else if(strncasecmp(p, MIME_TYPE_MP4, strlen(MIME_TYPE_MP4) ) == 0) {
                // P for now - as they are both processed the same subsequently
                *dataType = DATA_MPEG4P;
            }
            else if( strcasecmp(p, MIME_TYPE_TEXT ) == 0 )
                *dataType = DATA_PLAINTEXT;
            else if( strcasecmp(p, MIME_TYPE_LAYOUT ) == 0 )
                *dataType = DATA_LAYOUT;
            else if( strcasecmp(p, MIME_TYPE_XML ) == 0 )
                *dataType = DATA_XML_INFO;
            else if(strncasecmp(p, MIME_TYPE_ADPCM, strlen(MIME_TYPE_ADPCM)) == 0) {
                *dataType = DATA_AUDIO_ADPCM;

                // If we find audio in a mime header, we need to extract the
                // mode out. The header takes the form,
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

                *extra = strtol(tag, NULL, 10);

                // Map the rate to a RTP payload value for consistancy -
                // the other audio headers contain mode values in this format
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

static int parse_mp4_text_data( unsigned char *mp4TextData, int bufferSize,
                                NetVuImageData *vidDat, char **txtDat )
{
    unsigned char               buffer[TEMP_BUFFER_SIZE];
    int                         ch, err;
    unsigned char *             q = NULL;
    int                         lineCount = 0;
    unsigned char *             currentChar = mp4TextData;
    struct tm                   time;

    memset( &time, 0, sizeof(struct tm) );

    // Try and parse the header
    q = buffer;
    for(;;) {
        ch = *currentChar++;

        if (ch < 0)
            return 1;

        if (ch == '\n') {
            // process line
            if (q > buffer && q[-1] == '\r')
                q--;
            *q = '\0';

            err = process_mp4data_line(buffer, lineCount, vidDat, &time, txtDat);

            if( err < 0 )
                return err;

            if( err == 0 )
                return 0;

            // Check we're not at the end of the buffer. If the following
            // statement is true and we haven't encountered an error then we've
            // finished parsing the buffer
            if( err == 1 ) {
                // Not particularly happy with this code but it seems there's
                // little consistency in the way these buffers end. This block
                // catches all of those variations. The variations that indicate
                // the end of a MP4 MIME text block are:
                //
                // 1. The amount of buffer parsed successfully is equal to the
                //    total buffer size
                //
                // 2. The buffer ends with two NULL characters
                //
                // 3. The buffer ends with the sequence \r\n\0

                if( currentChar - mp4TextData == bufferSize )
                    return 0;

                // CS - I *think* lines should end either when we've processed
                // all the buffer OR it's padded with 0s
                // Is detection of a NULL character here sufficient?
                if( *currentChar == '\0' )
                    return 0;
            }

            lineCount++;
            q = buffer;
        }
        else {
            if ((q - buffer) < sizeof(buffer) - 1)
                *q++ = ch;
        }
    }

    return 1;
}

static int process_mp4data_line( char *line, int line_count,
                                 NetVuImageData *vidDat, struct tm *time,
                                 char ** txtDat )
{
    char        *tag = NULL, *p = NULL;
    int         lineLen = 0;

    // end of header
    if (line[0] == '\0')
        return 0;

    p = line;


    while (*p != '\0' && *p != ':')
        p++;
    if (*p != ':')
        return 1;

    tag = line;
    p++;

    while( *p != '\0' && *p == ' '  )  // While the current char is a space
        p++;

    if (*p == '\0')
        return 1;
    else {
        char * temp = p;

        // Get the length of the rest of the line
        while( *temp != '\0' )
            temp++;

        lineLen = temp - line;
    }

    if( !memcmp( tag, "Number", strlen( "Number" ) ) )
        vidDat->cam = strtol(p, NULL, 10);
    else if( !memcmp( tag, "Name", strlen( "Name" ) ) )
        memcpy( vidDat->title, p, FFMIN( TITLE_LENGTH, strlen(p) ) );
    else if( !memcmp( tag, "Version", strlen( "Version" ) ) )
        vidDat->version = strtol(p, NULL, 10);
    else if( !memcmp( tag, "Date", strlen( "Date" ) ) ) {
        sscanf( p, "%d/%d/%d", &time->tm_mday, &time->tm_mon, &time->tm_year );
#ifdef _WIN32
        time->tm_year -= 1900; // Windows uses 1900, not 1970
#endif
        time->tm_mon--;

        if( time->tm_sec != 0 || time->tm_min != 0 || time->tm_hour != 0 )
            vidDat->session_time = mktime( time );
    }
    else if( !memcmp( tag, "Time", strlen( "Time" ) ) ) {
        sscanf( p, "%d:%d:%d", &time->tm_hour, &time->tm_min, &time->tm_sec );

        if( time->tm_year != 0 )
            vidDat->session_time = mktime( time );
    }
    else if( !memcmp( tag, "MSec", strlen( "MSec" ) ) )
        vidDat->milliseconds = strtol(p, NULL, 10);
    else if( !memcmp( tag, "Locale", strlen( "Locale" ) ) )
        memcpy( vidDat->locale, p, FFMIN( MAX_NAME_LEN, strlen(p) ) );
    else if( !memcmp( tag, "UTCoffset", strlen( "UTCoffset" ) ) )
        vidDat->utc_offset = strtol(p, NULL, 10);
    else {
        // Any lines that aren't part of the pic struct,
        // tag onto the additional text block
        if( txtDat != NULL && lineLen > 0 ) {
#define LINE_END_LEN        3
            int             strLen  = 0;
            const char      lineEnd[LINE_END_LEN] = { '\r', '\n', '\0' };

            // Get the length of the existing text block if it exists
            if( *txtDat != NULL )
                strLen = strlen( *txtDat );

            // Ok, now allocate some space to hold the new string
            *txtDat = av_realloc(*txtDat, strLen + lineLen + LINE_END_LEN);

            // Copy the line into the text block
            memcpy( &(*txtDat)[strLen], line, lineLen );

            // Add a NULL terminator
            memcpy( &(*txtDat)[strLen + lineLen], lineEnd, LINE_END_LEN );
        }
    }

    return 1;
}

static int admime_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *         pb = s->pb;
    NetVuAudioData *        audDat = NULL;
    NetVuImageData *        vidDat = NULL;
    char *                  txtDat = NULL;
    int                     data_type = MAX_DATA_TYPE;
    int                     size = -1;
    long                    extra = 0;
    int                     errorVal = ADPIC_UNKNOWN_ERROR;
    FrameType               frameType = FrameTypeUnknown;
    int                     imgLoaded = FALSE;

    if(parse_mime_header( pb, &data_type, &size, &extra ) != 0 )  {
        errorVal = handleInvalidMime(s, pb, pkt, &data_type, &size, &imgLoaded);
        if (errorVal < 0)  {
            if( vidDat != NULL )
                av_free(vidDat);
            return errorVal;
        }
    }

    // Prepare for video or audio read
    errorVal = initADData(data_type, &frameType, &vidDat, &audDat);
    if (errorVal < 0)
        return errorVal;

    // Proceed based on the type of data in this frame
    switch(data_type) {
        case DATA_JPEG:
            errorVal = ad_read_jpeg(s, pb, pkt, vidDat, &txtDat);
            break;
        case DATA_JFIF:
            errorVal = ad_read_jfif(s, pb, pkt, imgLoaded, size, vidDat, &txtDat);
            break;
        case DATA_MPEG4I:
        case DATA_MPEG4P:
        case DATA_H264I:
        case DATA_H264P:
            errorVal = ad_read_mpeg(s, pb, pkt, size, &extra, vidDat, &txtDat);
            break;
        case DATA_AUDIO_ADPCM:
            errorVal = ad_read_audio(s, pb, pkt, size, extra, audDat);
            break;
        case DATA_INFO:
        case DATA_XML_INFO:
            errorVal = ad_read_info(s, pb, pkt, size);
            break;
        case DATA_LAYOUT:
            errorVal = ad_read_layout(s, pb, pkt, size);
            break;
        default: {
            av_log(s, AV_LOG_WARNING, "admime_read_packet: No handler for "
                   "data_type=%d\n", data_type);
            errorVal = ADPIC_DEFAULT_ERROR;
        }
        break;
    }

    if (errorVal >= 0)  {
        if (frameType == NetVuAudio)
            errorVal = ad_read_packet(s, pb, pkt, frameType, audDat, txtDat);
        else
            errorVal = ad_read_packet(s, pb, pkt, frameType, vidDat, txtDat);
    }
    else  {
        // If there was an error, release any memory that has been allocated
        if( vidDat != NULL )
            av_free( vidDat );

        if( audDat != NULL )
            av_free( audDat );

        if( txtDat != NULL )
            av_free( txtDat );
    }

    return errorVal;
}

static int ad_read_mpeg(AVFormatContext *s, ByteIOContext *pb,
                        AVPacket *pkt, int size, long *extra,
                        NetVuImageData *vidDat, char **txtDat)
{
    int errorVal = 0;
    int mimeBlockType = 0;

    // Fields are set manually from MIME data with these types so need
    // to set everything to zero initially in case some values aren't
    // available
    memset(vidDat, 0, sizeof(NetVuImageData));

    // Allocate a new packet to hold the frame's image data
    if( ad_new_packet(pkt, size) < 0 ) {
        errorVal = ADPIC_MPEG4_MIME_NEW_PACKET_ERROR;
        return errorVal;
    }

    // Now read the frame data into the packet
    if( ad_get_buffer( pb, pkt->data, size ) != size ) {
        errorVal = ADPIC_MPEG4_MIME_GET_BUFFER_ERROR;
        return errorVal;
    }

    // Now we should have a text block following this which contains the
    // frame data that we can place in a _image_data struct
    if(  parse_mime_header( pb, &mimeBlockType, &size, extra ) != 0 ) {
        errorVal = ADPIC_MPEG4_MIME_PARSE_HEADER_ERROR;
        return errorVal;
    }

    // Validate the data type and then extract the text buffer
    if( mimeBlockType == DATA_PLAINTEXT ) {
        unsigned char *         textBuffer = av_malloc( size );

        if( textBuffer != NULL ) {
            if( ad_get_buffer( pb, textBuffer, size ) == size ) {
                // Now parse the text buffer and populate the
                // _image_data struct
                if( parse_mp4_text_data(textBuffer, size, vidDat, txtDat ) != 0) {
                    errorVal = ADPIC_MPEG4_MIME_PARSE_TEXT_DATA_ERROR;
                    av_free( textBuffer );
                    return errorVal;
                }
                vidDat->vid_format = PIC_MODE_MPEG4_411;
            }
            else {
                av_free( textBuffer );
                errorVal = ADPIC_MPEG4_MIME_GET_TEXT_BUFFER_ERROR;
                return errorVal;
            }

            av_free( textBuffer );
        }
        else {
            errorVal = ADPIC_MPEG4_MIME_ALOCATE_TEXT_BUFFER_ERROR;
            return errorVal;
        }
    }
    return errorVal;
}

static int ad_read_audio(AVFormatContext *s, ByteIOContext *pb,
                         AVPacket *pkt, int size, long extra,
                         NetVuAudioData *audDat)
{
    // No presentation information is sent with audio frames in a mime
    // stream so there's not a lot we can do here other than ensure the
    // struct contains the size of the audio data
    audDat->sizeOfAudioData = size;
    audDat->mode = extra;
    audDat->seconds = 0;
    audDat->msecs = 0;
    audDat->channel = 0;
    audDat->sizeOfAdditionalData = 0;
    audDat->additionalData = NULL;

    if (ad_new_packet(pkt, size) < 0)
        return(ADPIC_AUDIO_ADPCM_MIME_NEW_PACKET_ERROR);

    // Now get the actual audio data
    if (ad_get_buffer( pb, pkt->data, size) != size)
        return(ADPIC_AUDIO_ADPCM_MIME_GET_BUFFER_ERROR);

    return 0;
}

/**
 * Invalid mime header.  However sometimes the header is missing and then there
 * is a valid image
 */
static int handleInvalidMime(AVFormatContext *s, ByteIOContext *pb,
                             AVPacket *pkt, int *data_type,
                             int *size, int *imgLoaded)
{
    int errorVal = 0;

    int chkByte1 = url_fgetc(pb);
    int chkByte2 = url_fgetc(pb);
    if( (chkByte1 == 0xff) && (chkByte2 == 0xD8)) {
        int status, BuffSize;
        int found = FALSE;
        int read = 2;

        uint8_t *imageData = av_malloc(MAX_IMAGE_SIZE);
        imageData[0] = chkByte1;
        imageData[1] = chkByte2;

        //Set the data type
        *data_type = DATA_JFIF;

        // Read more data till we find end of image marker
        for (; !found && !url_feof(pb) && !url_ferror(pb); read++) {
            if (read >= MAX_IMAGE_SIZE)  {
                errorVal = ADPIC_PARSE_MIME_HEADER_ERROR;
                av_free(imageData);
                return errorVal;
            }

            chkByte1 = chkByte2;
            chkByte2 = url_fgetc( pb );
            if (chkByte2 < 0)
                break;
            imageData[read] = chkByte2;
            if(chkByte1 == 0xFF && chkByte2 == 0xD9)
                found = TRUE;
        }

        BuffSize = read;
        if ((status = ad_new_packet(pkt, BuffSize)) < 0) {
            av_log(s, AV_LOG_ERROR, "ADPIC: DATA_JFIF ad_new_packet %d failed, "
                   "status %d\n", BuffSize, status);
            errorVal = ADPIC_NEW_PACKET_ERROR;
            return errorVal;
        }

        memcpy(pkt->data, imageData, BuffSize);
        av_free(imageData);
        *size = BuffSize;
        *imgLoaded = TRUE;
    }
    else {
        errorVal = ADPIC_PARSE_MIME_HEADER_ERROR;
        return errorVal;
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
