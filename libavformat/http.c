/*
 * HTTP protocol for ffmpeg client
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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
#include <unistd.h>
#include "network.h"
#include "dsenc.h"

#ifdef __MINGW32__
#define sleep(x)                Sleep(x*1000)
#endif /* __MINGW32__ */

#include "base64.h"

/* XXX: POST protocol is not completly implemented because ffmpeg use
   only a subset of it */

//#define DEBUG

/* used for protocol handling */
#define BUFFER_SIZE 1024
#define URL_SIZE    4096
#define MAX_REDIRECTS 8

typedef struct {
    URLContext *hd;
    unsigned char buffer[BUFFER_SIZE], *buf_ptr, *buf_end;
    int line_count;
    int http_code;
    offset_t off, filesize;
    char location[URL_SIZE];

    /* CS - added to support authentication */
    int    authentication_mode;
    char * realm;
    char * nonce;
    char * algorithm;
    char * qop;

	/* BMOJ - added to hold utc_offset from header */
    char* sever;
	char* content;
	char* resolution;
	char* compression;
	char* rate;
	char* pps;
	char* site_id;
    char* boundry;
    char* Transfer_Encoding;

	int utc_offset;
    int isBinary;
    int isChunked;
    int ChunkSize;
} HTTPContext;

static int http_connect(URLContext *h, const char *path, const char *hoststr,
                        const char *auth, int *new_location, const char *tcpConnStr);
static int http_write(URLContext *h, uint8_t *buf, int size);

static void http_parse_authentication_header( char * p, HTTPContext *s );
static void http_parse_content_type_header( char * p, HTTPContext *s );
static void http_parse_transfer_encoding_header( char * p, HTTPContext *s );
static void copy_value_to_field( const char *value, char **dest );
static int http_do_request( URLContext *h, const char *path, const char *hoststr, const char *auth, int post, int *new_location );
static int http_respond_to_basic_challenge( URLContext *h, const char *auth, int post, const char *path, const char *hoststr, int *new_location );
static int http_respond_to_digest_challenge( URLContext *h, const char *auth, int post, const char *path, const char *hoststr, int *new_location );
static void value_to_string( const char *input, int size, char *output );


static void http_read_ChunkSize(HTTPContext *h);
static int LocalHTTP_read(HTTPContext *h, uint8_t *buf, int size);

typedef int (*HTTPAuthenticationResponseFunc)( URLContext *h, const char *auth, int post, const char *path, const char *hoststr );

static HTTPAuthenticationResponseFunc   http_auth_responses[2] = {
    http_respond_to_basic_challenge,
    http_respond_to_digest_challenge
};
/* Use these to index the array above */
#define AUTHENTICATION_MODE_BASIC       0
#define AUTHENTICATION_MODE_DIGEST      1

/* return non zero if error */
static int http_open_cnx(URLContext *h)
{
    const char *path, *proxy_path;
    char hostname[1024], hoststr[1024];
    char auth[1024];
    char path1[1024];
    char buf[1024];
    int port, use_proxy, location_changed = 0, redirects = 0;
    HTTPContext *s = h->priv_data;
    URLContext *hd = NULL;
    int retVal = AVERROR_IO;

    /* CS - I've omitted the following proxy resolution from WinCE builds as it doesn't support the concept of environment variables */
    /* A better solution will be available but as yet I don't know what that solution should be. Registry or config files probably... */
#ifndef CONFIG_WINCE
    proxy_path = getenv("http_proxy");
    use_proxy = (proxy_path != NULL) && !getenv("no_proxy") &&
        strstart(proxy_path, "http://", NULL);
#else
    use_proxy = 0;
#endif /* ifndef CONFIG_WINCE */

    /* fill the dest addr */
 redo:
    /* needed in any case to build the host string */
    url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port,
              path1, sizeof(path1), s->location);
    if (port > 0) {
        snprintf(hoststr, sizeof(hoststr), "%s:%d", hostname, port);
    } else {
        pstrcpy(hoststr, sizeof(hoststr), hostname);
    }

    if (use_proxy) {
        url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port,
                  NULL, 0, proxy_path);
        path = s->location;
    } else {
        if (path1[0] == '\0')
            path = "/";
        else
            path = path1;
    }
    if (port < 0)
        port = 80;

    snprintf(buf, sizeof(buf), "tcp://%s:%d", hostname, port);
    if( (retVal = url_open( &hd, buf, URL_RDWR )) < 0 )
        goto fail;

    s->hd = hd;
    if( (retVal = http_connect( h, path, hoststr, auth, &location_changed, buf )) < 0 )
        goto fail;

    if (s->http_code == 303 && location_changed == 1) {
        /* url moved, get next */
        url_close(hd);
        if (redirects++ >= MAX_REDIRECTS)
            return AVERROR_IO;
        location_changed = 0;
        goto redo;
    }
    return 0;
 fail:
    if (hd)
        url_close(hd);
    return retVal;
}

