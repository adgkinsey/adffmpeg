/*
 * Build a C550 compressed image into a JFIF format
 * 
 * Copyright (c) 2006-2010 AD-Holdings plc
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * build_jpeg()
 * Build the correct JFIF headers & tables for the supplied compressed image data
 * 
 * calc_q_tables()
 * Calculate the correct Q tables for the specified Q factor
 */

#include "internal.h"
#include "libavutil/bswap.h"

#include "jfif_img.h"
#include "adpic.h"

static int find_q(unsigned char *qy);
static void parse_comment( char *text, int text_len, NetVuImageData *pic, char **additionalText );
static void calcQtabs(void);


#if HAVE_BIGENDIAN
#define host2network16(x)
#else
#define host2network16(x) ((void)(x=bswap_16(x)))
#endif


/* JCB 004 start */
static const char comment_version[] = "Version: 00.02\r\n";
static const char comment_version_0_1[] = "Version: 00.01\r\n";
static const char camera_title[] = "Name: ";
static const char camera_number[] = "Number: ";
static const char image_date[] = "Date: ";
static const char image_time[] = "Time: ";
static const char image_ms[] = "MSec: ";
static const char q_factor[] = "Q-Factor: ";
static const char alarm_comment[] = "Alarm-text: ";
static const char active_alarms[] = "Active-alarms: ";	/* JCB 007 */
static const char active_detectors[] = "Active-detectors: ";	/* JCB 021 */
static const char script_msg[] = "Comments: ";			/* JCB 007 */
static const char time_zone[] = "Locale: ";				/* JCB 005 */
static const char utc_offset[] = "UTCoffset: ";			/* JCB 005 */
/* JCB 004 end */
static char comment_msg[256];

static const unsigned char jfif_header[] =
{	0xFF,0xD8,0xFF,0xE0,0x00,0x10,0x4A,0x46,0x49,0x46,0x00,0x01,0x02,0x02,0x00,0x32,0x00,0x19,0x00,0x00};	// JCB 019

static const unsigned char sof_422_header[] =
{	0xFF,0xC0,0x00,0x11,0x08,0x01,0x00,0x01,0x60,0x03,0x01,0x21,0x00,0x02,0x11,0x01,0x03,0x11,0x01}; // 422
static const unsigned char sof_411_header[] =
{	0xFF,0xC0,0x00,0x11,0x08,0x01,0x00,0x01,0x60,0x03,0x01,0x22,0x00,0x02,0x11,0x01,0x03,0x11,0x01}; // 411

static const unsigned char huf_header[] =
{ 	0xFF,0xC4,0x00,0x1F,0x00,0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,
	0x0B,0xFF,0xC4,0x00,0xB5,0x10,0x00,0x02,0x01,0x03,0x03,0x02,0x04,0x03,0x05,0x05,
	0x04,0x04,0x00,0x00,0x01,0x7D,0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,
	0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xA1,0x08,0x23,0x42,
	0xB1,0xC1,0x15,0x52,0xD1,0xF0,0x24,0x33,0x62,0x72,0x82,0x09,0x0A,0x16,0x17,0x18,
	0x19,0x1A,0x25,0x26,0x27,0x28,0x29,0x2A,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x43,
	0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x63,
	0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x83,
	0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,
	0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,
	0xB9,0xBA,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xD2,0xD3,0xD4,0xD5,0xD6,
	0xD7,0xD8,0xD9,0xDA,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xF1,0xF2,
	0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFF,0xC4,0x00,0x1F,0x01,0x00,0x03,0x01,
	0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,
	0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0xFF,0xC4,0x00,0xB5,0x11,0x00,0x02,
	0x01,0x02,0x04,0x04,0x03,0x04,0x07,0x05,0x04,0x04,0x00,0x01,0x02,0x77,0x00,0x01,
	0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,
	0x32,0x81,0x08,0x14,0x42,0x91,0xA1,0xB1,0xC1,0x09,0x23,0x33,0x52,0xF0,0x15,0x62,
	0x72,0xD1,0x0A,0x16,0x24,0x34,0xE1,0x25,0xF1,0x17,0x18,0x19,0x1A,0x26,0x27,0x28,
	0x29,0x2A,0x35,0x36,0x37,0x38,0x39,0x3A,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,
	0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,
	0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
	0x8A,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,
	0xA8,0xA9,0xAA,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xC2,0xC3,0xC4,0xC5,
	0xC6,0xC7,0xC8,0xC9,0xCA,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xE2,0xE3,
	0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA };

