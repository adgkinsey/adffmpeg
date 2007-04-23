#include "DebugTrace.h"

/* Debug output - Same usage as MFC TRACE */
void TRACE(LPCSTR lpszFormat, ...) 
{ 
    va_list args;

    va_start( args, lpszFormat ); 

    char       szBuffer[MAX_TRACE_BUFFER_SIZE]; 

    int nBuf = vsprintf(szBuffer, lpszFormat, args); 

    if (nBuf > -1) 
        OutputDebugString(szBuffer); 
    else 
        OutputDebugString(TEXT("TRACE buffer overflow\n"));

    va_end(args);
}
