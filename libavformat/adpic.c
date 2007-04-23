// $HDR$
//$Log:  126591: adpic.c 
//
//    Rev 1.1    13/12/2006 12:53:22  pcolbran
// Added error checking to get_buffer_calls. Don't free streams in
// adpic_read_close() this is done at the av_bif level
//
//    Rev 1.0    30/11/2006 08:12:36  pcolbran
// Decoder for AD format streams
/* ------------------------------------------------------------------------
*   Module name : pic.c
*   Description : AD picture format handling
*	Author  : PR Colbran
*	(c) Copyright Dedicated Micros Ltd.
*  ------------------------------------------------------------------------
   Version Initials   Date      Comments
   ------------------------------------------------------------------------

  	001		PRC		24/11/06	Initial creation 
  	002		PRC		13/12/06	Added error checking to get_buffer_calls.
  								Don't free streams in adpic_read_close() this
  								is done at the av_bif level
  	003		PRC		25/01/07	Switched lo logger debug. Added error checking to 
  								av_new_packet calls
	---------------------------------------------------------------------------
*/

#include <stdio.h>
#include "avcodec.h"
#include "adpic.h"

#define BINARY_PUSH_MIME_STR "video/adhbinary"

enum tx_type { TX_MIME, TX_MIME_NV, TX_STREAM, TX_MINIMAL_STREAM };
enum pkt_offsets { DATA_TYPE, DATA_CHANNEL, DATA_SIZE_BYTE_0 , DATA_SIZE_BYTE_1 , DATA_SIZE_BYTE_2 , DATA_SIZE_BYTE_3, SEPARATOR_SIZE };
enum data_type { DATA_JPEG, DATA_JFIF, DATA_MPEG4I, DATA_MPEG4P, DATA_AUDIO_ADPCM, DATA_AUDIO_RAW, DATA_MINIMAL_MPEG4, DATA_MINIMAL_AUDIO_ADPCM, DATA_LAYOUT, DATA_INFO, MAX_DATA_TYPE  };

struct _minimal_video_header	// PRC 002
{
	uint32_t t;
	unsigned short ms;
};

union _minimal_video	// PRC 002
{
	struct _minimal_video_header header;
	char payload[sizeof(uint32_t)+sizeof(unsigned short)];
};

struct _minimal_audio_header	// PRC 002
{
	uint32_t t;
	unsigned short ms;
	unsigned short mode;
};


typedef struct {
    int64_t  riff_end;
    int64_t  movi_end;
    offset_t movi_list;
    int index_loaded;
} ADPICDecContext;



void pic_network2host(struct _image_data *pic)
{
	network2host32(pic->version);
	network2host32(pic->mode);
	network2host32(pic->cam);
	network2host32(pic->vid_format);
	network2host32(pic->start_offset);
	network2host32(pic->size);
	network2host32(pic->max_size);
	network2host32(pic->target_size);
	network2host32(pic->factor);
	network2host32(pic->alm_bitmask_hi);
	network2host32(pic->status);
	network2host32(pic->session_time);
	network2host32(pic->milliseconds);
	network2host32(pic->utc_offset);
	network2host32(pic->alm_bitmask);
	network2host16(pic->format.src_pixels);
	network2host16(pic->format.src_lines);
	network2host16(pic->format.target_pixels);
	network2host16(pic->format.target_lines);
	network2host16(pic->format.pixel_offset);
	network2host16(pic->format.line_offset);
}

void pic_host2network(struct _image_data *pic)
{
	host2network32(pic->version);
	host2network32(pic->mode);
	host2network32(pic->cam);
	host2network32(pic->vid_format);
	host2network32(pic->start_offset);
	host2network32(pic->size);
	host2network32(pic->max_size);
	host2network32(pic->target_size);
	host2network32(pic->factor);
	host2network32(pic->alm_bitmask_hi);
	host2network32(pic->status);
	host2network32(pic->session_time);
	host2network32(pic->milliseconds);
	host2network32(pic->utc_offset);
	host2network32(pic->alm_bitmask);
	host2network16(pic->format.src_pixels);
	host2network16(pic->format.src_lines);
	host2network16(pic->format.target_pixels);
	host2network16(pic->format.target_lines);
	host2network16(pic->format.pixel_offset);
	host2network16(pic->format.line_offset);
}

void pic_le2host(struct _image_data *pic)
{
	le2host32(pic->version);
	le2host32(pic->mode);
	le2host32(pic->cam);
	le2host32(pic->vid_format);
	le2host32(pic->start_offset);
	le2host32(pic->size);
	le2host32(pic->max_size);
	le2host32(pic->target_size);
	le2host32(pic->factor);
	le2host32(pic->alm_bitmask_hi);
	le2host32(pic->status);
	le2host32(pic->session_time);
	le2host32(pic->milliseconds);
	le2host32(pic->utc_offset);
	le2host32(pic->alm_bitmask);
	le2host16(pic->format.src_pixels);
	le2host16(pic->format.src_lines);
	le2host16(pic->format.target_pixels);
	le2host16(pic->format.target_lines);
	le2host16(pic->format.pixel_offset);
	le2host16(pic->format.line_offset);
}