static const unsigned char sos_header[] =
{	0xFF,0xDA,0x00,0x0c,0x03,0x01,0x00,0x02,0x11,0x03,0x11,0x00,0x3F,0x00};
#ifdef DRI_HEADER
static const unsigned char dri_header[] =
{	0xFF,0xDD,0x00,0x04,0x00,0x02};
#endif
static const unsigned char soc_header[] =	/* JCB 004 comment header */
{	0xFF, 0xFE, 0x00, 0x00 };

/****************************************************************************
 Initialised Global Data
****************************************************************************/


/* VARIOUS TABLES */


unsigned short Yvis[64] =
    {
     16   ,11   ,12   ,14   ,12   ,10   ,16   ,14,
     13   ,14   ,18   ,17   ,16   ,19   ,24   ,40,	// PRC 024
     26   ,24   ,22   ,22   ,24   ,49   ,35   ,37,
     29   ,40   ,58   ,51   ,61   ,60   ,57   ,51,
     56   ,55   ,64   ,72   ,92   ,78   ,64   ,68,
     87   ,69   ,55   ,56   ,80   ,109  ,81   ,87,
     95   ,98   ,103  ,104  ,103  ,62   ,77   ,113,
     121  ,112  ,100  ,120  ,92   ,101  ,103  ,99
    };

unsigned short UVvis[64] =
    {
     17  ,18  ,18  ,24  ,21  ,24  ,47  ,26,
     26  ,47  ,99  ,66  ,56  ,66  ,99  ,99,
     99  ,99  ,99  ,99  ,99  ,99  ,99  ,99,
     99  ,99  ,99  ,99  ,99  ,99  ,99  ,99,
     99  ,99  ,99  ,99  ,99  ,99  ,99  ,99,
     99  ,99  ,99  ,99  ,99  ,99  ,99  ,99,
     99  ,99  ,99  ,99  ,99  ,99  ,99  ,99,
     99  ,99  ,99  ,99  ,99  ,99  ,99  ,99   };


unsigned char YQuantizationFactors[256][64], UVQuantizationFactors[256][64]; // PRC 025


static int q_init;

