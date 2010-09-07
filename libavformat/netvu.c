/*
 * HTTP protocol for ffmpeg client
 * Copyright (c) 2000, 2001 Fabrice Bellard
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

#include "libavutil/avstring.h"
#include "avformat.h"
#include <unistd.h>
#include <strings.h>
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "httpauth.h"

/* XXX: POST protocol is not completely implemented because ffmpeg uses
   only a subset of it. */

/* used for protocol handling */
#define BUFFER_SIZE 1024
#define URL_SIZE    4096
#define MAX_REDIRECTS 8

typedef struct {
    URLContext *hd;
    unsigned char buffer[BUFFER_SIZE], *buf_ptr, *buf_end;
    int line_count;
    int http_code;
    int64_t chunksize;      /**< Used if "Transfer-Encoding: chunked" otherwise -1. */
    int64_t off, filesize;
    char location[URL_SIZE];
    HTTPAuthState auth_state;

    /* BMOJ - added to hold utc_offset from header */
	char* sever;
    char* content;
    char* resolution;
    char* compression;
    char* rate;
    char* pps;
    char* site_id;
    char* boundry;

    int utc_offset;
    int isBinary;
} HTTPContext;

static int http_connect(URLContext *h, const char *path, const char *hoststr,
                        const char *auth, int *new_location);
static int http_write(URLContext *h, uint8_t *buf, int size);

static void http_parse_content_type_header( char * p, HTTPContext *s );
static void copy_value_to_field( const char *value, char **dest );

/* return non zero if error */
static int http_open_cnx(URLContext *h)
{
    const char *path, *proxy_path;
    char hostname[1024], hoststr[1024];
    char auth[1024];
    char path1[1024];
    char buf[1024];
    int port, use_proxy, err, location_changed = 0, redirects = 0;
    HTTPAuthType cur_auth_type;
    HTTPContext *s = h->priv_data;
    URLContext *hd = NULL;

    /* CS - I've omitted the following proxy resolution from WinCE builds as it doesn't support the concept of environment variables */
    /* A better solution will be available but as yet I don't know what that solution should be. Registry or config files probably... */
#ifndef CONFIG_WINCE
    proxy_path = getenv("http_proxy");
    use_proxy = (proxy_path != NULL) && !getenv("no_proxy") &&
        av_strstart(proxy_path, "http://", NULL);
#else
    use_proxy = 0;
#endif /* ifndef CONFIG_WINCE */

    /* fill the dest addr */
 redo:
    /* needed in any case to build the host string */
    ff_url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port,
                 path1, sizeof(path1), s->location);
    ff_url_join(hoststr, sizeof(hoststr), NULL, NULL, hostname, port, NULL);

    if (use_proxy) {
        ff_url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port,
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

    ff_url_join(buf, sizeof(buf), "tcp", NULL, hostname, port, NULL);
    err = url_open(&hd, buf, URL_RDWR);
    if (err < 0)
        goto fail;

    s->hd = hd;
    cur_auth_type = s->auth_state.auth_type;
    if (http_connect(h, path, hoststr, auth, &location_changed) < 0)
        goto fail;
    if (s->http_code == 401) {
        if (cur_auth_type == HTTP_AUTH_NONE && s->auth_state.auth_type != HTTP_AUTH_NONE) {
            url_close(hd);
            goto redo;
        } else
            goto fail;
    }
    if ((s->http_code == 302 || s->http_code == 303) && location_changed == 1) {
        /* url moved, get next */
        url_close(hd);
        if (redirects++ >= MAX_REDIRECTS)
            return AVERROR(EIO);
        location_changed = 0;
        goto redo;
    }
    return 0;
 fail:
    if (hd)
        url_close(hd);
    return AVERROR(EIO);
}

static int http_open(URLContext *h, const char *uri, int flags)
{
    HTTPContext *s;
    int ret;

    h->is_streamed = 1;

    s = av_mallocz(sizeof(HTTPContext));
    if (!s) {
        return AVERROR(ENOMEM);
    }
    h->priv_data = s;
    s->filesize = -1;
    s->chunksize = -1;
    s->off = 0;
    memset(&s->auth_state, 0, sizeof(s->auth_state));
    av_strlcpy(s->location, uri, URL_SIZE);

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
            return AVERROR(EIO);
        } else if (len == 0) {
            return -1;
        } else {
            s->buf_ptr = s->buffer;
            s->buf_end = s->buffer + len;
        }
    }
    return *s->buf_ptr++;
}

