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


int ad_read_header(AVFormatContext *s, AVFormatParameters *ap, int *utcOffset);
void adpic_network2host(NetVuImageData *pic);
AVStream * ad_get_stream(struct AVFormatContext *s, NetVuImageData *pic);
AVStream * ad_get_audio_stream(struct AVFormatContext *s, NetVuAudioData* audioHeader);
AVStream * ad_get_data_stream(struct AVFormatContext *s);
int adpic_new_packet(AVPacket *pkt, int size);
void adpic_release_packet( AVPacket *pkt );
int adpic_get_buffer(ByteIOContext *s, unsigned char *buf, int size);


#define PIC_REVISION 1	// JCB 016

#define MIN_PIC_VERSION 0xDECADE10
#define MAX_PIC_VERSION (MIN_PIC_VERSION + PIC_REVISION)

//#define PIC_VERSION (MIN_PIC_VERSION + PIC_REVISION)
#define PIC_VERSION (MIN_PIC_VERSION + PIC_REVISION)

#define pic_version_valid(v) ( ( (v)>=MIN_PIC_VERSION ) && ( (v)<=MAX_PIC_VERSION ) )

#define AUDIO_STREAM_ID             1
#define DATA_STREAM_ID              2


#define PIC_MODE_JPEG_422        0
#define PIC_MODE_JPEG_411        1
#define PIC_MODE_MPEG4_411       2
#define PIC_MODE_MPEG4_411_I     3
#define PIC_MODE_MPEG4_411_GOV_P 4
#define PIC_MODE_MPEG4_411_GOV_I 5
#define PIC_MODE_H264I           6
#define PIC_MODE_H264P           7
#define PIC_MODE_H264J           8

#define AUD_MODE_AUD_RAW		   1 // historical - reserved RTP payload type
#define AUD_MODE_AUD_ADPCM		   2 // historical - reserved RTP payload type
#define AUD_MODE_AUD_ADPCM_8000	   5 
#define AUD_MODE_AUD_ADPCM_16000   6 
#define AUD_MODE_AUD_L16_44100	  11 
#define AUD_MODE_AUD_ADPCM_11025  16 
#define AUD_MODE_AUD_ADPCM_22050  17 
#define AUD_MODE_AUD_ADPCM_32000  96 
#define AUD_MODE_AUD_ADPCM_44100  97 
#define AUD_MODE_AUD_ADPCM_48000  98 
#define AUD_MODE_AUD_L16_8000	 100 
#define AUD_MODE_AUD_L16_11025	 101 
#define AUD_MODE_AUD_L16_16000	 102 
#define AUD_MODE_AUD_L16_22050	 103 
#define AUD_MODE_AUD_L16_32000	 104 
#define AUD_MODE_AUD_L16_48000	 105 
#define AUD_MODE_AUD_L16_12000	 106 
#define AUD_MODE_AUD_L16_24000	 107 


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
#define ADPIC_FAILED_TO_PARSE_INFOLIST              ADPIC_ERROR + -49

#endif
