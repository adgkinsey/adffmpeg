//---------------------------------------------------------------------------

//#include <vcl.h>
//#pragma hdrstop
#if !defined(__MINGW32__) && defined(WIN32)  /* MSVC specific */
#pragma warning(disable : 4995)
#pragma warning(disable : 4996)
#endif /* WIN32 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "avformat.h"
#include "dsenc.h"

static void MD5Init(MD5_CTX * context, unsigned char *pub_key);
static void MD5Update(MD5_CTX* context, unsigned char* input, unsigned int inputLen);
static void MD5Final(unsigned char * digest, MD5_CTX * context);
static void MD5Transform(UINT4 * state, unsigned char * block);
static void Encode(unsigned char * output, UINT4 * input, unsigned int len);
static void Decode(UINT4 * output, unsigned char * input, unsigned int len);
static unsigned char hex_val (char *char2);
static void MD5Print(char* FingerPrint, int size, unsigned char * digest);

//---------------------------------------------------------------------------

#pragma pack(1)
//#pragma package(smart_init)

static unsigned char PADDING[64] = 
{
  0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};


/* MD5 initialization. Begins an MD5 operation, writing a new context. */
static void MD5Init(MD5_CTX * context, unsigned char *pub_key)
{
	context->count[0] = context->count[1] = 0;
	/* Load magic initialization constants.*/
	context->state[0] = 0x67452301;
	context->state[1] = 0xefcdab89;
	context->state[2] = 0x98badcfe;
	context->state[3] = 0x10325476;
	if (pub_key != NULL)
	{
		context->state[0] &= 0xFF0000FF ;
		context->state[0] |= ((pub_key[4]<<8)|(pub_key[1]<<16)) ;
		context->state[1] &= 0xFFFFFF00 ;
		context->state[1] |= (pub_key[5]) ;
		context->state[2] &= 0x00FF00FF ;
		context->state[2] |= ((pub_key[0]<<8)|(pub_key[2]<<24)) ;
		context->state[3] &= 0xFF00FFFF ;
		context->state[3] |= (pub_key[3]<<16) ;
	}
}

/* MD5 block update operation. Continues an MD5 message-digest
  operation, processing another message block, and updating the
  context.
 */
static void MD5Update(MD5_CTX* context, unsigned char* input, unsigned int inputLen)
{
	unsigned int i, index, partLen;

	/* Compute number of bytes mod 64 */
	index = (unsigned int)((context->count[0] >> 3) & 0x3F);

	/* Update number of bits */
	if ((context->count[0] += ((UINT4)inputLen << 3)) < ((UINT4)inputLen << 3))
		context->count[1]++;
		
	context->count[1] += ((UINT4)inputLen >> 29);

	partLen = 64 - index;

	/* Transform as many times as possible */
	if (inputLen >= partLen) 
	{
		memcpy((POINTER)&context->buffer[index], (POINTER)input, partLen);
		MD5Transform (context->state, context->buffer);

		for (i = partLen; i + 63 < inputLen; i += 64)
			MD5Transform (context->state, &input[i]);

			index = 0;
	}
	else
	{
		i = 0;
	}

	/* Buffer remaining input */
    if (inputLen > i)
	    memcpy((POINTER)&context->buffer[index], (POINTER)&input[i], inputLen-i);
}

/* MD5 finalization. Ends an MD5 message-digest operation, writing the
  the message digest and zeroizing the context.
 */
static void MD5Final(unsigned char * digest, MD5_CTX * context)
{
	unsigned char bits[8];
	unsigned int index, padLen;

	/* Save number of bits */
	Encode (bits, context->count, 8);

	/* Pad out to 56 mod 64 */
	index = (unsigned int)((context->count[0] >> 3) & 0x3f);
	padLen = (index < 56) ? (56 - index) : (120 - index);
	MD5Update (context, PADDING, padLen);

	/* Append length (before padding) */
	MD5Update (context, bits, 8);
	/* Store state in digest */
	Encode (digest, context->state, 16);

	/* Zeroize sensitive information */
	memset ((POINTER)context, 0, sizeof (*context));
}



