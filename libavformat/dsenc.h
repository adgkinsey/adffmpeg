// $HDR$
//$Log:  28929: EncryptionRoutines.h 
//
//   Rev 1.1    03/11/2003 11:15:00  SMallinson
// ValidateCheckSum function added to allow use with PC Playback and CDPlayer

//---------------------------------------------------------------------------

#ifndef __DM_ENCRYPTION_ROUTINES_H__
#define __DM_ENCRYPTION_ROUTINES_H__
//---------------------------------------------------------------------------

char * EncryptPasswordString( char * Username, char * Password, long Timestamp, char * MacAddress, long RemoteApplicationVersion );

#endif /* __DM_ENCRYPTION_ROUTINES_H__ */
 
