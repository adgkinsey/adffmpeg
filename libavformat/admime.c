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

/**
 * @file AD-Holdings demuxer for AD stream format (binary)
 */

#include <strings.h>

#include "avformat.h"
#include "adffmpeg_errors.h"
#include "adpic.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/avstring.h"


#define TEMP_BUFFER_SIZE        1024
#define MAX_IMAGE_SIZE          (256 * 1024)

// This value is only used internally within the library DATA_PLAINTEXT blocks
// should not be exposed to the client
#define DATA_PLAINTEXT          (AD_DATATYPE_MAX + 1)


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
static const char *     MIME_TYPE_PBM    = "image/pbm";
static const char *     MIME_TYPE_H264   = "image/adh264";

static const uint8_t rawJfifHeader[] = { 0xff, 0xd8, 0xff, 0xe0,
                                         0x00, 0x10, 0x4a, 0x46,
                                         0x49, 0x46, 0x00, 0x01 };



/**
 * Validates a multipart MIME boundary separator against the convention used by
 * NetVu video servers
 *
 * \param buf Buffer containing the boundary separator
 * \param bufLen Size of the buffer
 * \return 1 if boundary separator is valid, 0 if not
 */
static int is_valid_separator( unsigned char * buf, int bufLen )
{
    if (buf == NULL )
        return FALSE;

    if (bufLen < strlen(BOUNDARY_PREFIX1) + strlen(BOUNDARY_SUFFIX) )
        return FALSE;

    if ((strncmp(buf, BOUNDARY_PREFIX1, strlen(BOUNDARY_PREFIX1)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX2, strlen(BOUNDARY_PREFIX2)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX3, strlen(BOUNDARY_PREFIX3)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX4, strlen(BOUNDARY_PREFIX4)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX5, strlen(BOUNDARY_PREFIX5)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX6, strlen(BOUNDARY_PREFIX6)) == 0) ||
        (strncmp(buf, BOUNDARY_PREFIX7, strlen(BOUNDARY_PREFIX7)) == 0)    ) {
        unsigned char *     b = &buf[strlen(BOUNDARY_PREFIX1)];

        // Now we have a server type string. We must skip past this
        while( !av_isspace(*b) && *b != ':' && (b - buf) < bufLen ) {
            b++;
        }

        if (*b == ':' ) {
            if ((b - buf) + strlen(BOUNDARY_SUFFIX)  <= bufLen ) {
                if (strncmp(b, BOUNDARY_SUFFIX, strlen(BOUNDARY_SUFFIX)) == 0 )
                    return TRUE;
            }
        }
    }

    return FALSE;
}


/**
 * Parse a line of MIME data
 */
