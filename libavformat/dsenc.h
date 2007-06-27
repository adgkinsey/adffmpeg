// $HDR$
//$Log:  28929: EncryptionRoutines.h 
//
//   Rev 1.1    03/11/2003 11:15:00  SMallinson
// ValidateCheckSum function added to allow use with PC Playback and CDPlayer

//---------------------------------------------------------------------------

#ifndef __DM_ENCRYPTION_ROUTINES_H__
#define __DM_ENCRYPTION_ROUTINES_H__
//---------------------------------------------------------------------------
/* Constants for MD5Transform routine.
 */
#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21



/* POINTER defines a generic pointer type */
typedef unsigned char *POINTER;

/* UINT2 defines a two byte word */
typedef unsigned short int UINT2;

/* UINT4 defines a four byte word */
typedef unsigned long int UINT4;


/* MD5 context. */
typedef struct 
{
  UINT4 state[4];                   /* state (ABCD) */
  UINT4 count[2];					/* number of bits, modulo 2^64 (lsb first) */
  unsigned char buffer[64];         /* input buffer */
} MD5_CTX;


/* Length of test block, number of test blocks.
 */
#define TEST_BLOCK_LEN 1000
#define TEST_BLOCK_COUNT 1000



// Macro Definitions


/* F, G, H and I are basic MD5 functions.
 */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

/* ROTATE_LEFT rotates x left n bits.
 */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

/* FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4.
Rotation is separate from addition to prevent recomputation.
 */
#define FF(a, b, c, d, x, s, ac) { \
 (a) += F ((b), (c), (d)) + (x) + (UINT4)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }
#define GG(a, b, c, d, x, s, ac) { \
 (a) += G ((b), (c), (d)) + (x) + (UINT4)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }
#define HH(a, b, c, d, x, s, ac) { \
 (a) += H ((b), (c), (d)) + (x) + (UINT4)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }
#define II(a, b, c, d, x, s, ac) { \
 (a) += I ((b), (c), (d)) + (x) + (UINT4)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }

#define VIEWER_VERSION_104_001                      0x00104001

char * EncryptPasswordString( char * Username, char * Password, long Timestamp, char * MacAddress, long RemoteApplicationVersion );
void GetFingerPrint(char* FingerPrint, char* Source, unsigned int Length, char *cpublic_key);

#endif /* __DM_ENCRYPTION_ROUTINES_H__ */
 
