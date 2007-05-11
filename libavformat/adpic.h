// $HDR$
//$Log:  126589: adpic.h 
//
//    Rev 1.0    30/11/2006 08:12:34  pcolbran
// Decoder for AD format streams
/* ------------------------------------------------------------------------
*   Module name : adpic.h
*   Description : header file
*	Author  :
*  ------------------------------------------------------------------------
   Version Initials   Date      Comments
   ------------------------------------------------------------------------

	001		PRC		24/11/06	Initial creation 
   ------------------------------------------------------------------------
*/

#ifndef __ADPIC_H__
#define __ADPIC_H__

//#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "avformat.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

enum tx_type { TX_MIME, TX_MIME_NV, TX_STREAM, TX_MINIMAL_STREAM };
enum pkt_offsets { DATA_TYPE, DATA_CHANNEL, DATA_SIZE_BYTE_0 , DATA_SIZE_BYTE_1 , DATA_SIZE_BYTE_2 , DATA_SIZE_BYTE_3, SEPARATOR_SIZE };
enum data_type { DATA_JPEG, DATA_JFIF, DATA_MPEG4I, DATA_MPEG4P, DATA_AUDIO_ADPCM, DATA_AUDIO_RAW, DATA_MINIMAL_MPEG4, DATA_MINIMAL_AUDIO_ADPCM, DATA_LAYOUT, DATA_INFO, DATA_PLAINTEXT, MAX_DATA_TYPE  };

#ifdef WORDS_BIGENDIAN
#define network2host32(x) x = x
#define host2network32(x) x = x
#define host2be32(x) x = x
#define be2host32(x) x = x
#define host2le32(x) ((void)(x=bswap_32(x)))
#define le2host32(x) ((void)(x=bswap_32(x)))
#define network2host16(x) 
#define host2network16(x)
#define host2be16(x)
#define be2host16(x)
#define host2le16(x) ((void)(x=bswap_32(x)))
#define le2host16(x) ((void)(x=bswap_32(x)))
#else
#define network2host32(x) ((void)(x=bswap_32(x)))
#define host2network32(x) ((void)(x=bswap_32(x)))
#define host2be32(x) ((void)(x=bswap_32(x)))
#define be2host32(x) ((void)(x=bswap_32(x)))
#define host2le32(x) x = x 
#define le2host32(x) x = x
#define network2host16(x) ((void)(x=bswap_16(x)))
#define host2network16(x) ((void)(x=bswap_16(x)))
#define host2be16(x) ((void)(x=bswap_16(x)))
#define be2host16(x) ((void)(x=bswap_16(x)))
#define host2le16(x) 
#define le2host16(x) 
#endif

#define PIC_REVISION 1	// JCB 016

#define MIN_PIC_VERSION 0xDECADE10
#define MAX_PIC_VERSION (MIN_PIC_VERSION + PIC_REVISION)

//#define PIC_VERSION (MIN_PIC_VERSION + PIC_REVISION)
#define PIC_VERSION (MIN_PIC_VERSION + PIC_REVISION)

#define pic_version_valid(v) ( ( (v)>=MIN_PIC_VERSION ) && ( (v)<=MAX_PIC_VERSION ) )
#define pic_revision_level(v) ( (v) - MIN_PIC_VERSION )	// JCB 016

#define AUDIO_STREAM_ID             1

#define TITLE_LENGTH 30
#define MAX_NAME_LEN 30

typedef struct _image_data IMAGE;

/* Ensure Picture is packed on a short boundary within _image_data */
#ifdef __CWVX__
#pragma options align=packed
#endif

/* Image collection parameters (for Brooktree decoder collection setup) */
typedef struct
{
	uint16_t src_pixels;		/* Input image size (horizontal) */
	uint16_t src_lines;		/* Input image size (vertical) */
	uint16_t target_pixels;	/* Output image size (horizontal) */
	uint16_t target_lines;	/* Output image size (vertical) */
	uint16_t pixel_offset;	/* Image start offset (horizontal) */
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
    //byte[] audioData;
} AudioHeader;

