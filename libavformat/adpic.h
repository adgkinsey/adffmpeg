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
#include "ds_exports.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

enum tx_type { TX_MIME, TX_MIME_NV, TX_STREAM, TX_MINIMAL_STREAM };
enum pkt_offsets { DATA_TYPE, DATA_CHANNEL, DATA_SIZE_BYTE_0 , DATA_SIZE_BYTE_1 , DATA_SIZE_BYTE_2 , DATA_SIZE_BYTE_3, SEPARATOR_SIZE };

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
#define DATA_STREAM_ID              2


// JCB 016 bit definitions for the mode element for revisions > 0
#define PIC_STRUCT_MODE_AD		1<<0	// image is AD format with all data in PIC structure
#define PIC_STRUCT_MODE_JFIF	1<<1	// image is a full JFIF

#define IMAGE_MAX	2	/* dma finished before compression */
#define IMAGE_OK	1	/* image status */
#define IMAGE_FAIL	0	/* complete failure */
#define IMAGE_BIG	-1	/* image to big */

#define PIC_MODE_JPEG_422        0
#define PIC_MODE_JPEG_411        1
#define PIC_MODE_MPEG4_411       2
#define PIC_MODE_MPEG4_411_I     3
#define PIC_MODE_MPEG4_411_GOV_P 4
#define PIC_MODE_MPEG4_411_GOV_I 5
#define PIC_MODE_H264I           6
#define PIC_MODE_H264P           7
#define PIC_MODE_H264J           8

#define start_of_image(x) ((void *)((int8_t *)&(x)[1]+(x)->start_offset))	/* PRC 007 */
#define size_of_image(x) ((x)->size+(x)->start_offset+sizeof( NetVuImageData ))


/*	Define bits within status field of NetVuImageData structure  */
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


#define get_pic_rec_mode(pic) ({ NetVuImageData *_pic=(pic);((_pic->status & PIC_REC_MODE_MASK)>>5)&0x07;})
#define set_pic_rec_mode(pic, mode) ({ NetVuImageData *_pic=(pic);_pic->status &= ~PIC_REC_MODE_MASK; _pic->status |= (((mode)&7)<<5);})

#define PIC_FIRST_PRETRIG_PIC 0x1
#define PIC_LAST_PRETRIG_PIC  0x2


#define get_pic_pretrig_mode(pic) ({ NetVuImageData *_pic=(pic);((_pic->status & PIC_PRETRIG_MODE_MASK)>>8)&0x03;})
#define set_pic_pretrig_mode(pic, mode) ({ NetVuImageData *_pic=(pic);_pic->status &= ~PIC_PRETRIG_MODE_MASK; _pic->status |= (((mode)&3)<<8);})


#define BMP_OFFSET 64

