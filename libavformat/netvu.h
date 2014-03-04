#include "avformat.h"

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

    char* hdrs[NETVU_MAX_HEADERS];
    const char* hdrNames[NETVU_MAX_HEADERS];
    int utc_offset;
} NetvuContext;