typedef struct _frameData
{
    enum data_type      dataType;       /* The type of the frame we have */
    void *              typeInfo;       /* Pointer to either AudioHeader or _image_data depending on whether it's a video or audio frame */
} FrameData;

/* No more structure-packing after here */
#ifdef __CWVX__
#pragma options align=reset
#endif


// JCB 016 bit definitions for the mode element for revisions > 0
#define PIC_STRUCT_MODE_AD		1<<0	// image is AD format with all data in PIC structure
#define PIC_STRUCT_MODE_JFIF	1<<1	// image is a full JFIF

#define IMAGE_MAX	2	/* dma finished before compression */
#define IMAGE_OK	1	/* image status */
#define IMAGE_FAIL	0	/* complete failure */
#define IMAGE_BIG	-1	/* image to big */

#define PIC_MODE_JPEG_422 0
#define PIC_MODE_JPEG_411 1
#define PIC_MODE_MPEG4_411 2
#define PIC_MODE_MPEG4_411_I 3
#define PIC_MODE_MPEG4_411_GOV_P 4
#define PIC_MODE_MPEG4_411_GOV_I 5

#define start_of_image(x) ((void *)((int8_t *)&(x)[1]+(x)->start_offset))	/* PRC 007 */
#define size_of_image(x) ((x)->size+(x)->start_offset+sizeof( struct _image_data ))


/*	Define bits within status field of IMAGE structure  */
#define CAMFAIL_MASK			0x007		/* camera fail bits */
#define PIC_NEW_TEXT			0x008		/* set when image has new POS text */
#define PIC_HAS_BMP				0x010		/* set when image include a bitmap */
#define PIC_REC_MODE_MASK		0x0E0		/* recording mode field (3 bits) */
#define PIC_PRETRIG_MODE_MASK 	0x700		/* Bitmask for pre-trigger control info */
#define PIC_HAS_DBASE			0x800		/* set when image text include a dabase record */

/*	Mode values for the PIC_REC_MODE_MASK bitfield  */
#define PIC_REC_MODE0  0x00
#define PIC_REC_MODE1  0x01
#define PIC_REC_MODE2  0x02
#define PIC_REC_MODE3  0x03
#define PIC_REC_MODE4  0x04
#define PIC_REC_MODE5  0x05
#define PIC_REC_MODE6  0x06
#define PIC_REC_MODE7  0x07


#define get_pic_rec_mode(pic) ({ struct _image_data *_pic=(pic);((_pic->status & PIC_REC_MODE_MASK)>>5)&0x07;})
#define set_pic_rec_mode(pic, mode) ({ struct _image_data *_pic=(pic);_pic->status &= ~PIC_REC_MODE_MASK; _pic->status |= (((mode)&7)<<5);})

#define PIC_FIRST_PRETRIG_PIC 0x1
#define PIC_LAST_PRETRIG_PIC  0x2


#define get_pic_pretrig_mode(pic) ({ struct _image_data *_pic=(pic);((_pic->status & PIC_PRETRIG_MODE_MASK)>>8)&0x03;})
#define set_pic_pretrig_mode(pic, mode) ({ struct _image_data *_pic=(pic);_pic->status &= ~PIC_PRETRIG_MODE_MASK; _pic->status |= (((mode)&3)<<8);})


#define BMP_OFFSET 64

/*	Special values for image size that can be set in the IMAGE structure,
	to indicate conditions where image acquisition has failed.            */
#define PICSIZE_IN_PROGRESS		0			/* acquisition still in progress */
#define PICSIZE_CAM_FAIL		-1			/* camera signal failure */
#define PICSIZE_OVER_SIZE		-2			/* JPEG/MPEG oversized for image buffer */
#define PICSIZE_COMP_FAIL		-3			/* compression process failed */
#define PICSIZE_INCOMPLETE		-4			/* incomplete image data */
#define PICSIZE_RETRY			-5			/* no image available this instant, please retry  */
#define PICSIZE_CONNECTING		-6			/* not yet made connection with remote camera */
#define PICSIZE_ILLEGAL_CAM		-7			/* illegal camera number requested */
#define PICSIZE_INVALID_ADDR	-8			/* invalid address for remote camera */
#define PICSIZE_ERROR			-9			/* error in image grab task */