/****************************************************************************

Prototype	  :	unsigned int build_jpeg(void *image, void *jfif, NetVuImageData *pic, unsigned int max)

Procedure Desc: Build the correct JFIF headers & tables for the supplied
				compressed image data

	Inputs	  :	void *jfif		pointer to output buffer
				NetVuImageData *pic		pointer to image structure - includes
								Q factors, image size, mode etc
				int add_comment flag to control addition of comment
				unsigned int max		maximum size of compressed image
	Outputs	  :	unsigned int	total bytes in the JFIF image
	Globals   :	none

****************************************************************************/
unsigned int build_jpeg_header(void *jfif, NetVuImageData *pic, int add_comment, unsigned int max)	/* JCB 004 */
{
volatile unsigned int count;
unsigned int comment_length;
unsigned short    us1;
char  *bufptr = jfif;
char *comment_header;
char line[128];
char sof_copy[sizeof(sof_422_header)];
char *txt;


	if (!q_init)
	{
		calcQtabs();
		q_init = 1;
	}

	/*
	*	Add all the fixed length headers prior to building comment field
	*/
	count = sizeof(jfif_header);
	if (count > max)
		return 0;

	memcpy(bufptr, jfif_header, sizeof(jfif_header));
	bufptr += sizeof(jfif_header);

	/*
	*	Now add the variable length comments
	*/
	/* JCB 006 end */
	if (add_comment)
	{
		if ((count + + sizeof(soc_header) + strlen(comment_version)) > max)
			return 0;
		comment_header = bufptr + 2;
		memcpy(bufptr, soc_header, sizeof(soc_header));
		bufptr += sizeof(soc_header);
		strcpy(bufptr, comment_version);
		bufptr += strlen(comment_version);
		
		snprintf(line, 128, "%s%d\r\n",camera_number, pic->cam);
		count += strlen(line);
		if (count > max)
			return 0;
		else
		{
			strcpy(bufptr, line);
			bufptr += strlen(line);
		}

		snprintf(line, 128, "%s%s\r\n",camera_title, pic->title);
		count += strlen(line);
		if (count > max)
			return 0;
		else
		{
			strcpy(bufptr, line);
			bufptr += strlen(line);
		}

		if (pic->alarm[0])
		{
			snprintf(line, 128, "%s%s\r\n",alarm_comment, pic->alarm);
			count += strlen(line);
			if (count > max)
				return 0;
			else
			{
				strcpy(bufptr, line);
				bufptr += strlen(line);
			}
		}

		if (pic->alm_bitmask)	/* JCB 008 */
		{
			snprintf(line, 128, "%s%08X\r\n",active_alarms, pic->alm_bitmask);	/* JCB 008 */
			count += strlen(line);
			if (count > max)
				return 0;
			else
			{
				strcpy(bufptr, line);
				bufptr += strlen(line);
			}
		}

		if (pic->alm_bitmask_hi)	/* JCB 021 */
		{
			snprintf(line, 128, "%s%08X\r\n",active_detectors, pic->alm_bitmask_hi);
			count += strlen(line);
			if (count > max)
				return 0;
			else
			{
				strcpy(bufptr, line);
				bufptr += strlen(line);
			}
		}

		if (comment_msg[0])
		{
			snprintf(line, 128, "%s%s\r\n",script_msg, comment_msg);
			count += strlen(line);
			if (count > max)
				return 0;
			else
			{
				strcpy(bufptr, line);
				bufptr += strlen(line);
			}
		}

		count += strlen(image_date);
		if (count > max)
			return 0;
		else
		{
			strcpy(bufptr, image_date);
			bufptr += strlen(image_date);
		}
		strftime (line, sizeof(line), "%d/%m/%Y\r\n", localtime((const time_t*)&pic->session_time));
		count += strlen(line);
		if (count > max)
			return 0;
		else
		{
			strcpy(bufptr, line);
			bufptr += strlen(line);
		}

		count += strlen(image_time);
		if (count > max)
			return 0;
		else
		{
			strcpy(bufptr, image_time);
			bufptr += strlen(image_time);
		}
		strftime (line, sizeof(line), "%H:%M:%S\r\n", localtime((const time_t*)&pic->session_time));
		count += strlen(line);
		if (count > max)
			return 0;
		else
		{
			strcpy(bufptr, line);
			bufptr += strlen(line);
		}

		count += strlen(image_ms);	/* PRC 009 */
		if (count > max)
			return 0;
		else
		{
			strcpy(bufptr, image_ms);
			bufptr += strlen(image_ms);
		}
		snprintf (line, 128, "%u\r\n", pic->milliseconds%1000 );// PRC 013
		count += strlen(line);
		if (count > max)
			return 0;
		else
		{
			strcpy(bufptr, line);
			bufptr += strlen(line);
		}

		snprintf(line, 128, "%s%s\r\n",time_zone, pic->locale);
		count += strlen(line);
		if (count > max)
			return 0;
		else
		{
			strcpy(bufptr, line);
			bufptr += strlen(line);
		}

		snprintf(line, 128, "%s%d\r\n",utc_offset, pic->utc_offset);
		count += strlen(line);
		if (count > max)
			return 0;
		else
		{
			strcpy(bufptr, line);
			bufptr += strlen(line);
		}

		if (pic->start_offset > 0)
		{
			txt = (char *)&pic[1];
			count += pic->start_offset;
			if (count > max)
				return 0;
			else
			{
#if 0
			char cpy[80];
				memset(cpy,0,80);
				strncpy(cpy,txt,pic->start_offset);
				//logger (LOG_DEBUG, "JFIF_IMG: 1 adding text %s\n", cpy);
#endif
				strncpy(bufptr, txt, pic->start_offset);
				bufptr += pic->start_offset;
			}
		}

		comment_length = bufptr - comment_header;
		*comment_header++ = comment_length>>8;
		*comment_header++ = comment_length & 0xff;
	}
	count += 138;		/* Q tables and markers */
	if (count > max)
		return 0;
	*bufptr++=0xff;
	*bufptr++=0xdb;
	*bufptr++=0x00;
	*bufptr++=0x43;
	*bufptr++=0x00;
	for (us1=0; us1<64; us1++)
		*bufptr++ = YQuantizationFactors[pic->factor][us1];
	*bufptr++=0xff;
	*bufptr++=0xdb;
	*bufptr++=0x00;
	*bufptr++=0x43;
	*bufptr++=0x01;
	for (us1=0; us1<64; us1++)
		*bufptr++ = UVQuantizationFactors[pic->factor][us1];
#ifdef DRI_HEADER
	count += sizeof(dri_header);
	if (count > max)
		return 0;
	else
	{
		memcpy(bufptr, dri_header, sizeof(dri_header));
		bufptr += sizeof(dri_header);
	}
#endif
	count += (sizeof(sof_copy) + sizeof(huf_header) + sizeof(sos_header));
	if (count > max)
		return 0;
	else
	{
	unsigned short targ;
		char *ptr1, *ptr2;
		memcpy(sof_copy,(pic->vid_format == PIC_MODE_JPEG_411) ? sof_411_header: sof_422_header, sizeof( sof_copy ) );
		targ = pic->format.target_pixels;
		host2network16(targ);
		ptr1 = (char *)&targ;
		ptr2 = &sof_copy[7];
		*ptr2++ = *ptr1++;
		*ptr2 = *ptr1;

		targ = pic->format.target_lines;
		host2network16(targ);
		ptr1 = (char *)&targ;
		ptr2 = &sof_copy[5];
		*ptr2++ = *ptr1++;
		*ptr2 = *ptr1;

		memcpy(bufptr, sof_copy, sizeof(sof_copy));
		bufptr += sizeof(sof_copy);

		memcpy(bufptr, huf_header, sizeof(huf_header));
		bufptr += sizeof(huf_header);

		memcpy(bufptr, sos_header, sizeof(sos_header));
		bufptr += sizeof(sos_header);
	}
	if (count > max)
		return 0;

	return count;
}

