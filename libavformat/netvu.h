#include "avformat.h"
#include "httpauth.h"
#include "internal.h"


/* used for protocol handling */
#define BUFFER_SIZE 1024
#define MAX_REDIRECTS 8


typedef struct {
    const AVClass *class;
    URLContext *hd;
    unsigned char buffer[BUFFER_SIZE], *buf_ptr, *buf_end;
    int line_count;
    int http_code;
    int64_t chunksize;      /**< Used if "Transfer-Encoding: chunked" otherwise -1. */
    int64_t off, filesize;
    char location[MAX_URL_SIZE];
    HTTPAuthState auth_state;
    unsigned char headers[BUFFER_SIZE];
    int willclose;          /**< Set if the server correctly handles Connection: close and will close the connection after feeding us the content. */

    /* BMOJ - added to hold utc_offset from header */
	char* server;
    char* content;
    char* resolution;
    char* compression;
    char* rate;
    char* pps;
    char* site_id;
    char* boundry;

    int utc_offset;
} NetvuContext;