/* MD5 basic transformation. Transforms state based on block */
static void MD5Transform(UINT4 * state, unsigned char * block)
{
	UINT4 a = state[0], b = state[1], c = state[2], d = state[3], x[16];

	Decode (x, block, 64);

	/* Round 1 */
	FF (a, b, c, d, x[ 0], S11, 0xd76aa478); /* 1 */
	FF (d, a, b, c, x[ 1], S12, 0xe8c7b756); /* 2 */
	FF (c, d, a, b, x[ 2], S13, 0x242070db); /* 3 */
	FF (b, c, d, a, x[ 3], S14, 0xc1bdceee); /* 4 */
	FF (a, b, c, d, x[ 4], S11, 0xf57c0faf); /* 5 */
	FF (d, a, b, c, x[ 5], S12, 0x4787c62a); /* 6 */
	FF (c, d, a, b, x[ 6], S13, 0xa8304613); /* 7 */
	FF (b, c, d, a, x[ 7], S14, 0xfd469501); /* 8 */
	FF (a, b, c, d, x[ 8], S11, 0x698098d8); /* 9 */
	FF (d, a, b, c, x[ 9], S12, 0x8b44f7af); /* 10 */
	FF (c, d, a, b, x[10], S13, 0xffff5bb1); /* 11 */
	FF (b, c, d, a, x[11], S14, 0x895cd7be); /* 12 */
	FF (a, b, c, d, x[12], S11, 0x6b901122); /* 13 */
	FF (d, a, b, c, x[13], S12, 0xfd987193); /* 14 */
	FF (c, d, a, b, x[14], S13, 0xa679438e); /* 15 */
	FF (b, c, d, a, x[15], S14, 0x49b40821); /* 16 */

	/* Round 2 */
	GG (a, b, c, d, x[ 1], S21, 0xf61e2562); /* 17 */
	GG (d, a, b, c, x[ 6], S22, 0xc040b340); /* 18 */
	GG (c, d, a, b, x[11], S23, 0x265e5a51); /* 19 */
	GG (b, c, d, a, x[ 0], S24, 0xe9b6c7aa); /* 20 */
	GG (a, b, c, d, x[ 5], S21, 0xd62f105d); /* 21 */
	GG (d, a, b, c, x[10], S22,  0x2441453); /* 22 */
	GG (c, d, a, b, x[15], S23, 0xd8a1e681); /* 23 */
	GG (b, c, d, a, x[ 4], S24, 0xe7d3fbc8); /* 24 */
	GG (a, b, c, d, x[ 9], S21, 0x21e1cde6); /* 25 */
	GG (d, a, b, c, x[14], S22, 0xc33707d6); /* 26 */
	GG (c, d, a, b, x[ 3], S23, 0xf4d50d87); /* 27 */
	GG (b, c, d, a, x[ 8], S24, 0x455a14ed); /* 28 */
	GG (a, b, c, d, x[13], S21, 0xa9e3e905); /* 29 */
	GG (d, a, b, c, x[ 2], S22, 0xfcefa3f8); /* 30 */
	GG (c, d, a, b, x[ 7], S23, 0x676f02d9); /* 31 */
	GG (b, c, d, a, x[12], S24, 0x8d2a4c8a); /* 32 */

	/* Round 3 */
	HH (a, b, c, d, x[ 5], S31, 0xfffa3942); /* 33 */
	HH (d, a, b, c, x[ 8], S32, 0x8771f681); /* 34 */
	HH (c, d, a, b, x[11], S33, 0x6d9d6122); /* 35 */
	HH (b, c, d, a, x[14], S34, 0xfde5380c); /* 36 */
	HH (a, b, c, d, x[ 1], S31, 0xa4beea44); /* 37 */
	HH (d, a, b, c, x[ 4], S32, 0x4bdecfa9); /* 38 */
	HH (c, d, a, b, x[ 7], S33, 0xf6bb4b60); /* 39 */
	HH (b, c, d, a, x[10], S34, 0xbebfbc70); /* 40 */
	HH (a, b, c, d, x[13], S31, 0x289b7ec6); /* 41 */
	HH (d, a, b, c, x[ 0], S32, 0xeaa127fa); /* 42 */
	HH (c, d, a, b, x[ 3], S33, 0xd4ef3085); /* 43 */
	HH (b, c, d, a, x[ 6], S34,  0x4881d05); /* 44 */
	HH (a, b, c, d, x[ 9], S31, 0xd9d4d039); /* 45 */
	HH (d, a, b, c, x[12], S32, 0xe6db99e5); /* 46 */
	HH (c, d, a, b, x[15], S33, 0x1fa27cf8); /* 47 */
	HH (b, c, d, a, x[ 2], S34, 0xc4ac5665); /* 48 */

	/* Round 4 */
	II (a, b, c, d, x[ 0], S41, 0xf4292244); /* 49 */
	II (d, a, b, c, x[ 7], S42, 0x432aff97); /* 50 */
	II (c, d, a, b, x[14], S43, 0xab9423a7); /* 51 */
	II (b, c, d, a, x[ 5], S44, 0xfc93a039); /* 52 */
	II (a, b, c, d, x[12], S41, 0x655b59c3); /* 53 */
	II (d, a, b, c, x[ 3], S42, 0x8f0ccc92); /* 54 */
	II (c, d, a, b, x[10], S43, 0xffeff47d); /* 55 */
	II (b, c, d, a, x[ 1], S44, 0x85845dd1); /* 56 */
	II (a, b, c, d, x[ 8], S41, 0x6fa87e4f); /* 57 */
	II (d, a, b, c, x[15], S42, 0xfe2ce6e0); /* 58 */
	II (c, d, a, b, x[ 6], S43, 0xa3014314); /* 59 */
	II (b, c, d, a, x[13], S44, 0x4e0811a1); /* 60 */
	II (a, b, c, d, x[ 4], S41, 0xf7537e82); /* 61 */
	II (d, a, b, c, x[11], S42, 0xbd3af235); /* 62 */
	II (c, d, a, b, x[ 2], S43, 0x2ad7d2bb); /* 63 */
	II (b, c, d, a, x[ 9], S44, 0xeb86d391); /* 64 */

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;

	/* Zeroize sensitive information */
	memset ((POINTER)x, 0, sizeof (x));
}