/****************************************************************************

Prototype	  :	static void calc_q_tables(short factor)

Procedure Desc: Calculate the correct Q tables for the specified Q factor

	Inputs	  :	short factor	picture Q factor
	Outputs	  :	none
	Globals   :	Updates the module Q factor tables

****************************************************************************/
static void calcQtabs(void)
{
short i;
int uvfactor;
short factor;
short yval, uvval;	
	for (factor = 1; factor < 256; factor++ )
	{
		uvfactor = factor * 1;
		if (uvfactor > 255)
			uvfactor = 255;
	   	for (i=0;i<64;i++)
   		{
		    /* Version 2.0a modification:                                             */
   			/*          lum_q[] and chr_q[] are now rounded, not truncated.           */

        	yval  = (short)(((Yvis[i]*factor)+25)/50);
	    	uvval= (short)(((UVvis[i]*uvfactor)+25)/50);

		    /* Version 1.0a modification:                                             */
   			/*     The boundry below has been changed from no less than               */
   			/*     2 to no less than 1.                                               */
	   		/* Version 3.1 modification:                                              */
   			/*     The boundry below has been added to cap the quantization values    */
   			/*     to no greater than 255.                                            */

	        if ( yval < 1 )
				yval = 1;  /* The DC and AC values cannot be   */
   		    if ( uvval < 1)
				uvval = 1;   /* less than 1                      */

        	if ( yval > 255 )
				yval = 255;  /* The DC and AC values cannot  */
	   	    if ( uvval > 255)
				uvval = 255;   /* be more than 255             */

        	YQuantizationFactors[factor][i]  = (unsigned char)yval;  // PRC 025
	    	UVQuantizationFactors[factor][i] = (unsigned char)uvval;


   		}
	}
}