static int http_open(URLContext *h, const char *uri, int flags)
{
    HTTPContext *s;
    int ret;

    h->is_streamed = 1;

    s = av_mallocz(sizeof(HTTPContext));
    if (!s) 
    {
        return AVERROR(ENOMEM);
    }
    h->priv_data = s;
    s->filesize = -1;
    s->off = 0;
    pstrcpy (s->location, URL_SIZE, uri);

    ret = http_open_cnx(h);
    if (ret != 0)
        av_free (s);
    return ret;
}
static int http_getc(HTTPContext *s)
{
    int len;
    if (s->buf_ptr >= s->buf_end) {
        len = url_read(s->hd, s->buffer, BUFFER_SIZE);
        if (len < 0) {
            return AVERROR_IO;
        } else if (len == 0) {
            return -1;
        } else {
            s->buf_ptr = s->buffer;
            s->buf_end = s->buffer + len;
        }
    }
    return *s->buf_ptr++;
}

static int process_line(URLContext *h, char *line, int line_count,
                        int *new_location)
{
    //int i =0;
    HTTPContext *s = h->priv_data;
    char *tag, *p;

    /* end of header */
    if (line[0] == '\0')
        return 0;

    p = line;
    if (line_count == 0) 
    {
        while (!isspace(*p) && *p != '\0')
            p++;
        while (isspace(*p))
            p++;
        s->http_code = strtol(p, NULL, 10);

        //#ifdef DEBUG
        //    printf("http_code=%d\n", s->http_code);
        //#endif
    } 
    else 
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

        if (!strcmp(tag, "Location")) 
        {
            strcpy(s->location, p);
            *new_location = 1;
        } 
        else if (!strcmp (tag, "Content-Length") && s->filesize == -1) 
        {
            s->filesize = atoll(p);
        } 
        else if (!strcmp (tag, "Content-Range"))
        {
            /* "bytes $from-$to/$document_size" */
            const char *slash;
            if (!strncmp (p, "bytes ", 6)) {
                p += 6;
                s->off = atoll(p);
                if ((slash = strchr(p, '/')) && strlen(slash) > 0)
                    s->filesize = atoll(slash+1);
            }
            h->is_streamed = 0; /* we _can_ in fact seek */
        }
		else if(!strcmp(tag, "Content-type"))/* Check for Content headers */
		{
            http_parse_content_type_header( p, s );
        }
        else if(!strcmp(tag, "Transfer-Encoding"))
        { 
            http_parse_transfer_encoding_header( p, s );
        }
		else if(!strcmp(tag, "Server"))
		{
			copy_value_to_field( p, &s->sever);
		}
        else if( strcmp( tag, "WWW-Authenticate" ) == 0 )/* Check for authentication headers */
        {
            char * mode = p;

            while (!isspace(*p) && *p != '\0')
                p++;

            if( isspace(*p) )
            {
                *p = '\0';
                p++;
            }

            /* Basic or digest requested? */
            if( strncmp( av_strlwr(mode), "basic", 5 ) == 0 )
            {
                s->authentication_mode = AUTHENTICATION_MODE_BASIC;
            }
            else if( strncmp( av_strlwr(mode), "digest", 6 ) == 0 )
            {
                /* If it's digest that's been requested, we need to extract the nonce and store it in the context */
                s->authentication_mode = AUTHENTICATION_MODE_DIGEST;
            }

            http_parse_authentication_header( p, s );
        }
    }
    return 1;
}