/*	Special values for image size that can be set in the NetVuImageData structure,
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

//AD pic error codes 
#define ADPIC_NO_ERROR                              0
#define ADPIC_ERROR                                 -200            //used to offset ad pic errors so that adpic can be identifyed as the origon of the error

#define ADPIC_UNKNOWN_ERROR                         ADPIC_ERROR + -1 //AVERROR_UNKNOWN     // (-200 + -1) unknown error 
#define ADPIC_READ_6_BYTE_SEPARATOR_ERROR           ADPIC_ERROR + -2
#define ADPIC_NEW_PACKET_ERROR                      ADPIC_ERROR + -3
#define ADPIC_PARSE_MIME_HEADER_ERROR               ADPIC_ERROR + -4
#define ADPIC_NETVU_IMAGE_DATA_ERROR                ADPIC_ERROR + -5
#define ADPIC_NETVU_AUDIO_DATA_ERROR                ADPIC_ERROR + -6

#define ADPIC_JPEG_IMAGE_DATA_READ_ERROR            ADPIC_ERROR + -7
#define ADPIC_JPEG_PIC_VERSION_ERROR                ADPIC_ERROR + -8
#define ADPIC_JPEG_ALOCATE_TEXT_BLOCK_ERROR         ADPIC_ERROR + -9
#define ADPIC_JPEG_READ_TEXT_BLOCK_ERROR            ADPIC_ERROR + -10
#define ADPIC_JPEG_HEADER_ERROR                     ADPIC_ERROR + -11
#define ADPIC_JPEG_NEW_PACKET_ERROR                 ADPIC_ERROR + -12
#define ADPIC_JPEG_READ_BODY_ERROR                  ADPIC_ERROR + -13

#define ADPIC_JFIF_NEW_PACKET_ERROR                 ADPIC_ERROR + -14
#define ADPIC_JFIF_GET_BUFFER_ERROR                 ADPIC_ERROR + -15
#define ADPIC_JFIF_MANUAL_SIZE_ERROR                ADPIC_ERROR + -16

#define ADPIC_MPEG4_MIME_NEW_PACKET_ERROR           ADPIC_ERROR + -17    
#define ADPIC_MPEG4_MIME_GET_BUFFER_ERROR           ADPIC_ERROR + -18 
#define ADPIC_MPEG4_MIME_PARSE_HEADER_ERROR         ADPIC_ERROR + -19
#define ADPIC_MPEG4_MIME_PARSE_TEXT_DATA_ERROR      ADPIC_ERROR + -20
#define ADPIC_MPEG4_MIME_GET_TEXT_BUFFER_ERROR      ADPIC_ERROR + -21
#define ADPIC_MPEG4_MIME_ALOCATE_TEXT_BUFFER_ERROR  ADPIC_ERROR + -22

#define ADPIC_MPEG4_GET_BUFFER_ERROR                ADPIC_ERROR + -23
#define ADPIC_MPEG4_PIC_VERSION_VALID_ERROR         ADPIC_ERROR + -24
#define ADPIC_MPEG4_ALOCATE_TEXT_BUFFER_ERROR       ADPIC_ERROR + -25
#define ADPIC_MPEG4_GET_TEXT_BUFFER_ERROR           ADPIC_ERROR + -26
#define ADPIC_MPEG4_NEW_PACKET_ERROR                ADPIC_ERROR + -27
#define ADPIC_MPEG4_PIC_BODY_ERROR                  ADPIC_ERROR + -28

#define ADPIC_MPEG4_MINIMAL_GET_BUFFER_ERROR        ADPIC_ERROR + -29
#define ADPIC_MPEG4_MINIMAL_NEW_PACKET_ERROR        ADPIC_ERROR + -30
#define ADPIC_MPEG4_MINIMAL_NEW_PACKET_ERROR2       ADPIC_ERROR + -31

#define ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR  ADPIC_ERROR + -32
#define ADPIC_MINIMAL_AUDIO_ADPCM_NEW_PACKET_ERROR  ADPIC_ERROR + -33
#define ADPIC_MINIMAL_AUDIO_ADPCM_GET_BUFFER_ERROR2 ADPIC_ERROR + -34

#define ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR          ADPIC_ERROR + -35
#define ADPIC_AUDIO_ADPCM_ALOCATE_ADITIONAL_ERROR   ADPIC_ERROR + -36
#define ADPIC_AUDIO_ADPCM_GET_BUFFER_ERROR2         ADPIC_ERROR + -37

#define ADPIC_AUDIO_ADPCM_MIME_NEW_PACKET_ERROR     ADPIC_ERROR + -38
#define ADPIC_AUDIO_ADPCM_MIME_GET_BUFFER_ERROR     ADPIC_ERROR + -39

#define ADPIC_INFO_NEW_PACKET_ERROR                 ADPIC_ERROR + -40
#define ADPIC_INFO_GET_BUFFER_ERROR                 ADPIC_ERROR + -41

#define ADPIC_LAYOUT_NEW_PACKET_ERROR               ADPIC_ERROR + -42
#define ADPIC_LAYOUT_GET_BUFFER_ERROR               ADPIC_ERROR + -43

#define ADPIC_DEFAULT_ERROR                         ADPIC_ERROR + -44

#define ADPIC_GET_STREAM_ERROR                      ADPIC_ERROR + -45
#define ADPIC_GET_AUDIO_STREAM_ERROR                ADPIC_ERROR + -46
#define ADPIC_GET_INFO_LAYOUT_STREAM_ERROR          ADPIC_ERROR + -47

#define ADPIC_END_OF_STREAM                         ADPIC_ERROR + -48

extern void fprint_pic(FILE *f, NetVuImageData *pic);

/****************************************************************************

Prototype	  :	static inline uint8_t *start_of_bitmap(NetVuImageData *pic, int32_t *size);

Procedure Desc: Inline function to find the start address of the bitmap data and
				its size

	Inputs	  :	NetVuImageData *pic		the picture
				int32_t *size					size (Null if not required)
	Outputs	  :	pointer to start of bitmap or NULL if no bitmap present
	Globals   :

****************************************************************************/
static inline int8_t *start_of_bitmap(NetVuImageData *pic, int32_t *size) // PRC 013
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

Prototype	  :	static inline int32_t size_of_image_and_bitmap(NetVuImageData *pic);

Procedure Desc: Inline function to compute the total size of an image + bitmap

	Inputs	  :	NetVuImageData *pic		the picture
	Outputs	  :	Image size
	Globals   :

****************************************************************************/
static inline int32_t size_of_image_and_bitmap(NetVuImageData *pic) // PRC 013
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


extern void pic_network2host(NetVuImageData *pic);
extern void pic_host2network(NetVuImageData *pic);
extern void pic_le2host(NetVuImageData *pic);
extern void pic_host2le(NetVuImageData *pic);
extern void pic_be2host(NetVuImageData *pic);
extern void pic_host2be(NetVuImageData *pic);
extern void add_pic_stats(NetVuImageData *pic, int32_t size);

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