static int find_q(unsigned char *qy)
{
int factor, smallest_err=0, best_factor = 0;
unsigned char *q1, *q2, *qe;
unsigned char qtest[64];
int err_diff;

	if (!q_init)
	{
		calcQtabs();
		q_init = 1;
	}

	memcpy(&qtest[0], qy, 64); // PRC 025

	qe = &qtest[32];

	for (factor = 1; factor < 256; factor++ )
	{
		q1 = &qtest[0];
		q2 = &YQuantizationFactors[factor][0];
		err_diff = 0;
		while (q1<qe)
		{
			if (*q1>*q2)
				err_diff = *q1-*q2;
			else
				err_diff = *q2-*q1;
				
			q1++;
			q2++;
		}
		if (err_diff == 0)
		{
			best_factor = factor;
			smallest_err = 0;
			break;
		}
		else if ( err_diff < smallest_err || best_factor == 0 )
		{
			best_factor = factor;
			smallest_err = err_diff;
		}
	}
	if (factor == 256)
	{
		factor = best_factor;
		av_log(NULL, AV_LOG_ERROR, "JFIF_IMG: Unable to match Q setting %d\n", best_factor );
	}
	else
	{
#if 0
		static time_t lastdisplay=0;
		if (lastdisplay - time(NULL) > 10)
		{
			av_log(NULL, AV_LOG_DEBUG, "JFIF_IMG: Matching Q = %d\n", factor );
			lastdisplay = time(NULL);
		}
#endif
	}
	return factor;
}