static void http_parse_content_type_header( char * p, HTTPContext *s )
{
	int finishedContentHeader = 0;
    char *  name  = NULL;
    char *  value = NULL;

    //strip the content-type from the headder
    value = p;
    while((*p != ';') && (*p != '\0'))
    {
        p++;
    }

	if(*p == '\0')
	{ finishedContentHeader = 1; }

    *p = '\0';
    p++;
    copy_value_to_field( value, &s->content );

    if( strstr(s->content, "adhbinary")!=NULL)
    {s->isBinary=1;}
    else
    {s->isBinary=0;}
    
    while( *p != '\0' && finishedContentHeader != 1)
    {
        while(isspace(*p))p++; /* Skip whitespace */
        name = p;

        /* Now we get attributes in <name>=<value> pairs */
        while (*p != '\0' && *p != '=')
            p++;

        if (*p != '=')
            return;

        *p = '\0';
        p++;

        value = p;

        while (*p != '\0' && *p != ';')
            p++;

        if (*p == ';')
        {
            *p = '\0';
            p++;
        }

        /* Strip any "s off */
        if( strlen(value) > 0 )
        {
            if( *value == '"' && *(value + strlen(value) - 1) == '"' )
            {
                *(value + strlen(value) - 1) = '\0';
                value += 1;
            }
        }

        /* Copy the attribute into the relevant field */
		if( strcmp( name, "resolution" ) == 0 )
            copy_value_to_field( value, &s->resolution);
        if( strcmp( name, "compression" ) == 0 )
            copy_value_to_field( value, &s->compression);
        if( strcmp( name, "rate" ) == 0 )
            copy_value_to_field( value, &s->rate);
        if( strcmp( name, "pps" ) == 0 )
            copy_value_to_field( value, &s->pps);
        if( strcmp( name, "utc_offset" ) == 0 )
		    s->utc_offset = atoi(value);
		if( strcmp( name, "site_id" ) == 0 )
            copy_value_to_field( value, &s->site_id);
        if( strcmp( name, "boundary" ) == 0 )
            copy_value_to_field( value, &s->boundry);
    }
}


static void http_parse_transfer_encoding_header( char * p, HTTPContext *s )
{
    char *  value = NULL;

    //strip the content-type from the headder
    value = p;
    while(*p != '\0')
    { p++; }
    *p = '\0';

    p++;
    
    copy_value_to_field( value, &s->Transfer_Encoding); 
    if( strstr(s->Transfer_Encoding, "chunked")!=NULL)
    {
        s->isChunked=1;
    }
    else
    {
        s->isChunked=0;
    }
}

static void http_parse_authentication_header( char * p, HTTPContext *s )
{
    char *  name = NULL;
    char *  value = NULL;

    while( *p != '\0' )
    {
        while(isspace(*p))p++; /* Skip whitespace */
        name = p;

        /* Now we get attributes in <name>=<value> pairs */
        while (*p != '\0' && *p != '=')
            p++;

        if (*p != '=')
            return;

        *p = '\0';
        p++;

        value = p;

        while (*p != '\0' && *p != ',')
            p++;

        if (*p == ',')
        {
            *p = '\0';
            p++;
        }

        /* Strip any "s off */
        if( strlen(value) > 0 )
        {
            if( *value == '"' && *(value + strlen(value) - 1) == '"' )
            {
                *(value + strlen(value) - 1) = '\0';
                value += 1;
            }
        }

        /* Copy the attribute into the relevant field */
        if( strcmp( name, "realm" ) == 0 )
            copy_value_to_field( value, &s->realm );
        else if( strcmp( name, "nonce" ) == 0 )
            copy_value_to_field( value, &s->nonce );
        else if( strcmp( name, "qop" ) == 0 )
            copy_value_to_field( value, &s->qop );
        else if( strcmp( name, "algorithm" ) == 0 )
            copy_value_to_field( value, &s->algorithm );
    }
}

