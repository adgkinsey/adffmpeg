#ifndef __DSPIC_H__
#define __DSPIC_H__

#include "avformat.h"


typedef struct _dspicFormat
{
    int64_t  t;
    ByteIOContext      pb;
} DSPicFormat;

int dspicInit( void );

#endif /* __DSPIC_H__ */