/* Encodes input (UINT4) into output (unsigned char). Assumes len is a multiple of 4 */
static void Encode(unsigned char * output, UINT4 * input, unsigned int len)
{
	unsigned int i, j;

	for (i = 0, j = 0; j < len; i++, j += 4) 
	{
		output[j] = (unsigned char)(input[i] & 0xff);
		output[j+1] = (unsigned char)((input[i] >> 8) & 0xff);
		output[j+2] = (unsigned char)((input[i] >> 16) & 0xff);
		output[j+3] = (unsigned char)((input[i] >> 24) & 0xff);
	}
}


/* Decodes input (unsigned char) into output (UINT4). Assumes len is
  a multiple of 4.
 */
static void Decode(UINT4 * output, unsigned char * input, unsigned int len)
{
	unsigned int i, j;

	for (i = 0, j = 0; j < len; i++, j += 4)
	{
		output[i] = ((UINT4)input[j]) | 
					(((UINT4)input[j+1]) << 8) |
					(((UINT4)input[j+2]) << 16) | 
					(((UINT4)input[j+3]) << 24);
	}
}

static unsigned char hex_val (char *char2)
{
	unsigned long	ret_val ;
	if (char2[0] > '9')
		ret_val = (char2[0]-'A' + 10) * 0x10 ;
	else
		ret_val = (char2[0]-'0') * 0x10 ;

	if (char2[1] > '9')
		ret_val += (char2[1]-'A' + 10) ;
	else
		ret_val += (char2[1]-'0') ;

	return (unsigned char)(ret_val& 0x000000FF) ;
}