static void copy_value_to_field( const char *value, char **dest )
{
    if( *dest != NULL )
        av_free( *dest );

    *dest = av_malloc( strlen(value) + 1 );
    strcpy( *dest, value );
    (*dest)[strlen(value)] = '\0';
}

static int http_connect(URLContext *h, const char *path, const char *hoststr,
                        const char *auth, int *new_location, const char *tcpConnStr )
{
    HTTPContext *s = h->priv_data;
    int post;

    /* send http header */
    post = h->flags & URL_WRONLY;

    /* Try to make a simple get request */
    snprintf(s->buffer, sizeof(s->buffer),
             "%s %s HTTP/1.0\r\n"//"%s %s HTTP/1.1\r\n"
             "User-Agent: %s\r\n"
             "Accept: */*\r\n"
             "Range: bytes=%"PRId64"-\r\n"
             "Host: %s\r\n"
             "\r\n",
             post ? "POST" : "GET",
             path,
             LIBAVFORMAT_IDENT,
             s->off,
             hoststr);

    if( http_do_request( h, path, hoststr, auth, post, new_location ) < 0 )
        return AVERROR_IO;

    /* If we got a 401 authentication challenge back, we need to respond to that challenge accordingly if we have the credentials */
    if( s->http_code == 401 )
    {
        if( s->authentication_mode == AUTHENTICATION_MODE_BASIC || s->authentication_mode == AUTHENTICATION_MODE_DIGEST )
        {
            if( strcmp(auth, "" ) == 0 )
                return ADFFMPEG_ERROR_AUTH_REQUIRED; /* We can't proceed without any supplied credentials */
            else
            {
                int     err;

                /* Close the underlying TCP connection and reopen it for the second request */
                url_close(s->hd);
                s->hd = NULL;
                err = url_open( &s->hd, tcpConnStr, URL_RDWR );

                if (err < 0)
                {
                    if( s->hd != NULL )
                        url_close(s->hd);

                    return AVERROR_IO;
                }

                return http_auth_responses[s->authentication_mode]( h, auth, post, path, hoststr );
            }
        }
        else
        {
            return AVERROR_IO;
        }
    }

    return 0;
}