static int process_line(char *line, int *line_count, int *dataType,
                        int *size, long *extra )
{
    char *tag, *p = NULL;
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
    if (*line_count == 0 ) {
        if (is_valid_separator( p, strlen(p) ) == FALSE )
            return -1;
    }
    else if (*line_count == 1 ) { // Second line will contain the HTTP status code
        while (!av_isspace(*p) && *p != '\0')
            p++;
        while (av_isspace(*p))
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
        while (av_isspace(*p))
            p++;

        if (!strcmp(tag, "Content-length")) {
            *size = strtol(p, NULL, 10);

            if (size == 0 )
                return -1;
        }

        if (!strcmp(tag, "Content-type")) {
            // Work out what type we actually have
            if (av_strcasecmp(p, MIME_TYPE_JPEG ) == 0 )
                *dataType = AD_DATATYPE_JFIF;
            // Or if it starts image/mp4 - this covers all the supported mp4
            // variations (i and p frames)
            else if(av_strncasecmp(p, MIME_TYPE_MP4, strlen(MIME_TYPE_MP4) ) == 0) {
                // P for now - as they are both processed the same subsequently
                *dataType = AD_DATATYPE_MPEG4P;
            }
            else if (av_strcasecmp(p, MIME_TYPE_TEXT ) == 0 )
                *dataType = DATA_PLAINTEXT;
            else if (av_strcasecmp(p, MIME_TYPE_LAYOUT ) == 0 )
                *dataType = AD_DATATYPE_LAYOUT;
            else if (av_strcasecmp(p, MIME_TYPE_XML ) == 0 )
                *dataType = AD_DATATYPE_XML_INFO;
            else if(av_strncasecmp(p, MIME_TYPE_ADPCM, strlen(MIME_TYPE_ADPCM)) == 0) {
                *dataType = AD_DATATYPE_AUDIO_ADPCM;

                // If we find audio in a mime header, we need to extract the
                // mode out. The header takes the form,
                // Content-Type: audio/adpcm;rate=<mode>
                while (*p != '\0' && *p != ';')
                    p++;

                if (*p != ';' )
                    return 1;

                p++;
                while (av_isspace(*p))
                    p++;

                // p now pointing at the rate. Look for the first '='
                while (*p != '\0' && *p != '=')
                    p++;
                if (*p != '=')
                    return 1;

                p++;

                tag = p;

                while( *p != '\0' && !av_isspace(*p) )
                    p++;

                if (*p != '\0' )
                    *p = '\0';

                *extra = strtol(tag, NULL, 10);

                // Map the rate to a RTP payload value for consistancy -
                // the other audio headers contain mode values in this format
                if (*extra == 8000 )
                    *extra = RTP_PAYLOAD_TYPE_8000HZ_ADPCM;
                else if (*extra == 11025 )
                    *extra = RTP_PAYLOAD_TYPE_11025HZ_ADPCM;
                else if (*extra == 16000 )
                    *extra = RTP_PAYLOAD_TYPE_16000HZ_ADPCM;
                else if (*extra == 22050 )
                    *extra = RTP_PAYLOAD_TYPE_22050HZ_ADPCM;
                else if (*extra == 32000 )
                    *extra = RTP_PAYLOAD_TYPE_32000HZ_ADPCM;
                else if (*extra == 44100 )
                    *extra = RTP_PAYLOAD_TYPE_44100HZ_ADPCM;
                else if (*extra == 48000 )
                    *extra = RTP_PAYLOAD_TYPE_48000HZ_ADPCM;
                else
                    *extra = RTP_PAYLOAD_TYPE_8000HZ_ADPCM; // Default
            }
            else if (av_strcasecmp(p, MIME_TYPE_PBM ) == 0 )  {
                *dataType = AD_DATATYPE_PBM;
            }
            else if(av_strncasecmp(p, MIME_TYPE_H264, strlen(MIME_TYPE_H264) ) == 0) {
                // P for now - as they are both processed the same subsequently
                *dataType = AD_DATATYPE_H264P;
            }
            else  {
                *dataType = AD_DATATYPE_MAX;
            }
        }
    }
    return 1;
}

/**
 * Read and process MIME header information
 *
 * \return Zero on successful decode, -2 if a raw JPEG, anything else indicates
 *         failure
 */