void pic_host2le(struct _image_data *pic)
{
	host2le32(pic->version);
	host2le32(pic->mode);
	host2le32(pic->cam);
	host2le32(pic->vid_format);
	host2le32(pic->start_offset);
	host2le32(pic->size);
	host2le32(pic->max_size);
	host2le32(pic->target_size);
	host2le32(pic->factor);
	host2le32(pic->alm_bitmask_hi);
	host2le32(pic->status);
	host2le32(pic->session_time);
	host2le32(pic->milliseconds);
	host2le32(pic->utc_offset);
	host2le32(pic->alm_bitmask);
	host2le16(pic->format.src_pixels);
	host2le16(pic->format.src_lines);
	host2le16(pic->format.target_pixels);
	host2le16(pic->format.target_lines);
	host2le16(pic->format.pixel_offset);
	host2le16(pic->format.line_offset);
}

void pic_be2host(struct _image_data *pic)
{
	be2host32(pic->version);
	be2host32(pic->mode);
	be2host32(pic->cam);
	be2host32(pic->vid_format);
	be2host32(pic->start_offset);
	be2host32(pic->size);
	be2host32(pic->max_size);
	be2host32(pic->target_size);
	be2host32(pic->factor);
	be2host32(pic->alm_bitmask_hi);
	be2host32(pic->status);
	be2host32(pic->session_time);
	be2host32(pic->milliseconds);
	be2host32(pic->utc_offset);
	be2host32(pic->alm_bitmask);
	be2host16(pic->format.src_pixels);
	be2host16(pic->format.src_lines);
	be2host16(pic->format.target_pixels);
	be2host16(pic->format.target_lines);
	be2host16(pic->format.pixel_offset);
	be2host16(pic->format.line_offset);
}

void pic_host2be(struct _image_data *pic)
{
	host2be32(pic->version);
	host2be32(pic->mode);
	host2be32(pic->cam);
	host2be32(pic->vid_format);
	host2be32(pic->start_offset);
	host2be32(pic->size);
	host2be32(pic->max_size);
	host2be32(pic->target_size);
	host2be32(pic->factor);
	host2be32(pic->alm_bitmask_hi);
	host2be32(pic->status);
	host2be32(pic->session_time);
	host2be32(pic->milliseconds);
	host2be32(pic->utc_offset);
	host2be32(pic->alm_bitmask);
	host2be16(pic->format.src_pixels);
	host2be16(pic->format.src_lines);
	host2be16(pic->format.target_pixels);
	host2be16(pic->format.target_lines);
	host2be16(pic->format.pixel_offset);
	host2be16(pic->format.line_offset);
}




static int adpic_probe(AVProbeData *p)
{
    if (p->buf_size <= 6)
        return 0;
    if ( (p->buf[0] == 9) && (p->buf[2] == 0) && (p->buf[3] == 0) )
    {
        return AVPROBE_SCORE_MAX;
    }
    else
        return 0;
}

static int adpic_read_close(AVFormatContext *s)
{
#if 0
    int i;
//    ADPICDecContext *adpic = s->priv_data;

    for(i=0;i<s->nb_streams;i++) 
	{
        AVStream *st = s->streams[i];
        av_free(st);
    }
#endif
    return 0;
}

static int adpic_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
#if 0
ByteIOContext *pb = &s->pb;
char header_str[256], *hptr, *h0;
int i, found = FALSE;
uint32_t header_word;
h0 = hptr = &header_str[0];

#if 0
	while(!url_feof(pb))
	{
		*hptr = url_fgetc(pb);
		if ((hptr-h0)>3)
		{
			if ( (*(hptr-0) == 0x0a) && (*(hptr-1) == 0x0d) && (*(hptr-2) == 0x0a) && (*(hptr-3) == 0x0d) )
			{
				found = TRUE;
				break;
			}
		}
		else if ((hptr-h0)>=255)
		{
			break;
		}
		else if (*hptr==0)
		{
			break;
		}
	}
	*hptr = 0;
#else
	hptr = (char *)&header_word;
	for (i=0; i<4; i++)
	{
		*hptr++ = pb->buf_ptr[i];
		if (url_feof(pb))
			return -1;
	}
	network2host32(header_word);
	if (header_word == 0x09000000)
		found = TRUE;
#endif

	if (!found)
	{
		return -1;
	}