extern void fprint_pic(FILE *f, struct _image_data *pic);

/****************************************************************************

Prototype	  :	static inline uint8_t *start_of_bitmap(struct _image_data *pic, int32_t *size);

Procedure Desc: Inline function to find the start address of the bitmap data and
				its size

	Inputs	  :	struct _image_data *pic		the picture
				int32_t *size					size (Null if not required)
	Outputs	  :	pointer to start of bitmap or NULL if no bitmap present
	Globals   :

****************************************************************************/
static inline int8_t *start_of_bitmap(struct _image_data *pic, int32_t *size) // PRC 013
{
uint8_t *bmp=NULL;

	if (pic->status & PIC_HAS_BMP)
	{
	 	bmp = start_of_image(pic);
		bmp += (pic->size + BMP_OFFSET) & ~((BMP_OFFSET>>1)-1);	// pRC 015
		if (size)
			memcpy(size, bmp, 4);
		bmp += sizeof(int32_t);
	}
	return (int8_t *) bmp;
}

/****************************************************************************

Prototype	  :	static inline int32_t size_of_image_and_bitmap(struct _image_data *pic);

Procedure Desc: Inline function to compute the total size of an image + bitmap

	Inputs	  :	struct _image_data *pic		the picture
	Outputs	  :	Image size
	Globals   :

****************************************************************************/
static inline int32_t size_of_image_and_bitmap(struct _image_data *pic) // PRC 013
{
int8_t *bmp=NULL, *p;
int32_t bmpsize, size;

	if (pic->status & PIC_HAS_BMP)
	{
		p = (int8_t *)pic;
		bmp = start_of_bitmap(pic, &bmpsize);	// PRC 015
		size = bmpsize + (bmp-p);
	}
	else
	{
		size = size_of_image(pic);
	}
	return size;
}

#undef BMP_OFFSET

static inline  int32_t ismpeg4(int32_t vid_format)
{
	if (  (vid_format == PIC_MODE_MPEG4_411_I)
		||(vid_format == PIC_MODE_MPEG4_411)
		||(vid_format == PIC_MODE_MPEG4_411_GOV_I)
		||(vid_format == PIC_MODE_MPEG4_411_GOV_P))
		return 1;
	return 0;
}

static inline  int32_t ismpeg4iframe(int32_t vid_format)
{
	if (  (vid_format == PIC_MODE_MPEG4_411_I)
		||(vid_format == PIC_MODE_MPEG4_411_GOV_I))
		return 1;
	return 0;
}

static inline  int32_t ismpeg4pframe(int32_t vid_format)
{
	if (  (vid_format == PIC_MODE_MPEG4_411)
		||(vid_format == PIC_MODE_MPEG4_411_GOV_P))
		return 1;
	return 0;
}


extern void pic_network2host(struct _image_data *pic);
extern void pic_host2network(struct _image_data *pic);
extern void pic_le2host(struct _image_data *pic);
extern void pic_host2le(struct _image_data *pic);
extern void pic_be2host(struct _image_data *pic);
extern void pic_host2be(struct _image_data *pic);
extern void add_pic_stats(IMAGE *pic, int32_t size);

typedef struct {
    int num;
    unsigned char seq;
    /* use for reading */
    AVPacket pkt;
    int frag_offset;
    int timestamp;
    int64_t duration;
    int packet_pos;

} ADPICStream;


typedef struct {
     ADPICStream streams[128];	/* it's max number and it's not that big */
     ByteIOContext pb;
      ADPICStream* adpic; /* currently decoded stream */
} ADPICContext;

#define LOG_DEBUG           0x0010
int logger (int log_level, const char *fmt, ...);

#endif