static int parse_mime_header(AVIOContext *pb, uint8_t *buffer, int *bufSize,
                             int *dataType, int *size, long *extra)
{
    unsigned char ch, *q = NULL;
    int           err, lineCount = 0;
    const int     maxBufSize = *bufSize;

    *bufSize = 0;

    // Check for JPEG header first
    do {
        if (avio_read(pb, &ch, 1) < 0)
            break;
        if (buffer && (ch == rawJfifHeader[*bufSize]))  {
            buffer[*bufSize] = ch;
            ++(*bufSize);
            if ((*bufSize) == sizeof(rawJfifHeader))
                return -2;
        }
        else
            break;
    } while( (*bufSize) < sizeof(rawJfifHeader));

    q = buffer + *bufSize;
    // Try and parse the header
    for(;;) {
        if (ch == '\n') {
            // process line
            if (q > buffer && q[-1] == '\r')
                q--;
            *q = '\0';

            err = process_line( buffer, &lineCount, dataType, size, extra );
            // First line contains a \n
            if (!(err == 0 && lineCount == 0) ) {
                if (err < 0 )
                    return err;

                if (err == 0 )
                    return 0;
                lineCount++;
            }

            q = buffer;
        }
        else {
            if ((q - buffer) < (maxBufSize - 1))
                *q++ = ch;
            else
                return ADFFMPEG_AD_ERROR_PARSE_MIME_HEADER;
        }

        err = avio_read(pb, &ch, 1);
        if (err < 0)  {
            if (pb->eof_reached)
                return err;
            else
                return ADFFMPEG_AD_ERROR_PARSE_MIME_HEADER;
        }
    }

    return ADFFMPEG_AD_ERROR_PARSE_MIME_HEADER;
}


/**
 * Parse a line of MIME data for MPEG video frames
 */