/****************************************************************************

Prototype	  :	int parse_jfif_header(unsigned char *data, NetVuImageData *pic, int imglength, unsigned char **qy, unsigned char **qc, char *site, int decode_comment)

Procedure Desc: Analyses a JFIF header and fills out an NetVuImageData structure with the
				info

	Inputs	  :	unsigned char *data		input buffer
				NetVuImageData *pic				NetVuImageData structure
				int imglength			total length of input buffer
				unsigned char **qy		pointer for luma   Q table
				unsigned char **qc		pointer for chroma Q table
				char *site				pointer to site_id string
				int decode_comment		TRUE to try to parse any comment in the header
	Outputs	  :	length of JFIF header in bytes
	Globals   :

****************************************************************************/
int parse_jfif_header( unsigned char *data, NetVuImageData *pic, int imglength, unsigned char **qy, unsigned char **qc, char *site, int decode_comment, char **additionalText )
{
int i, sos = FALSE;
unsigned short length, marker;
#ifdef CHECK_HUFFMAN_TABLE // PRC 028
int huf_cmp_offset=0, huf_tab_no=0;
#endif

//	fprintf(stderr, "JFIF_IMG: parse_jfif_header, leading bytes 0x%02X, 0x%02X, imglength=%d\n", data[0], data[1], imglength);
	memset(pic, 0, sizeof(*pic));
	if (qc)
		*qc  = NULL;
	if (qy)
		*qy = NULL;
	if (site)
		*site = 0;
	pic->version = PIC_VERSION;
	pic->factor = -1;
	pic->start_offset = 0;
	i = 0;
	
    if(data[i]!= 0xff && data[i+1] != 0xd8)
    {
        //there is a header so skip it 
        while( ((unsigned char)data[i] != 0xff) && (i<imglength) )
		    i++;
	    if ( i > 0 )
			av_log(NULL, AV_LOG_DEBUG, "JFIF_IMG: parse_jfif_header, %d leading bytes\n", i);
			
	    i++;
	    if ( (unsigned char) data[i] != 0xd8)
	    {
			av_log(NULL, AV_LOG_ERROR, "JFIF_IMG: parse_jfif_header, incorrect SOI 0xff%02x\n", data[i]);
		    return -1;
	    }
	    i++;
    }
    else
    {
        //no header
        i+=2;
    }



	while ( !sos && (i<imglength) )
	{
		memcpy(&marker, &data[i], 2 );
		i+= 2;
		memcpy(&length, &data[i], 2 );
		i+= 2;
		marker = be2me_16(marker);
		length = be2me_16(length);

		switch (marker)
		{
		case 0xffe0 :	// APP0
//			fprintf(stderr, "JFIF_IMG: found APP0 marker, length = %d\n", length );
			i += length-2;
			break;
		case 0xffdb :	// Q table
//			fprintf(stderr, "JFIF_IMG: found Q table marker, length = %d, Table no = %d\n", length, data[i] );
			if (data[i]) // PRC 029
			{
				if (qc)
					*qc = &data[i+1];
			}
			else
			{
				pic->factor =  find_q(&data[i+1]);
				if (qy)
					*qy = &data[i+1];
			}
			i += length-2;
			break;
		case 0xffc0 :	// SOF
			memcpy(&pic->format.target_pixels, &data[i+3], 2);
			pic->format.target_pixels = be2me_16(pic->format.target_pixels);
			memcpy(&pic->format.target_lines, &data[i+1], 2);
			pic->format.target_lines = be2me_16(pic->format.target_lines);
			if (data[i+7]==0x22)	// PRC 022
			{
				pic->vid_format = PIC_MODE_JPEG_411;
			}
			else if (data[i+7]==0x21)
			{
				pic->vid_format = PIC_MODE_JPEG_422;
			}
			else
			{
				av_log(NULL, AV_LOG_INFO, "JFIF_IMG: Unknown format byte 0x%02X\n", data[i+7]);
			}
//			fprintf(stderr, "JFIF_IMG: found SOF marker, length = %d, xres=%d, yres = %d\n", length, pic->format.target_pixels, pic->format.target_lines );
			i += length-2;
			break;
		case 0xffc4 :	// Huffman table
#ifdef CHECK_HUFFMAN_TABLE // PRC 028
			huf_cmp_offset += 2; // skip past FFC4
			if (memcmp(&data[i-2], &huf_header[huf_cmp_offset], length))
			{
				av_log(NULL, AV_LOG_INFO, "JFIF_IMG: found Non default Huffman table %d, length = %d, offset =%d\n", huf_tab_no, length, huf_cmp_offset );
			}
			else
			{
//				fprintf(stderr, "JFIF_IMG: found default Huffman table %d, length = %d, offset =%d\n", huf_tab_no, length, huf_cmp_offset );
			}
			huf_cmp_offset += length;
			huf_tab_no++;
#endif
			i += length-2;
			break;
		case 0xffda :	// SOS
//			fprintf(stderr, "JFIF_IMG: found SOS marker, length = %d\n", length );
			i += length-2;
			sos = TRUE;
			break;
		case 0xffdd :	// DRI
//			fprintf(stderr, "JFIF_IMG: found DRI marker, length = %d\n", length );
			i += length-2;
			break;
		case 0xfffe :	// Comment
//			fprintf(stderr, "JFIF_IMG: found Comment marker, length = %d\n", length );
			if (decode_comment)	// JCB 027
				parse_comment( (char *)&data[i], length-2, pic, additionalText );
			i += length-2;
			break;
		default :
			av_log(NULL, AV_LOG_INFO, "JFIF_IMG: Unknown marker 0x%04X, next byte = 0x%02X, length = %d\n", marker, data[i], length );
//			return -1;
			i += length-2;	// JCB 026 skip past the unknown field
			break;
		}
	}
	pic->size = imglength-i-2; 	// 2 bytes for FFD9 PRC 029
	return i;
}