static int http_respond_to_digest_challenge( URLContext *h, const char *auth, int post, const char *path, const char *hoststr, int *new_location )
{
    HTTPContext *       s = h->priv_data;
    char                A1Hash[33];
    char                A2Hash[33];
    char                requestDigest[33];
    char                buffer[2048];
    const char *        p = NULL;
    int                 i = 0;
    int                 cnonce = 0xdecade11;
    unsigned int        nc = 1;
    char                cnonceStr[9];
    char                ncStr[9];
    char *              userName = NULL;
    int                 retVal = 0;

    /* Validate that the server supports the QOP=auth method (which is all we support) */
    av_strlwr( s->qop );
    if( strstr( s->qop, "auth" ) == NULL )
        return AVERROR_IO;

    /* Prepare a request using digest authentication */
    p = auth;

    /* Copy the username into the buffer */
    while( *p != ':' )
        buffer[i++] = *p++;

    /* Take a temporary copy of the username */
    if( i > 0 )
    {
        if( (userName = av_mallocz( i + 1 )) != NULL ) /* i will be length of the username string here */
        {
            memcpy( userName, buffer, i );
        }
    }

    /* Now copy the realm */
    buffer[i++] = ':';
    p++;

    strcpy( &buffer[i], s->realm );
    i += (int)strlen(s->realm);

    buffer[i++] = ':';

    /* Now the password */
    while( *p != '\0' )
        buffer[i++] = *p++;

    buffer[i] = '\0';

    /* MD5 this */
    GetFingerPrint( A1Hash, buffer, i, NULL );
    A1Hash[32] = '\0';

    /* Now create the A2 hash */
    i = 0;
    strcpy( &buffer[i], post ? "POST" : "GET" );
    i+= post ? (int)strlen("POST") : (int)strlen("GET");

    buffer[i++] = ':';

    strcpy( &buffer[i], path );
    i += (int)strlen(path);

    buffer[i] = '\0';

    GetFingerPrint( A2Hash, buffer, i, NULL );
    A2Hash[32] = '\0';

    /* CS - New QOP=auth implementation */
    /* Now concatenate the 2 hashes and md5 them */
    i = 0;
    strncpy( &buffer[i], A1Hash, 32 );
    i += 32;

    buffer[i++] = ':';

    strcpy( &buffer[i], s->nonce );
    i += (int)strlen(s->nonce);

    buffer[i++] = ':';

    /* Add the nc counter in hex here */
#ifndef WORDS_BIGENDIAN
    /* We need the integer in big endian before we do the conversion to hex... */
    nc = bswap_32(nc);
#endif
    value_to_string( (char *)&nc, 4, ncStr );
    ncStr[8] = '\0';
    strncpy( &buffer[i], ncStr, 8 );
    i += 8;
    buffer[i++] = ':';

    /* Add the cnonce in hex here */
    value_to_string( (char *)&cnonce, 4, cnonceStr );
    cnonceStr[8] = '\0';
    strncpy( &buffer[i], cnonceStr, 8 );
    i += 8;
    buffer[i++] = ':';

    /* Add the QOP method in here */
    strncpy( &buffer[i], "auth", 4 );
    i += 4;
    buffer[i++] = ':';

    strncpy( &buffer[i], A2Hash, 32 );
    i += 32;

    buffer[i] = '\0';

    GetFingerPrint( requestDigest, buffer, i, NULL );
    requestDigest[32] = '\0';

    snprintf(s->buffer, sizeof(s->buffer),
        "%s %s HTTP/1.0\r\n"
        "User-Agent: %s\r\n"
        "Accept: */*\r\n"
        "Range: bytes=%"PRId64"-\r\n"
        "Host: %s\r\n"
        "Authorization: Digest username=\"%s\",realm=\"%s\",nonce=\"%s\",uri=\"%s\",cnonce=\"%s\",nc=%s,algorithm=%s,response=\"%s\",qop=\"%s\"\r\n"
        "\r\n",
        post ? "POST" : "GET",
        path,
        LIBAVFORMAT_IDENT,
        s->off,
        hoststr,
        userName,
        s->realm,
        s->nonce,
        path,
        cnonceStr,
        ncStr,
        s->algorithm,
        requestDigest,
        "auth");

    if( http_do_request( h, path, hoststr, auth, post, new_location ) < 0 )
        retVal = AVERROR_IO;

    if( userName != NULL )
    {
        av_free( userName );
    }

    return retVal;
}

