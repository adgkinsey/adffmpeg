/* DebugTrace.h - Provides an implementation of a trace routine that acts like the MFC
 TRACE macro (writes a debug string to the debugger's output window) */

#ifndef __DEBUG_TRACE_H__
#define __DEBUG_TRACE_H__

#include <windows.h>
#include <tchar.h>
#include <stdio.h>       
#include <stdarg.h>

#define MAX_TRACE_BUFFER_SIZE           512

void TRACE(LPCSTR lpszFormat, ...); 

#endif /* __DEBUG_TRACE_H__ */