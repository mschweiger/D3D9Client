// =================================================================================================================================
// The MIT Lisence:
//
// Copyright (C) 2012-2016 Jarmo Nikkanen
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation 
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, 
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software 
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// =================================================================================================================================

#include <stdio.h>
#include <queue>
#include <string>


#ifndef __LOGGING_H
#define __LOGGING_H

typedef struct {
	double time;
	double count;
	double peak;
} D3D9Time;

extern FILE *d3d9client_log;
extern int uEnableLog;
extern int iEnableLog;
extern int EnableLogStack[16];
extern __int64 qpcRef;
extern __int64 qpcFrq;
extern __int64 qpcStart;
extern std::queue<std::string> D3D9DebugQueue;

#define _PUSHLOG EnableLogStack[iEnableLog++] = uEnableLog;
#define _SETLOG(x) { EnableLogStack[iEnableLog++] = uEnableLog; if (uEnableLog>0) uEnableLog=x; } 
#define _POPLOG  uEnableLog = EnableLogStack[--iEnableLog];

#define _UNDEBUGED LogWrn("[Undebuged/Unfinished code section reached in %s (File %s, Line %d)]",__FUNCTION__,__FILE__,__LINE__);
//#define _UNDEBUGED

void   D3D9DebugLog(const char *format, ...);
void   D3D9InitLog(char *file);
void   D3D9CloseLog();
void   LogTrace(const char *format, ...);
void   LogErr(const char *format, ...);
void   LogWrn(const char *format, ...);
void   LogOk (const char *format, ...);
void   LogBlu(const char *format, ...);
void   LogOapi(const char *format, ...);
void   LogAlw(const char *format, ...);
void   LogDbg(const char *color, const char *format, ...);

double	D3D9GetTime();
void	D3D9SetTime(D3D9Time &inout, double ref);

#endif