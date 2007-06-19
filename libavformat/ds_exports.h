#ifndef __DS_EXPORTS_H__
#define __DS_EXPORTS_H__

#include "avformat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: We need a 64 bit value for the structures that we'll use later. As 64 bit values aren't part of the ANSI standard, we'll
have to conditioanlly typedef the 64 bit value here based on the compiler/platform we're building for */
typedef long long                       long64;

#define DS1_FRAME_DATA_TYPE             0xFFFFFFFF

/* These are the data types that are supported by the DS2 video servers. */
enum data_type { DATA_JPEG, DATA_JFIF, DATA_MPEG4I, DATA_MPEG4P, DATA_AUDIO_ADPCM, DATA_AUDIO_RAW, DATA_MINIMAL_MPEG4, DATA_MINIMAL_AUDIO_ADPCM, DATA_LAYOUT, DATA_INFO, DATA_PLAINTEXT, MAX_DATA_TYPE };

#define TITLE_LENGTH 30
#define MAX_NAME_LEN 30

typedef struct _image_data IMAGE;
typedef struct _image_data ImageData;

/* Ensure Picture is packed on a short boundary within _image_data */
#ifdef __CWVX__
#pragma options align=packed
#endif

/* Image collection parameters (for Brooktree decoder collection setup) */
typedef struct
{
	uint16_t src_pixels;		/* Input image size (horizontal) */
	uint16_t src_lines;		    /* Input image size (vertical) */
	uint16_t target_pixels;	    /* Output image size (horizontal) */
	uint16_t target_lines;	    /* Output image size (vertical) */
	uint16_t pixel_offset;	    /* Image start offset (horizontal) */
	uint16_t line_offset;		/* Image start offset (vertical) */
}Picture;
struct _image_data
{
	uint32_t version;	/* structure version number */
	int32_t mode;				// in PIC_REVISION 0 this was the DFT style FULL_HI etc
							// in PIC_REVISION 1 this is used to specify AD or JFIF format image amongst other things
	int32_t cam;				/* camera number */
	int32_t vid_format;			/* 422 or 411 */
	uint32_t start_offset;	/* start of picture PRC 007 */
	int32_t size;				/* size of image */
	int32_t max_size;			/* maximum size allowed */
	int32_t target_size;		/* size wanted for compression JCB 004 */
	int32_t factor;				/* Q factor */
	uint32_t alm_bitmask_hi;		/* High 32 bits of the alarm bitmask */
	int32_t status;				/* status of last action performed on picture */
	uint32_t session_time;	/* playback time of image */		/* JCB 002 */
	uint32_t milliseconds;	/* sub-second count for playback speed control JCB 005  PRC 009 */
	char res[3+1];			/* picture size JCB 004 */
	char title[TITLE_LENGTH+1];		/* JCB 003 camera title */
	char alarm[TITLE_LENGTH+1];		/* JCB 003 alarm text - title, comment etc */
	Picture format;					/* NOTE: Do not assign to a pointer due to CW-alignment */
	char locale[MAX_NAME_LEN];	/* JCB 006 */
	int32_t utc_offset;				/* JCB 006 */
	uint32_t alm_bitmask;
};

typedef struct _audioHeader
{
    uint32_t            version;
    int32_t             mode;
    int32_t             channel;
    int32_t             sizeOfAdditionalData;
    int32_t             sizeOfAudioData;
    uint32_t            seconds;
    uint32_t            msecs;
    unsigned char *     additionalData;
} AudioHeader;

#define ID_LENGTH                       8
#define NUM_ACTIVITIES                  8
#define CAM_TITLE_LENGTH                24
#define ALARM_TEXT_LENGTH               24
typedef struct _ds1VideoFrameData
{
    char            identifier[ID_LENGTH];
    unsigned long   jpegLength;
    long64          imgSeq;
    long64          imgTime;
    unsigned char   camera;
    unsigned char   status;
    unsigned short  activity[NUM_ACTIVITIES];
    unsigned short  QFactor;
    unsigned short  height;
    unsigned short  width;
    unsigned short  resolution;
    unsigned short  interlace;
    unsigned short  subHeaderMask;
    char            camTitle[CAM_TITLE_LENGTH];
    char            alarmText[ALARM_TEXT_LENGTH];
} DS1VideoFrameData;

/* TODO: Populate this when adding audio support */
typedef struct _ds1AudioFrameData
{
    long                placeHolder;
} DS1AudioFrameData;

#define DS1_PACKET_TYPE_VIDEO           0x01
#define DS1_PACKET_TYPE_AUDIO           0x02
#define DS1_PACKET_TYPE_NUDGE           0x04

typedef struct _ds1FrameData
{
    unsigned char       type;
    DS1VideoFrameData * imageData;
    DS1AudioFrameData * audioData;
} DS1FrameData;

/* No more structure-packing after here */
#ifdef __CWVX__
#pragma options align=reset
#endif

/* This is the data structure that the ffmpeg parser fills in as part of the parsing routines. It will be shared between adpic and dspic so that our clients
   can be compatible with either stream more easily */
typedef struct _frameData
{
    long                dataType;       /* The type of the frame we have */
    void *              typeInfo;       /* Pointer to either AudioHeader or _image_data depending on whether it's a video or audio frame */
} FrameData;

int long64ToTimeValues( const long64 *timeValue, time_t * time, unsigned short *ms, unsigned short *flags );
long64 TimeTolong64( time_t time );

#define GET_FLAG_MASK_ZONE_UNKNOWN(x)       ( ((x) & 0x80) >> 7 )
#define GET_FLAG_MASK_DST(x)                ( ((x) & 0x180) >> 8 )
#define GET_FLAG_MASK_ZONE(x)               ( ((x) & 0xFC00) >> 10)

#define SET_FLAG_ZONE_UNKNOWN(flags)        ((flags) |= 0x80)
#define SET_FLAG_DST(flags, dst)            ((flags) |= (unsigned short)(dst << 8))
#define SET_FLAG_ZONE(flags, zone)          ((flags) |= (unsigned short)(zone << 10))

#ifdef __cplusplus
}
#endif


#endif /* __DS_EXPORTS_H__ */