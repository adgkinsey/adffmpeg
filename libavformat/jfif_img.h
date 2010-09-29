#ifndef __JFIF_IMG__
#define __JFIF_IMG__

#include "adpic.h"

extern unsigned int build_jpeg_header(void *jfif, NetVuImageData *pic, int add_comment, unsigned int max);
extern int parse_jfif_header(unsigned char *data, NetVuImageData *pic, int imglength, unsigned char **qy, unsigned char **qc, char *site, int decode_comment, char **additionalText ); // JCB 010

#endif