static void value_to_string( const char *input, int size, char *output )
{
    int j = 0;
    char hexval[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    for( j = 0; j < size; j++ )
    {
        output[j*2] = hexval[((input[j] >> 4) & 0xF)];
        output[(j*2) + 1] = hexval[(input[j]) & 0x0F];
    }
}

static int http_respond_to_basic_challenge( URLContext *h, const char *auth, int post, const char *path, const char *hoststr, int *new_location )
{
    HTTPContext *       s = h->priv_data;
    char *              auth_b64 = NULL;

    auth_b64 = av_base64_encode((uint8_t *)auth, strlen(auth));

    snprintf(s->buffer, sizeof(s->buffer),
             "%s %s HTTP/1.0\r\n"//"%s %s HTTP/1.1\r\n"
             "User-Agent: %s\r\n"
             "Accept: */*\r\n"
             "Range: bytes=%"PRId64"-\r\n"
             "Host: %s\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             post ? "POST" : "GET",
             path,
             LIBAVFORMAT_IDENT,
             s->off,
             hoststr,
             auth_b64);

    av_freep(&auth_b64);

    if( http_do_request( h, path, hoststr, auth, post, new_location ) < 0 )
        return AVERROR_IO;

    return 0;
}

static int http_do_request( URLContext *h, const char *path, const char *hoststr, const char *auth, int post, int *new_location )
{
    HTTPContext *s = h->priv_data;
    char line[1024], *q;
    int err, ch;
    offset_t off = s->off;

    if (http_write(h, s->buffer, (int)strlen(s->buffer)) < 0)
        return AVERROR_IO;

    /* init input buffer */
    s->buf_ptr = s->buffer;
    s->buf_end = s->buffer;
    s->line_count = 0;
    s->location[0] = '\0';
    s->off = 0;
    if (post) {
        sleep(1);
        return 0;
    }

    /* wait for header */
    q = line;
    for(;;) {
        ch = http_getc(s);
        if (ch < 0)
            return AVERROR_IO;
        if (ch == '\n') {
            /* process line */
            if (q > line && q[-1] == '\r')
                q--;
            *q = '\0';

            //#ifdef DEBUG
            //    printf("header='%s'\n", line);
            //#endif
            err = process_line(h, line, s->line_count, new_location);
            if (err < 0)
                return err;
            if (err == 0)
                return 0;
			h->utc_offset = s->utc_offset;
            h->isBinary = s->isBinary;
            s->line_count++;
            q = line;
        } else {
            if ((q - line) < sizeof(line) - 1)
                *q++ = ch;
        }
    }
    return (off == s->off) ? 0 : -1;
}




//static int http_read(URLContext *h, uint8_t *buf, int size)
//{
//    HTTPContext *s = h->priv_data;
//    int len;
//
//    /* read bytes from input buffer first */
//    len = (int)(s->buf_end - s->buf_ptr);
//    if (len > 0) {
//        if (len > size)
//            len = size;
//        memcpy(buf, s->buf_ptr, len);
//        s->buf_ptr += len;
//    } else {
//        len = url_read(s->hd, buf, size);
//    }
//    return len;
//}


static int http_read(URLContext *h, uint8_t *buf, int size)
{
    //uint8_t *mbuf = buf;
    HTTPContext *HTTP = h->priv_data;
    int DataInBufferLength = 0;
    //int readsize;
    //int i = 0,j=0;
    //int totalread =0;
    //int len;
    static int started = 0;

    if(HTTP->isChunked==0)
    {
        DataInBufferLength = LocalHTTP_read(HTTP, buf, size);
    }
    else
    {
        /* read bytes from input buffer first */
        DataInBufferLength = (int)(HTTP->buf_end - HTTP->buf_ptr);
        //printf("<<HTTP>>buffer sixe = %d\n",DataInBufferLength);
        if (DataInBufferLength > 0 && DataInBufferLength!=5 /*started == 1*/) 
        {
            if (DataInBufferLength > size)
                DataInBufferLength = size;

            memcpy(buf, HTTP->buf_ptr, DataInBufferLength);
            HTTP->buf_ptr += DataInBufferLength;
        } 
        else 
        {
            started=1;
            
            http_read_ChunkSize(HTTP);
            DataInBufferLength = url_read(HTTP->hd, buf, HTTP->ChunkSize);
    
            if (DataInBufferLength > 0)
            { HTTP->off += DataInBufferLength; }
        }
    }
    return DataInBufferLength;
}

static int ParseHexToInt(uint8_t *buf, int size)
{
    uint8_t* mBuf = buf; 
    int result = 0;
    int i;

    if(*mBuf == '\r')
    {
        size--;
        mBuf++;
    }

    if(*mBuf == '\n')
    {
        size--;
        mBuf++;
    }

    for(i = 0; i<size; i++)
    {
        if(*mBuf<48)
        {
            i = size; 
        }
        else
        {
            result *= 16;
            if (*mBuf>=97 && *mBuf<=122)// A to Z
            {
                result+= (*mBuf - 97) + 10;
            }
            else if ( *mBuf >= 65 && *mBuf<=90)//a to z
            {
                result+=  (*mBuf - 65) + 10;
            }
            else if(*mBuf >= 48 && *mBuf<=57)//0 to 9
            {
                result += (*mBuf - 48);
            }
        }
        mBuf++;
    }

    return result;
}

static void http_read_ChunkSize(HTTPContext* HTTP)
{
    uint8_t buffer[20];
    uint8_t thisChar = ' ';
    uint8_t lastChar = ' ';
    int i,j;
    int len = 0;

    //printf("<<HTTP>>http_read_ChunkSize\n");
    for(i=0; (i<sizeof(buffer) && (lastChar != '\r' && thisChar != '\n' || i<3) ); i++)
    {
        lastChar = thisChar;
        len = LocalHTTP_read(HTTP,&thisChar,1);
        
        buffer[i] = thisChar;
    }

    //for(j=0; j<i; j++)
    //{
	//    printf("<<HTTP>>%d,'%c'\n",j,buffer[j]);
    //}
   
    HTTP->ChunkSize =  ParseHexToInt(buffer, i-2);
    //if(HTTP->ChunkSize<0)
    //{
	//    printf("<<HTTP>> trap");
	//}
    //printf("<<HTTP>> Chunk Size = %d\n", HTTP->ChunkSize);    
}

static int LocalHTTP_read(HTTPContext *HTTP, uint8_t *buf, int size)
{
    int DataInBufferLength = (int)(HTTP->buf_end - HTTP->buf_ptr);

    if (DataInBufferLength > 0) 
    {
        if (DataInBufferLength > size)
        { DataInBufferLength = size; }

        memcpy(buf, HTTP->buf_ptr, DataInBufferLength);
        HTTP->buf_ptr += DataInBufferLength;
    } 
    else 
    {
        DataInBufferLength = url_read(HTTP->hd, buf, size);
    }
    if (DataInBufferLength > 0)
        HTTP->off += DataInBufferLength;
    return DataInBufferLength;
}

/* used only when posting data */
static int http_write(URLContext *h, uint8_t *buf, int size)
{
    HTTPContext *s = h->priv_data;
    return url_write(s->hd, buf, size);
}

static int http_close(URLContext *h)
{
    HTTPContext *s = h->priv_data;
    url_close(s->hd);

    /* Make sure we release any memory that may have been allocated to store authentication info */
    if( s->realm )
        av_free( s->realm );

    if( s->nonce )
        av_free( s->nonce );

    if( s->qop )
        av_free( s->qop );

    if( s->algorithm )
        av_free( s->algorithm );

    if( s->sever )
		av_free( s->sever );

    if( s->content )
        av_free( s->content );

    if( s->resolution )
        av_free( s->resolution );

    if( s->compression )
        av_free( s->compression );

    if( s->rate )
        av_free( s->rate );

    if( s->pps )
        av_free( s->pps );

    if( s->site_id )
        av_free( s->site_id );

    if( s->boundry )
        av_free( s->boundry );

    if( s->Transfer_Encoding )
        av_free( s->Transfer_Encoding );

    av_free(s);
    return 0;
}

static offset_t http_seek(URLContext *h, offset_t off, int whence)
{
    HTTPContext *s = h->priv_data;
    URLContext *old_hd = s->hd;
    offset_t old_off = s->off;

    if (whence == AVSEEK_SIZE)
        return s->filesize;
    else if ((s->filesize == -1 && whence == SEEK_END) || h->is_streamed)
        return -1;

    /* we save the old context in case the seek fails */
    s->hd = NULL;
    if (whence == SEEK_CUR)
        off += s->off;
    else if (whence == SEEK_END)
        off += s->filesize;
    s->off = off;

    /* if it fails, continue on old connection */
    if (http_open_cnx(h) < 0) {
        s->hd = old_hd;
        s->off = old_off;
        return -1;
    }
    url_close(old_hd);
    return off;
}

URLProtocol http_protocol = {
    "http",
    http_open,
    http_read,
    http_write,
    http_seek,
    http_close,
};

URLProtocol netvuProtocol = {
    "netvu",
    http_open,
    http_read,
    http_write,
    NULL, /* seek */
    http_close,
};
