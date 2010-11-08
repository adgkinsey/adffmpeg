#include "avformat.h"
#include "httpauth.h"


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
