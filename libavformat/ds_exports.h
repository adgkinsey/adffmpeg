#ifndef __DS_EXPORTS_H__
#define __DS_EXPORTS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>


#define TITLE_LENGTH 30
#define MAX_NAME_LEN 30

/* Ensure Picture is packed on a short boundary within _image_data */
#ifdef __CWVX__
#pragma options align=packed
#endif

/* Image collection parameters (for Brooktree decoder collection setup) */
typedef struct
{
	uint16_t src_pixels;		///< Input image size (horizontal)
	uint16_t src_lines;		    ///<  Input image size (vertical)
	uint16_t target_pixels;	    ///<  Output image size (horizontal)
	uint16_t target_lines;	    ///<  Output image size (vertical)
	uint16_t pixel_offset;	    ///<  Image start offset (horizontal)
	uint16_t line_offset;		///<  Image start offset (vertical)
}Picture;
typedef struct _imageData
{
	uint32_t version;	            ///<  structure version number */
	
	/// in PIC_REVISION 0 this was the DFT style FULL_HI etc
	/// in PIC_REVISION 1 this is used to specify AD or JFIF format image amongst other things
	int32_t mode;
	int32_t cam;				    ///< camera number
	int32_t vid_format;			    ///< 422 or 411
	uint32_t start_offset;	        ///< start of picture PRC 007
	int32_t size;				    ///< size of image
	int32_t max_size;			    ///< maximum size allowed
	int32_t target_size;		    ///< size wanted for compression JCB 004
	int32_t factor;				    ///< Q factor
	uint32_t alm_bitmask_hi;		///< High 32 bits of the alarm bitmask
	int32_t status;				    ///< status of last action performed on picture
	uint32_t session_time;	        ///< playback time of image  JCB 002
	uint32_t milliseconds;	        ///< sub-second count for playback speed control JCB 005  PRC 009
	char res[3+1];			        ///< picture size JCB 004
	char title[TITLE_LENGTH+1];		///< JCB 003 camera title
	char alarm[TITLE_LENGTH+1];		///< JCB 003 alarm text - title, comment etc
	Picture format;					///< NOTE: Do not assign to a pointer due to CW-alignment
	char locale[MAX_NAME_LEN];	    ///< JCB 006
	int32_t utc_offset;				///< JCB 006
	uint32_t alm_bitmask;
} NetVuImageData;

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
} NetVuAudioData;

#define ID_LENGTH                       8
#define NUM_ACTIVITIES                  8
#define CAM_TITLE_LENGTH                24
#define ALARM_TEXT_LENGTH               24
typedef struct _dmImageData
{
    char            identifier[ID_LENGTH];
    unsigned long   jpegLength;
    int64_t         imgSeq;
    int64_t         imgTime;
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
} DMImageData;

/* No more structure-packing after here */
#ifdef __CWVX__
#pragma options align=reset
#endif

typedef enum _frameType
{
    FrameTypeUnknown = 0,
    NetVuVideo,
    NetVuAudio,
    DMVideo,
    DMNudge,
    NetVuDataInfo,
    NetVuDataLayout,
    RTPAudio
} FrameType;

/* This is the data structure that the ffmpeg parser fills in as part of the parsing routines. It will be shared between adpic and dspic so that our clients
   can be compatible with either stream more easily */
typedef struct _framedata
{
    FrameType           frameType;      /* Type of frame we have. See FrameType enum for supported types */
    void *              frameData;      /* Pointer to structure holding the information for the frame. Type determined by the data type field */
    void *              additionalData; /* CS - Added to hold text data. Not sure I prefer it here but as a first version, it works */
} FrameData;

#define GET_FLAG_MASK_ZONE_UNKNOWN(x)       ( ((x) & 0x80) >> 7 )
#define GET_FLAG_MASK_DST(x)                ( ((x) & 0x180) >> 8 )
#define GET_FLAG_MASK_ZONE(x)               ( ((x) & 0xFC00) >> 10)

#define SET_FLAG_ZONE_UNKNOWN(flags)        ((flags) |= 0x80)
#define SET_FLAG_DST(flags, dst)            ((flags) |= (unsigned short)(dst << 8))
#define SET_FLAG_ZONE(flags, zone)          ((flags) |= (unsigned short)(zone << 10))

#define RTP_PAYLOAD_TYPE_8000HZ_ADPCM                       5
#define RTP_PAYLOAD_TYPE_11025HZ_ADPCM                      16
#define RTP_PAYLOAD_TYPE_16000HZ_ADPCM                      6
#define RTP_PAYLOAD_TYPE_22050HZ_ADPCM                      17
#define RTP_PAYLOAD_TYPE_32000HZ_ADPCM                      96
#define RTP_PAYLOAD_TYPE_44100HZ_ADPCM                      97
#define RTP_PAYLOAD_TYPE_48000HZ_ADPCM                      98
#define RTP_PAYLOAD_TYPE_8000HZ_PCM                         100
#define RTP_PAYLOAD_TYPE_11025HZ_PCM                        101
#define RTP_PAYLOAD_TYPE_16000HZ_PCM                        102
#define RTP_PAYLOAD_TYPE_22050HZ_PCM                        103
#define RTP_PAYLOAD_TYPE_32000HZ_PCM                        104
#define RTP_PAYLOAD_TYPE_44100HZ_PCM                        11
#define RTP_PAYLOAD_TYPE_48000HZ_PCM                        105

#ifdef __cplusplus
}
#endif


#endif /* __DS_EXPORTS_H__ */