//	printf("%s",header_str );
#endif
    s->ctx_flags |= AVFMTCTX_NOHEADER;
//    s->ctx_flags |= 0;
					
	return 0;
}

static AVStream *get_stream(struct AVFormatContext *s, struct _image_data *pic)
{
int xres = pic->format.target_pixels;
int yres = pic->format.target_lines;
int codec_type, codec_id, id;
int i, found;
AVStream *st;
	switch(pic->vid_format)
	{
	case  PIC_MODE_JPEG_422 :
	case  PIC_MODE_JPEG_411 :
		codec_id = CODEC_ID_MJPEG;
		codec_type = 0;
		break;
	case  PIC_MODE_MPEG4_411 :
	case  PIC_MODE_MPEG4_411_I :
	case  PIC_MODE_MPEG4_411_GOV_P :
	case  PIC_MODE_MPEG4_411_GOV_I :
		codec_id = CODEC_ID_MPEG4;
		codec_type = 1;
		break;
	default:
		logger(LOG_DEBUG,"ADPIC: get_stream, unrecognised vid_format %d\n", pic->vid_format);
		return NULL;
	}
	found = FALSE;
	id = (codec_type<<31)|((pic->cam-1)<<24)|((xres>>4)<<12)|((yres>>4)<<0);
	for (i=0; i<s->nb_streams; i++)
	{
		st = s->streams[i];
		if (st->id == id)
		{
			found = TRUE;
			break;
		}
	}
	if (!found)
	{
		st = av_new_stream( s, id);
		if (st)
		{
		    st->codec->codec_type = CODEC_TYPE_VIDEO;
		    st->codec->codec_id = codec_id;
			st->codec->width = pic->format.target_pixels;
			st->codec->height = pic->format.target_lines;
			st->index = i;
		}
	}
	return st;
}

