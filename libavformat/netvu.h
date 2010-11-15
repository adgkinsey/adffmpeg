#include "avformat.h"
#include "httpauth.h"


/* used for protocol handling */
#define BUFFER_SIZE 1024
#define URL_SIZE    4096
#define MAX_REDIRECTS 8

enum NetvuHeaders { NETVU_SERVER = 0, 
                    NETVU_CONTENT, 
                    NETVU_RESOLUTION, 
                    NETVU_COMPRESSION, 
                    NETVU_RATE, 
                    NETVU_PPS, 
                    NETVU_SITE_ID, 
                    NETVU_BOUNDARY, 
                    NETVU_MAX_HEADERS
                    };

typedef struct {
    const AVClass *class;
    URLContext *hd;
    unsigned char buffer[BUFFER_SIZE], *buf_ptr, *buf_end;
    int line_count;
    int http_code;
    int64_t chunksize;      /**< Used if "Transfer-Encoding: chunked" otherwise -1. */
    int64_t off, filesize;
    char location[URL_SIZE];
    HTTPAuthState auth_state;

    char* hdrs[NETVU_MAX_HEADERS];
    const char* hdrNames[NETVU_MAX_HEADERS];
    int utc_offset;
} NetvuContext;