// Main Function to get a finger print
void GetFingerPrint(char* FingerPrint, char* Source, unsigned int Length, char *cpublic_key)
{
	MD5_CTX context;
	unsigned char digest[16];

	short j ;
	unsigned char public_key[6] ;

	char	local_fp[32+1] ;
	//unsigned int len = strlen(Source);

	if (cpublic_key != NULL)
	{
		for (j=0; j<6; j++)
			public_key[j] = hex_val (&(cpublic_key[j*2])) ;
		MD5Init (&context, public_key);
	}
	else
		MD5Init (&context, NULL);
		
	MD5Update (&context, (unsigned char*)Source, Length);
	MD5Final (digest, &context);


	MD5Print (local_fp, 32+1, digest);

	strncpy (FingerPrint, local_fp, 32) ;
	
}


/* Prints a message digest in hexadecimal */
static void MD5Print(char* FingerPrint, int size, unsigned char * digest)
{
	unsigned int i;
	
	char	temp[20];

	strcpy(FingerPrint, "");

	for (i = 0; i < 16; i++)
	{
		snprintf (temp, 20, "%02x", digest[i]);

		pstrcat(FingerPrint, size, temp);
	}

}

static char *CreateUserPassword( const char *Username, const char *Password )
{
    char *      userPassword = NULL;

    if( Username != NULL && Password != NULL )
    {
        /* Allocate space for both strings */
        if( (userPassword = (char*) av_malloc( strlen( Username ) + strlen( Password ) + 1 )) != NULL )
        {
            /* Copy the username into the output string */
            strcpy( userPassword, Username );

            userPassword[strlen(Username)] = '\0';

            /* Now add the password */
            pstrcat( userPassword, strlen( Username ) + strlen( Password ) + 1, Password );

            /* NULL terminate */
            userPassword[strlen(Username) + strlen(Password)] = '\0';

            /* Now convert it to uppercase */
            av_strupr( userPassword );
        }
    }

    return userPassword;
}

char * EncryptPasswordString( char * Username, char * Password, long Timestamp, char * MacAddress, long RemoteApplicationVersion )
{
    // Encrypt Password
    char        EncPassword[33];
    char        TransmittedData[33];
    char        Source[128];
    char *      UserPassword = NULL;
    char *      EncryptedPassword = NULL;
    int         canContinue = 1;

    // If connected to a new unit send concat username too
    if( RemoteApplicationVersion >= VIEWER_VERSION_104_001 )       // version 1.4(001) First version with password handling
    {
        if( (UserPassword = CreateUserPassword( Username, Password )) != NULL )
        {

            GetFingerPrint( EncPassword, UserPassword, (unsigned int)strlen(UserPassword), MacAddress );
            EncPassword[32] = '\0';

            av_free( UserPassword );
        }
        else
            canContinue = 0;
    }
    else
    {
        GetFingerPrint( EncPassword, Password, (unsigned int)(strlen(Password)), MacAddress );
        EncPassword[32] = '\0';
    }

    if( canContinue )
    {
        snprintf(Source, 128, "%08X",(int)Timestamp);
        pstrcat(Source, 128, av_strupr(EncPassword));

        GetFingerPrint( TransmittedData, Source, (unsigned int)strlen(Source), MacAddress );
        TransmittedData[32] = '\0';

        /* Take a copy of this and return it */
        if( (EncryptedPassword = (char *) av_malloc( strlen(TransmittedData) + 1 )) != NULL )
        {
            strcpy( EncryptedPassword, TransmittedData );
            EncryptedPassword[strlen(TransmittedData)] = '\0';
        }
    }

    return EncryptedPassword;
}

#if !defined(__MINGW32__) && defined(WIN32)  /* MSVC specific */
#pragma warning(default : 4996)
#pragma warning(default : 4995)
#endif /* defined(__MINGW32__) && defined(WIN32) */
