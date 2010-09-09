#ifndef __DSPIC_H__
#define __DSPIC_H__

#include "avformat.h"
#include "ds.h"
#include "ds_exports.h"

#ifdef WORDS_BIGENDIAN
#define NTOH64(x)               (x)
#define HTON64(x)               (x)

#define NTOH32(x)               (x)
#define HTON32(x)               (x)

#define NTOH16(x)               (x)
#define HTON16(x)               (x)
#else
#define NTOH64(x)               (bswap_64(x))
#define HTON64(x)               (bswap_64(x))

#define NTOH32(x)               (bswap_32(x))
#define HTON32(x)               (bswap_32(x))

#define NTOH16(x)               (bswap_16(x))
#define HTON16(x)               (bswap_16(x))

#endif /* WORDS_BIGENDIAN */

typedef struct _dspicFormat
{
    int64_t  t;
     ByteIOContext      pb;
} DSPicFormat;

int dspicInit( void );

#endif /* __DSPIC_H__ */
