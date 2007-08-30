// $HDR$
//$Log:  126594: jfif_img.h 
//
//    Rev 1.0    30/11/2006 08:53:02  pcolbran
// AD JPEG to JFIF conversion routines
#ifndef __JFIF_IMG__
#define __JFIF_IMG__
#include <time.h>
#include "adpic.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

extern unsigned int build_jpeg(void *image, void *jfif, struct _image_data *pic, unsigned int max, int inc_dm_header);	/* JCB 003 */
extern unsigned int build_jpeg_header(void *jfif, NetVuImageData *pic, int add_comment, unsigned int max);
extern unsigned int build_jpeg_header_lite(void *image, void *jfif, struct _image_data *pic, unsigned int max);	// PJB 001
extern unsigned int add_jpeg_comment(void *j_in, void *j_out, int max, int camera, time_t pictime, int utc_oset, char *locale, char *title);	// JCB 009
extern int parse_jfif_header(unsigned char *data, struct _image_data *pic, int imglength, unsigned char **qy, unsigned char **qc, char *site, int decode_comment); // JCB 010
extern int build_comment_text(char *buffer, struct _image_data *pic, int max);
extern void parse_comment(char *text, struct _image_data *pic, char *site);	// PRC 008
extern int read_jfif(char *filename, struct _image_data *pic );
extern void image_status_message(char *string);
extern int find_q(unsigned char *qy);
extern int parse_jfif_stream(unsigned char *data, struct _image_data *pic, int imglength, unsigned char **qy, unsigned char **qc, char *site, int handle, int (*read_func)(int handle, char *buff, int nbytes));

#endif