int adpic_read_packet(struct AVFormatContext *s, AVPacket *pkt)
{
ByteIOContext *pb = &s->pb;
ADPICContext *adpic = s->priv_data;
AVStream *st;
unsigned char adpkt[SEPARATOR_SIZE];
int data_type;
int data_channel;
int n, size;
int status;
char scratch[1024];
struct _image_data pic0, *pic;
int i = 0;

	// First read the 6 byte separator
	if ((n=get_buffer(pb, &adpkt[0], SEPARATOR_SIZE)) != SEPARATOR_SIZE)
	{
		logger(LOG_DEBUG,"ADPIC: short of data reading seperator, expected %d, read %d\n", SEPARATOR_SIZE, n);
		return -1;
	}
	pic = &pic0;

	memcpy(&size, &adpkt[DATA_SIZE_BYTE_0], 4);
	network2host32(size);

	data_type = adpkt[DATA_TYPE];
	data_channel = adpkt[DATA_CHANNEL];

	switch(data_type)
	{
	case DATA_JPEG:
		{
		int header_size;
		char jfif[2048], *ptr;
			// Read the pic structure
			if ((n=get_buffer(pb, (char *)pic, sizeof (*pic))) != sizeof (*pic))
			{
				logger(LOG_DEBUG,"ADPIC: short of data reading pic struct, expected %d, read %d\n", sizeof (*pic), n);
				return -1;
			}
			// Endian convert if necessary
			pic_network2host(pic);
			if (!pic_version_valid(pic->version))
			{
				logger(LOG_DEBUG,"ADPIC: invalid pic version 0x%08X\n", pic->version);
				return -1;
			}
			// Skip any additional text in the image
			if ((n=get_buffer(pb, scratch, pic->start_offset)) != pic->start_offset)
			{
				logger(LOG_DEBUG,"ADPIC: short of data reading start_offset data, expected %d, read %d\n", size, n);
				return -1;
			}
			// Use the pic struct to build a JFIF header 
			if ((header_size = build_jpeg_header( jfif, pic, FALSE, 2048))<=0)
			{
				logger(LOG_DEBUG,"ADPIC: adpic_read_packet, build_jpeg_header failed\n");
				return -1;
			}
			// We now know the packet size required for the image, allocate it.
			if ((status = av_new_packet(pkt, header_size+pic->size+2))<0) // PRC 003
			{
				logger(LOG_DEBUG,"ADPIC: DATA_JPEG av_new_packet %d failed, status %d\n", header_size+pic->size+2, status);
				return -1;
			}
			ptr = pkt->data;
			// Copy the JFIF header into the packet
			memcpy(ptr, jfif, header_size);
			ptr += header_size;
			// Now get the compressed JPEG data into the packet
			// Read the pic structure
			if ((n=get_buffer(pb, ptr, pic->size)) != pic->size)
			{
				logger(LOG_DEBUG,"ADPIC: short of data reading pic body, expected %d, read %d\n", pic->size, n);
				return -1;
			}
			ptr += pic->size;
			// Add the EOI marker
			*ptr++ = 0xff;
			*ptr++ = 0xd9;
		}
		break;
	case DATA_JFIF:
		{
		char site[32];
			if ((status = av_new_packet(pkt, size))<0) // PRC 003
			{
				logger(LOG_DEBUG,"ADPIC: DATA_JFIF av_new_packet %d failed, status %d\n", size, status);
				return -1;
			}
			if ((n=get_buffer(pb, pkt->data, size)) != size)
			{
				logger(LOG_DEBUG,"ADPIC: short of data reading jfif image, expected %d, read %d\n", size, n);
				return -1;
			}
			if ( parse_jfif_header(pkt->data, pic, size, NULL, NULL, site, TRUE) <= 0)
			{
				logger(LOG_DEBUG,"ADPIC: adpic_read_packet, parse_jfif_header failed\n");
				return -1;
			}
		}
		break;
	case DATA_MPEG4I:
	case DATA_MPEG4P:
		{
			if ((n=get_buffer(pb, (char *)pic, sizeof (*pic))) != sizeof (*pic))
			{
				logger(LOG_DEBUG,"ADPIC: short of data reading pic struct, expected %d, read %d\n", sizeof (*pic), n);
				return -1;
			}
			pic_network2host(pic);
			if (!pic_version_valid(pic->version))
			{
				logger(LOG_DEBUG,"ADPIC: invalid pic version 0x%08X\n", pic->version);
				return -1;
			}
			if ((n=get_buffer(pb, scratch, pic->start_offset)) != pic->start_offset)
			{
				logger(LOG_DEBUG,"ADPIC: short of data reading start_offset data, expected %d, read %d\n", size, n);
				return -1;
			}
			if ((status = av_new_packet(pkt, pic->size))<0) // PRC 003
			{
				logger(LOG_DEBUG,"ADPIC: DATA_MPEG4 av_new_packet %d failed, status %d\n", pic->size, status);
				return -1;
			}
			if ((n=get_buffer(pb, pkt->data, pic->size)) != pic->size)
			{
				logger(LOG_DEBUG,"ADPIC: short of data reading pic body, expected %d, read %d\n", pic->size, n);
				return -1;
			}
		}	
		break;
	case DATA_INFO:
	case DATA_LAYOUT:

		logger(LOG_DEBUG,"ADPIC: adpic_read_packet, found data_type=%d, %d bytes, skipping\n", data_type, size );		
		if ((n=get_buffer(pb, scratch, size)) != size)
		{
			logger(LOG_DEBUG,"ADPIC: short of data reading scratch data body, expected %d, read %d\n", size, n);
			return -1;
		}

		pkt->data = NULL;
		pkt->size = 0;

        // CS - We can't return 0 here as if this is called during setup, we won't have initialised the stream and this will cause a crash
        // in ffmpeg. Make a recursive call here to read the next packet which should be an image frame1
		return adpic_read_packet( s, pkt );
	default:
		logger(LOG_DEBUG,"ADPIC: adpic_read_packet, No handler for data_type=%d\n", data_type );
		return -1;
		
	}
	// At this point se have a legal pic structure which we use to determine 
	// which codec stream to use
	if (data_channel != (pic->cam-1))
	{
		logger(LOG_DEBUG,"ADPIC: adpic_read_packet, data channel (%d) and camera (%d) do not match%d\n", data_channel, pic->cam );
	}
	if ( (st = get_stream( s, pic)) == NULL )
	{
		logger(LOG_DEBUG,"ADPIC: adpic_read_packet, failed get_stream\n");
		return -1;
	}
    pkt->stream_index = st->index;
	pkt->priv =av_malloc(sizeof(*pic));
	if (pkt->priv)
		memcpy(pkt->priv, pic, sizeof(*pic));
	pkt->duration =  ((int)(AV_TIME_BASE * 1.0));
	return 0;
}

static int64_t adpic_read_pts(AVFormatContext *s, int stream_index, int64_t *ppos, int64_t pos_limit)
{
	return -1;
}

static int adpic_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp)
{
	return -1;
}


static AVInputFormat adpic_iformat = {
    "adpic",
    "adpic format",
    sizeof(ADPICContext),
    adpic_probe,
    adpic_read_header,
    adpic_read_packet,
    adpic_read_close,
    adpic_read_seek,
    adpic_read_pts,
};


int adpic_init(void)
{
    av_register_input_format(&adpic_iformat);
    return 0;
}

int logger (int log_level, const char *fmt, ...)
{
#if 0
    va_list args;

    va_start( args, fmt ); 

    TRACE( fmt, args );

    va_end(args);
#endif
}