static void parse_comment( char *text, int text_len, NetVuImageData *pic, char **additionalText )
{
#define RESULT_LEN  512
    char            result[RESULT_LEN];
    int             i = 0;
    int             j = 0;
    struct tm       t;

	memset(&t, 0, sizeof(t));

    /* CS - Have to refactor this function so that it processes the comment block line by line and puts anything that's not in the pic struct
       into an additional text block */
    while( 1 )
    {
        j = 0;

        /* Check we haven't covered all the buffer already */
        if( i >= text_len - 1 || text[i] <= 0 )
            break;

        /* Get the next line from the text block */
        while( text[i] && text[i] != '\n' && (i < (text_len - 1)) )
            result[j++] = text[i++];

        /* Skip the \n */
        if( text[i] == '\n' )
        {
            result[j-1] = 0;
            i++;
        }

        /* NULL terminate */
        /* result[j] = 0; */

        if( !memcmp( result, comment_version, strlen(comment_version) - 2 ) )  /* CS - Changed this line so it doesn't include the \r\n from the end of comment_version */
        {
            pic->version = 0xdecade11;
        }
        else if ( !memcmp( result, comment_version, strlen(comment_version_0_1) - 2 ) )
        {
            pic->version = 0xdecade10;
        }
        else if( !memcmp( result, camera_title, strlen(camera_title) ) )
        {
            strncpy( pic->title, &result[strlen(camera_title)], sizeof(pic->title) );
            pic->title[sizeof(pic->title)-1] = 0;
        }
        else if( !memcmp( result, camera_number, strlen(camera_number) ) )
        {
            sscanf( &result[strlen(camera_number)], "%d", &pic->cam );
        }
        else if( !memcmp( result, image_date, strlen(image_date) ) )
        {
		    sscanf( &result[strlen(image_date)], "%d/%d/%d", &t.tm_mday, &t.tm_mon, &t.tm_year );
#ifdef __MINGW32__
            t.tm_year -= 1900; // Win32 expects tm_year to be years since 1900
#else
		    t.tm_year -= 1970; // PRC 032
#endif
		    t.tm_mon--;
        }
        else if( !memcmp( result, image_time, strlen(image_time) ) )
        {
		    sscanf( &result[strlen(image_time)], "%d:%d:%d", &t.tm_hour, &t.tm_min, &t.tm_sec );
        }
        else if( !memcmp( result, image_ms, strlen(image_ms) ) )
        {
		    sscanf( &result[strlen(image_ms)], "%d", &pic->milliseconds );
        }
        else if( !memcmp( result, q_factor, strlen(q_factor) ) )
        {
            sscanf( &result[strlen(q_factor)], "%d", &pic->factor );
        }
	    else if( !memcmp( result, alarm_comment, strlen(alarm_comment) ) )
	    {
		    strncpy( pic->alarm, &result[strlen(alarm_comment)], sizeof(pic->title) );
        }
	    else if( !memcmp( result, active_alarms, strlen(active_alarms) ) )
	    {
		    sscanf( &result[strlen(active_alarms)], "%X", &pic->alm_bitmask );
        }
	    else if( !memcmp( result, active_detectors, strlen(active_detectors) ) )
        {
		    sscanf( &result[strlen(active_detectors)], "%X", &pic->alm_bitmask_hi );
        }
        else if( !memcmp( result, script_msg, strlen(script_msg) ) )
        {
        }
        else if( !memcmp( result, time_zone, strlen(time_zone) ) )
        {
            strncpy( pic->locale, &result[strlen(time_zone)], sizeof(pic->locale) );
        }
        else if( !memcmp( result, utc_offset, strlen(utc_offset) ) )
        {
            sscanf( &result[strlen(utc_offset)], "%d", &pic->utc_offset );
        }
        else
        {
            /* Any line we don't explicitly detect for extraction to the pic struct, we must add to the additional text block */
            /* Any lines that aren't part of the pic struct, tag onto the additional text block */
            if( additionalText != NULL )
            {
                int             strLen  = 0;
                const char      lineEnd[3] = { '\r', '\n', '\0' };

                /* Get the length of the existing text block if it exists */
                if( *additionalText != NULL )
                    strLen = strlen( *additionalText );

                /* Ok, now allocate some space to hold the new string */
                *additionalText = av_realloc( *additionalText, strLen + strlen(result) + 3 );

                /* Copy the line into the text block */
                memcpy( &(*additionalText)[strLen], result, strlen(result) );

                /* Add a \r\n and NULL termination */
                memcpy( &(*additionalText)[strLen + strlen(result)], lineEnd, 3 );

                /* NULL terminate */
                /* (*additionalText)[strLen + strlen(result)] = 0; */
            }
        }
    }

    pic->session_time = mktimegm(&t); 
    //pic->session_time = mktime(&t);
}