static int http_get_line(HTTPContext *s, char *line, int line_size)
{
    int ch;
    char *q;

    q = line;
    for(;;) {
        ch = http_getc(s);
        if (ch < 0)
            return AVERROR(EIO);
        if (ch == '\n') {
            /* process line */
            if (q > line && q[-1] == '\r')
                q--;
            *q = '\0';

            return 0;
        } else {
            if ((q - line) < line_size - 1)
                *q++ = ch;
        }
    }
}

static int process_line(URLContext *h, char *line, int line_count,
                        int *new_location)
{
    HTTPContext *s = h->priv_data;
    char *tag, *p;

    /* end of header */
    if (line[0] == '\0')
        return 0;

    p = line;
    if (line_count == 0) {
        while (!isspace(*p) && *p != '\0')
            p++;
        while (isspace(*p))
            p++;
        s->http_code = strtol(p, NULL, 10);

        dprintf(NULL, "http_code=%d\n", s->http_code);

        /* error codes are 4xx and 5xx, but regard 401 as a success, so we
         * don't abort until all headers have been parsed. */
        if (s->http_code >= 400 && s->http_code < 600 && s->http_code != 401)
            return -1;
    } else {
        while (*p != '\0' && *p != ':')
            p++;
        if (*p != ':')
            return 1;

        *p = '\0';
        tag = line;
        p++;
        while (isspace(*p))
            p++;
        if (!strcmp(tag, "Location")) {
            strcpy(s->location, p);
            *new_location = 1;
        } else if (!strcmp (tag, "Content-Length") && s->filesize == -1) {
            s->filesize = atoll(p);
        } else if (!strcmp (tag, "Content-Range")) {
            /* "bytes $from-$to/$document_size" */
            const char *slash;
            if (!strncmp (p, "bytes ", 6)) {
                p += 6;
                s->off = atoll(p);
                if ((slash = strchr(p, '/')) && strlen(slash) > 0)
                    s->filesize = atoll(slash+1);
            }
            h->is_streamed = 0; /* we _can_ in fact seek */
        } else if (!strcmp (tag, "Transfer-Encoding") && !strncasecmp(p, "chunked", 7)) {
            s->filesize = -1;
            s->chunksize = 0;
        } else if (!strcmp (tag, "WWW-Authenticate")) {
            ff_http_auth_handle_header(&s->auth_state, tag, p);
        } else if (!strcmp (tag, "Authentication-Info")) {
            ff_http_auth_handle_header(&s->auth_state, tag, p);
        } else if (!strcmp (tag, "Content-type")) {
            http_parse_content_type_header( p, s );
        } else if(!strcmp(tag, "Server")) {
			copy_value_to_field( p, &s->sever);
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

static void copy_value_to_field( const char *value, char **dest )
{
    if( *dest != NULL )
        av_free( *dest );

    *dest = av_malloc( strlen(value) + 1 );
    strcpy( *dest, value );
    (*dest)[strlen(value)] = '\0';
}

static int http_connect(URLContext *h, const char *path, const char *hoststr,
                        const char *auth, int *new_location)
{
    HTTPContext *s = h->priv_data;
    int post, err;
    char line[1024];
    char *authstr = NULL;
    int64_t off = s->off;


    /* send http header */
    post = h->flags & URL_WRONLY;
    authstr = ff_http_auth_create_response(&s->auth_state, auth, path,
                                        post ? "POST" : "GET");
    snprintf(s->buffer, sizeof(s->buffer),
             "%s %s HTTP/1.1\r\n"
             "User-Agent: %s\r\n"
             "Accept: */*\r\n"
             "Range: bytes=%"PRId64"-\r\n"
             "Host: %s\r\n"
             "%s"
             "Connection: close\r\n"
             "%s"
             "\r\n",
             post ? "POST" : "GET",
             path,
             LIBAVFORMAT_IDENT,
             s->off,
             hoststr,
             authstr ? authstr : "",
             post ? "Transfer-Encoding: chunked\r\n" : "");

    av_freep(&authstr);
    if (http_write(h, s->buffer, strlen(s->buffer)) < 0)
        return AVERROR(EIO);

    /* init input buffer */
    s->buf_ptr = s->buffer;
    s->buf_end = s->buffer;
    s->line_count = 0;
    s->off = 0;
    s->filesize = -1;
    if (post) {
        /* always use chunked encoding for upload data */
        s->chunksize = 0;
        return 0;
    }

    /* wait for header */
    for(;;) {
        if (http_get_line(s, line, sizeof(line)) < 0)
            return AVERROR(EIO);

        dprintf(NULL, "header='%s'\n", line);

        err = process_line(h, line, s->line_count, new_location);
        if (err < 0)
            return err;
        if (err == 0)
            break;
        h->utc_offset = s->utc_offset;
        h->isBinary = s->isBinary;
        s->line_count++;
    }

    return (off == s->off) ? 0 : -1;
}


static int http_read(URLContext *h, uint8_t *buf, int size)
{
    HTTPContext *s = h->priv_data;
    int len;

    if (s->chunksize >= 0) {
        if (!s->chunksize) {
            char line[32];

            for(;;) {
                do {
                    if (http_get_line(s, line, sizeof(line)) < 0)
                        return AVERROR(EIO);
                } while (!*line);    /* skip CR LF from last chunk */

                s->chunksize = strtoll(line, NULL, 16);

                dprintf(NULL, "Chunked encoding data size: %"PRId64"'\n", s->chunksize);

                if (!s->chunksize)
                    return 0;
                break;
            }
        }
        size = FFMIN(size, s->chunksize);
    }
    /* read bytes from input buffer first */
    len = s->buf_end - s->buf_ptr;
    if (len > 0) {
        if (len > size)
            len = size;
        memcpy(buf, s->buf_ptr, len);
        s->buf_ptr += len;
    } else {
        len = url_read(s->hd, buf, size);
    }
    if (len > 0) {
        s->off += len;
        if (s->chunksize > 0)
            s->chunksize -= len;
    }
    return len;
}

/* used only when posting data */
static int http_write(URLContext *h, uint8_t *buf, int size)
{
    char temp[11];  /* 32-bit hex + CRLF + nul */
    int ret;
    char crlf[] = "\r\n";
    HTTPContext *s = h->priv_data;

    if (s->chunksize == -1) {
        /* headers are sent without any special encoding */
        return url_write(s->hd, buf, size);
    }

    /* silently ignore zero-size data since chunk encoding that would
     * signal EOF */
    if (size > 0) {
        /* upload data using chunked encoding */
        snprintf(temp, sizeof(temp), "%x\r\n", size);

        if ((ret = url_write(s->hd, temp, strlen(temp))) < 0 ||
            (ret = url_write(s->hd, buf, size)) < 0 ||
            (ret = url_write(s->hd, crlf, sizeof(crlf) - 1)) < 0)
            return ret;
    }
    return size;
}

static int http_close(URLContext *h)
{
    int ret = 0;
    char footer[] = "0\r\n\r\n";
    HTTPContext *s = h->priv_data;

    /* signal end of chunked encoding if used */
    if ((h->flags & URL_WRONLY) && s->chunksize != -1) {
        ret = url_write(s->hd, footer, sizeof(footer) - 1);
        ret = ret > 0 ? 0 : ret;
    }

    url_close(s->hd);
	
    if( s->sever )
		av_free( s->sever );

    if( s->content )
        av_free( s->content );

    /* Make sure we release any memory that may have been allocated to store authentication info */
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

    av_free(s);
    return ret;
}

static int64_t http_seek(URLContext *h, int64_t off, int whence)
{
    HTTPContext *s = h->priv_data;
    URLContext *old_hd = s->hd;
    int64_t old_off = s->off;
    uint8_t old_buf[BUFFER_SIZE];
    int old_buf_size;

    if (whence == AVSEEK_SIZE)
        return s->filesize;
    else if ((s->filesize == -1 && whence == SEEK_END) || h->is_streamed)
        return -1;

    /* we save the old context in case the seek fails */
    old_buf_size = s->buf_end - s->buf_ptr;
    memcpy(old_buf, s->buf_ptr, old_buf_size);
    s->hd = NULL;
    if (whence == SEEK_CUR)
        off += s->off;
    else if (whence == SEEK_END)
        off += s->filesize;
    s->off = off;

    /* if it fails, continue on old connection */
    if (http_open_cnx(h) < 0) {
        memcpy(s->buffer, old_buf, old_buf_size);
        s->buf_ptr = s->buffer;
        s->buf_end = s->buffer + old_buf_size;
        s->hd = old_hd;
        s->off = old_off;
        return -1;
    }
    url_close(old_hd);
    return off;
}

static int
http_get_file_handle(URLContext *h)
{
    HTTPContext *s = h->priv_data;
    return url_get_file_handle(s->hd);
}

URLProtocol http_protocol = {
    "http",
    http_open,
    http_read,
    http_write,
    http_seek,
    http_close,
    .url_get_file_handle = http_get_file_handle,
};

URLProtocol netvu_protocol = {
    "netvu",
    http_open,
    http_read,
    http_write,
    NULL, /* seek */
    http_close,
};