static int process_mp4data_line( char *line, int line_count,
                                 struct NetVuImageData *vidDat, struct tm *tim,
                                 char ** txtDat )
{
    static const int titleLen = sizeof(vidDat->title) / sizeof(vidDat->title[0]);
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

    if (!memcmp( tag, "Number", strlen( "Number" ) ) )
        vidDat->cam = strtol(p, NULL, 10);
    else if (!memcmp( tag, "Name", strlen( "Name" ) ) )
        memcpy( vidDat->title, p, FFMIN( titleLen, strlen(p) ) );
    else if (!memcmp( tag, "Version", strlen( "Version" ) ) )  {
        int verNum = strtod(p, NULL) * 100.0 - 1;
        vidDat->version = 0xDECADE10 + verNum;
    }
    else if (!memcmp( tag, "Date", strlen( "Date" ) ) ) {
        sscanf( p, "%d/%d/%d", &tim->tm_mday, &tim->tm_mon, &tim->tm_year );
        tim->tm_year -= 1900;
        tim->tm_mon--;

        if ((tim->tm_sec != 0) || (tim->tm_min != 0) || (tim->tm_hour != 0))
            vidDat->session_time = mktime(tim);
    }
    else if (!memcmp( tag, "Time", strlen( "Time" ) ) ) {
        sscanf( p, "%d:%d:%d", &tim->tm_hour, &tim->tm_min, &tim->tm_sec );

        if (tim->tm_year != 0)
            vidDat->session_time = mktime(tim);
    }
    else if (!memcmp( tag, "MSec", strlen( "MSec" ) ) )
        vidDat->milliseconds = strtol(p, NULL, 10);
    else if (!memcmp( tag, "Locale", strlen( "Locale" ) ) )
        memcpy( vidDat->locale, p, FFMIN( titleLen, strlen(p) ) );
    else if (!memcmp( tag, "UTCoffset", strlen( "UTCoffset" ) ) )
        vidDat->utc_offset = strtol(p, NULL, 10);
    else {
        // Any lines that aren't part of the pic struct,
        // tag onto the additional text block
        // \todo Parse out some of these and put them into metadata
        if (txtDat != NULL && lineLen > 0 ) {
#define LINE_END_LEN        3
            int             strLen  = 0;
            const char      lineEnd[LINE_END_LEN] = { '\r', '\n', '\0' };

            // Get the length of the existing text block if it exists
            if (*txtDat != NULL )
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

/**
 * Parse MIME data that is sent after each MPEG video frame
 */
static int parse_mp4_text_data( unsigned char *mp4TextData, int bufferSize,
                                struct NetVuImageData *vidDat, char **txtDat )
{
    unsigned char               buffer[TEMP_BUFFER_SIZE];
    int                         ch, err;
    unsigned char *             q = NULL;
    int                         lineCount = 0;
    unsigned char *             currentChar = mp4TextData;
    struct tm                   tim;

    memset( &tim, 0, sizeof(struct tm) );

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

            err = process_mp4data_line(buffer, lineCount, vidDat, &tim, txtDat);

            if (err < 0 )
                return err;

            if (err == 0 )
                return 0;

            // Check we're not at the end of the buffer. If the following
            // statement is true and we haven't encountered an error then we've
            // finished parsing the buffer
            if (err == 1 ) {
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

                if (currentChar - mp4TextData == bufferSize )
                    return 0;

                // CS - I *think* lines should end either when we've processed
                // all the buffer OR it's padded with 0s
                // Is detection of a NULL character here sufficient?
                if (*currentChar == '\0' )
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

/**
 * MPEG4 or H264 video frame with a MIME trailer
 */
static int admime_mpeg(AVFormatContext *s,
                       AVPacket *pkt, int size, long *extra,
                       struct NetVuImageData *vidDat, char **txtDat,
                       int adDataType)
{
    AVIOContext *pb = s->pb;
    AdContext* adContext = s->priv_data;
    int errorVal = 0;
    int mimeBlockType = 0;
    uint8_t buf[TEMP_BUFFER_SIZE];
    int bufSize = TEMP_BUFFER_SIZE;

    // Fields are set manually from MIME data with these types so need
    // to set everything to zero initially in case some values aren't
    // available
    memset(vidDat, 0, sizeof(struct NetVuImageData));

    // Allocate a new packet to hold the frame's image data
    if (ad_new_packet(pkt, size) < 0 )
        return ADFFMPEG_AD_ERROR_MPEG4_MIME_NEW_PACKET;

    // Now read the frame data into the packet
    if (avio_read( pb, pkt->data, size ) != size )
        return ADFFMPEG_AD_ERROR_MPEG4_MIME_GET_BUFFER;
    
    if (adContext->streamDatatype == 0)  {
        if (adDataType == AD_DATATYPE_H264I)
            adContext->streamDatatype = PIC_MODE_H264I;
        else if (adDataType == AD_DATATYPE_H264P)
            adContext->streamDatatype = PIC_MODE_H264P;
        else
            adContext->streamDatatype = mpegOrH264(AV_RB32(pkt->data));
    }
    vidDat->vid_format = adContext->streamDatatype;

    // Now we should have a text block following this which contains the
    // frame data that we can place in a _image_data struct
    if (parse_mime_header(pb, buf, &bufSize, &mimeBlockType, &size, extra ) != 0)
        return ADFFMPEG_AD_ERROR_MPEG4_MIME_PARSE_HEADER;

    // Validate the data type and then extract the text buffer
    if (mimeBlockType == DATA_PLAINTEXT ) {
        unsigned char *textBuffer = av_malloc( size );

        if (textBuffer != NULL ) {
            if (avio_read( pb, textBuffer, size ) == size ) {
                // Now parse the text buffer and populate the
                // _image_data struct
                if (parse_mp4_text_data(textBuffer, size, vidDat, txtDat ) != 0) {
                    av_free( textBuffer );
                    return ADFFMPEG_AD_ERROR_MPEG4_MIME_PARSE_TEXT_DATA;
                }
            }
            else {
                av_free( textBuffer );
                return ADFFMPEG_AD_ERROR_MPEG4_MIME_GET_TEXT_BUFFER;
            }

            av_free( textBuffer );
        }
        else {
            return AVERROR(ENOMEM);
        }
    }
    return errorVal;
}


/**
 * Audio frame
 */
static int ad_read_audio(AVFormatContext *s,
                         AVPacket *pkt, int size, long extra,
                         struct NetVuAudioData *audDat,
                         enum AVCodecID codec_id)
{
    AVIOContext *pb = s->pb;
    
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
        return ADFFMPEG_AD_ERROR_AUDIO_ADPCM_MIME_NEW_PACKET;

    // Now get the actual audio data
    if (avio_read( pb, pkt->data, size) != size)
        return ADFFMPEG_AD_ERROR_AUDIO_ADPCM_MIME_GET_BUFFER;

    if (codec_id == CODEC_ID_ADPCM_IMA_WAV)
        audiodata_network2host(pkt->data, pkt->data, size);

    return 0;
}


/**
 * Invalid mime header.
 *
 * However sometimes the header is missing and then there is a valid image so
 * try and parse a frame out anyway.
 */
static int handleInvalidMime(AVFormatContext *s,
                             uint8_t *preRead, int preReadSize, AVPacket *pkt,
                             int *data_type, int *size, int *imgLoaded)
{
    AVIOContext *pb = s->pb;
    int errorVal = 0;
    unsigned char chkByte;
    int status, read, found = FALSE;
    uint8_t imageData[MAX_IMAGE_SIZE];

    for(read = 0; read < preReadSize; read++)  {
        imageData[read] = preRead[read];
    }

    //Set the data type
    *data_type = AD_DATATYPE_JFIF;

    // Read more data till we find end of image marker
    for (; !found && (pb->eof_reached==0) && (pb->error==0); read++) {
        if (read >= MAX_IMAGE_SIZE)
            return ADFFMPEG_AD_ERROR_PARSE_MIME_HEADER;

        if (avio_read(pb, &chkByte, 1) < 0)
            break;
        imageData[read] = chkByte;
        if(imageData[read - 1] == 0xFF && imageData[read] == 0xD9)
            found = TRUE;
    }

    *size = read;
    if ((status = ad_new_packet(pkt, *size)) < 0) {
        av_log(s, AV_LOG_ERROR, "handleInvalidMime: ad_new_packet (size %d)"
                                " failed, status %d\n", *size, status);
        return ADFFMPEG_AD_ERROR_NEW_PACKET;
    }

    memcpy(pkt->data, imageData, *size);
    *imgLoaded = TRUE;

    return errorVal;
}


/**
 * Identify if the stream as an AD MIME stream
 */
static int admime_probe(AVProbeData *p)
{
    int offset = 0;
    int ii, matchedBytes = 0;

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

    if (p->buf[0] == 0x0a )                             // DS2 detection
        offset = 1;
    else if (p->buf[0] == 0x0d &&  p->buf[1] == 0x0a )  // Eco 9 detection
        offset = 2;

    // Now check whether we have the start of a MIME boundary separator
    if (is_valid_separator( &p->buf[offset], p->buf_size - offset ) > 0 )
        return AVPROBE_SCORE_MAX;

    // If server is only sending a single frame (i.e. fields=1) then it
    // sometimes just sends the raw JPEG with no MIME or other header, check
    // for this
    if (p->buf_size >= sizeof(rawJfifHeader))  {
        for(ii = 0; ii < sizeof(rawJfifHeader); ii++)  {
            if (p->buf[ii] == rawJfifHeader[ii])
                ++matchedBytes;
        }
        if (matchedBytes == sizeof(rawJfifHeader))
            return AVPROBE_SCORE_MAX;
    }

    return 0;
}

static int admime_read_header(AVFormatContext *s)
{
    return ad_read_header(s, NULL);
}


static int admime_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    //AdContext*              adContext = s->priv_data;
    AVIOContext *           pb = s->pb;
    void *                  payload = NULL;
    char *                  txtDat = NULL;
    int                     data_type = AD_DATATYPE_MAX;
    int                     size = -1;
    long                    extra = 0;
    int                     errorVal = ADFFMPEG_AD_ERROR_UNKNOWN;
    enum AVMediaType        mediaType = AVMEDIA_TYPE_UNKNOWN;
    enum AVCodecID          codecId   = CODEC_ID_NONE;
    int                     imgLoaded = FALSE;
    uint8_t                 buf[TEMP_BUFFER_SIZE];
    int                     bufSize = TEMP_BUFFER_SIZE;
    unsigned char *         tempbuf = NULL;

    errorVal = parse_mime_header(pb, buf, &bufSize, &data_type, &size, &extra);
    if(errorVal != 0 )  {
        if (errorVal == -2)
            errorVal = handleInvalidMime(s, buf, bufSize, pkt,
                                         &data_type, &size, &imgLoaded);

        if (errorVal < 0)  {
            return errorVal;
        }
    }

    // Prepare for video or audio read
    errorVal = initADData(data_type, &mediaType, &codecId, &payload);
    if (errorVal < 0)  {
        if (payload != NULL )
            av_free(payload);
        return errorVal;
    }

    // Proceed based on the type of data in this frame
    switch(data_type) {
        case AD_DATATYPE_JPEG:
            errorVal = ad_read_jpeg(s, pkt, payload, &txtDat);
            break;
        case AD_DATATYPE_JFIF:
            errorVal = ad_read_jfif(s, pkt, imgLoaded, size, payload, &txtDat);
            break;
        case AD_DATATYPE_MPEG4I:
        case AD_DATATYPE_MPEG4P:
        case AD_DATATYPE_H264I:
        case AD_DATATYPE_H264P:
            errorVal = admime_mpeg(s, pkt, size, &extra, payload, &txtDat, data_type);
            break;
        case AD_DATATYPE_AUDIO_ADPCM:
            errorVal = ad_read_audio(s, pkt, size, extra, payload, CODEC_ID_ADPCM_IMA_WAV);
            break;
        case AD_DATATYPE_INFO:
        case AD_DATATYPE_XML_INFO:
        case AD_DATATYPE_SVARS_INFO:
            // May want to handle INFO, XML_INFO and SVARS_INFO separately in future
            errorVal = ad_read_info(s, pkt, size);
            break;
        case AD_DATATYPE_LAYOUT:
            errorVal = ad_read_layout(s, pkt, size);
            break;
        case AD_DATATYPE_PBM:
            errorVal = ad_read_overlay(s, pkt, 1, size, &txtDat);
            break;
        case AD_DATATYPE_BMP:
        default: {
            av_log(s, AV_LOG_WARNING, "admime_read_packet: No handler for "
                   "data_type=%d\n", data_type);
            
            // Would like to use avio_skip, but that needs seek support, 
            // so just read the data into a buffer then throw it away
            tempbuf = av_malloc(size); 
            if (tempbuf)  {
                avio_read(pb, tempbuf, size);
                av_free(tempbuf);
            }
            else
                return AVERROR(ENOMEM);
                
            return ADFFMPEG_AD_ERROR_DEFAULT;
        }
        break;
    }

    if (errorVal >= 0)  {
        errorVal = ad_read_packet(s, pkt, 1, mediaType, codecId, payload, txtDat);
    }
    else  {
        av_dlog(s, "admime_read_packet: Error %d\n", errorVal);
        
#ifdef AD_SIDEDATA_IN_PRIV
        // If there was an error, release any memory that has been allocated
        if (payload != NULL)
            av_free(payload);

        if (txtDat != NULL)
            av_free( txtDat );
#endif
    }

#ifndef AD_SIDEDATA_IN_PRIV
    if (payload != NULL)
        av_freep(&payload);

    if( txtDat != NULL )
        av_freep(&txtDat);
#endif

    return errorVal;
}

static int admime_read_close(AVFormatContext *s)
{
    return 0;
}


AVInputFormat ff_admime_demuxer = {
    .name           = "admime",
    .long_name      = NULL_IF_CONFIG_SMALL("AD-Holdings video format (MIME)"),
    .priv_data_size = sizeof(AdContext),
    .read_probe     = admime_probe,
    .read_header    = admime_read_header,
    .read_packet    = admime_read_packet,
    .read_close     = admime_read_close,
    .flags          = AVFMT_TS_DISCONT | AVFMT_VARIABLE_FPS | AVFMT_NO_BYTE_SEEK,
};
