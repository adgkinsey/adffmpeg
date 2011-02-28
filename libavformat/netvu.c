/*
 * Netvu protocol for ffmpeg client
 * Copyright (c) 2010 AD-Holdings plc
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

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "avformat.h"
#include "internal.h"
#include "http.h"
#include "netvu.h"


static void copy_value_to_field(const char *value, char **dest)
{
    if (*dest != NULL)
        av_free(*dest);

    *dest = av_malloc(strlen(value) + 1);
    strcpy(*dest, value);
    (*dest)[strlen(value)] = '\0';
}

static void netvu_parse_content_type_header(char * p, NetvuContext *nv)
{
	int finishedContentHeader = 0;
    char *  name  = NULL;
    char *  value = NULL;

    //strip the content-type from the headder
    value = p;
    while((*p != ';') && (*p != '\0'))
        p++;

	if(*p == '\0')
        finishedContentHeader = 1;

    *p = '\0';
    p++;
    copy_value_to_field( value, &nv->hdrs[NETVU_CONTENT] );
    
    while( *p != '\0' && finishedContentHeader != 1)  {
        while(isspace(*p))
            p++; // Skip whitespace
        name = p;

        // Now we get attributes in <name>=<value> pairs
        while (*p != '\0' && *p != '=')
            p++;

        if (*p != '=')
            return;

        *p = '\0';
        p++;

        value = p;

        while (*p != '\0' && *p != ';')
            p++;

        if (*p == ';')  {
            *p = '\0';
            p++;
        }

        // Strip any "s off
        if( strlen(value) > 0 )  {
            int ii;
            
            if( *value == '"' && *(value + strlen(value) - 1) == '"' )  {
                *(value + strlen(value) - 1) = '\0';
                value += 1;
            }

			// Copy the attribute into the relevant field
            for(ii = 0; ii < NETVU_MAX_HEADERS; ii++)  {
                if( strcasecmp(name, nv->hdrNames[ii] ) == 0 )
                    copy_value_to_field(value, &nv->hdrs[ii]);
            }
            if(strcasecmp(name, "utc_offset") == 0)
				nv->utc_offset = atoi(value);
		}
    }
}

static void processLine(char *line, NetvuContext *nv)
{
    char *p = line;
    char *tag;
    
    while (*p != '\0' && *p != ':')
        p++;
    if (*p != ':')
        return;

    *p = '\0';
    tag = line;
    p++;
    while (isspace(*p))
        p++;
    
    if (!strcasecmp (tag, nv->hdrNames[NETVU_CONTENT]))
        netvu_parse_content_type_header(p, nv);
    else if(!strcasecmp(tag, nv->hdrNames[NETVU_SERVER]))
        copy_value_to_field( p, &nv->hdrs[NETVU_SERVER]);
}

static int netvu_open(URLContext *h, const char *uri, int flags)
{
    char hostname[1024], auth[1024], path[1024], http[1024];
    int port, err;
    NetvuContext *nv = h->priv_data;
    
    nv->hdrNames[NETVU_SERVER]      = "Server";
    nv->hdrNames[NETVU_CONTENT]     = "Content-type";
    nv->hdrNames[NETVU_RESOLUTION]  = "resolution";
    nv->hdrNames[NETVU_COMPRESSION] = "compression";
    nv->hdrNames[NETVU_RATE]        = "rate";
    nv->hdrNames[NETVU_PPS]         = "pps";
    nv->hdrNames[NETVU_SITE_ID]     = "site_id";
    nv->hdrNames[NETVU_BOUNDARY]    = "boundary";
    nv->utc_offset                  = 1441;
    
    h->is_streamed = 1;
    
    av_url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port,
                 path, sizeof(path), uri);
    if (port < 0)
        port = 80;
    ff_url_join(http, sizeof(http), "http", auth, hostname, port, "%s", path);
    
    err = url_open(&nv->hd, http, URL_RDWR);
    if (err >= 0)  {
        char headers[1024];
        char *startOfLine = &headers[0];
        int ii;
        
        size_t hdrSize = ff_http_get_headers(nv->hd, headers, sizeof(headers));
        if (hdrSize > 0)  {
            for (ii = 0; ii < hdrSize; ii++)  {
                if (headers[ii] == '\n')  {
                    headers[ii] = '\0';
                    processLine(startOfLine, nv);
                    startOfLine = &headers[ii+1];
                }
            }
        }
        return 0;
    }
    else  {
        if (nv->hd)
            url_close(nv->hd);
        nv->hd = NULL;
        return AVERROR(EIO);
    }
}

static int netvu_read(URLContext *h, uint8_t *buf, int size)
{
    NetvuContext *nv = h->priv_data;
    return url_read(nv->hd, buf, size);
}

static int netvu_close(URLContext *h)
{
    NetvuContext *nv = h->priv_data;
    int i, ret = 0;

    if (nv->hd)
    	ret = url_close(nv->hd);

    for (i = 0; i < NETVU_MAX_HEADERS; i++)  {
        if (nv->hdrs[i])
            av_free(nv->hdrs[i]);
    }

    return ret;
}

static int netvu_write(URLContext *h, const uint8_t *buf, int size)
{
    NetvuContext *nv = h->priv_data;
    return url_write(nv->hd, buf, size);
}

static int64_t netvu_seek(URLContext *h, int64_t off, int whence)
{
    NetvuContext *nv = h->priv_data;
    return url_seek(nv->hd, off, whence);
}


URLProtocol ff_netvu_protocol = {
    "netvu",
    netvu_open,
    netvu_read,
    netvu_write,
    netvu_seek,
    netvu_close,
    .priv_data_size = sizeof(NetvuContext),
};
