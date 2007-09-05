#ifndef __AD_AUDIO_H__
#define __AD_AUDIO_H__

#include "avformat.h"

#define RTP_PAYLOAD_TYPE_8000HZ_ADPCM                                   5
#define RTP_PAYLOAD_TYPE_11025HZ_ADPCM                                  16
#define RTP_PAYLOAD_TYPE_16000HZ_ADPCM                                  6
#define RTP_PAYLOAD_TYPE_22050HZ_ADPCM                                  17
#define RTP_PAYLOAD_TYPE_32000HZ_ADPCM                                  96
#define RTP_PAYLOAD_TYPE_44100HZ_ADPCM                                  97
#define RTP_PAYLOAD_TYPE_48000HZ_ADPCM                                  98
#define RTP_PAYLOAD_TYPE_8000HZ_PCM                                     100
#define RTP_PAYLOAD_TYPE_11025HZ_PCM                                    101
#define RTP_PAYLOAD_TYPE_16000HZ_PCM                                    102
#define RTP_PAYLOAD_TYPE_22050HZ_PCM                                    103
#define RTP_PAYLOAD_TYPE_32000HZ_PCM                                    104
#define RTP_PAYLOAD_TYPE_44100HZ_PCM                                    11
#define RTP_PAYLOAD_TYPE_48000HZ_PCM                                    105

#define SIZEOF_RTP_HEADER                                               12

#define AD_AUDIO_STREAM_ID                                              0x212ED83E;

typedef struct _ADAudioContext
{
    int         dummy;
} ADAudioContext;


extern int adaudio_init(void);

#endif /* __AD_AUDIO_H__ */
