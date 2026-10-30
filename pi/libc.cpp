//
//  libc.cpp - the few C library functions the Omega core needs, on top of Circle
//
//  Circle (built with STDLIB_SUPPORT=0) already provides memcpy, memset,
//  strlen, strcpy, strcat, strcmp, atoi, malloc, free ... (circle/util.h,
//  circle/alloc.h). Here: printf family, qsort, exit.
//
//  printf policy: before the emulator owns the display, output goes to the
//  screen (boot/error messages) and the serial port. Afterwards it is
//  discarded, unless the build defines OMEGA_SERIAL_LOG, in which case it is
//  sent to the serial port (115200 8N1, GPIO14/15). The core prints a lot
//  (every floppy step), so serial logging slows the emulation down.
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "kernel.h"
#include "pihost.h"
#include <circle/string.h>
#include <circle/startup.h>
#include <circle/util.h>
#include <stdarg.h>

extern "C" {
#include "libc/stdio.h"
#include "libc/stdlib.h"
}

static int s_bConsoleOnScreen = 1;

struct OMEGA_FILE { int dummy; };
static OMEGA_FILE s_Stdout;
FILE *stdout = &s_Stdout;
FILE *stderr = &s_Stdout;

void piSetConsoleOnScreen (int bOnScreen)
{
	s_bConsoleOnScreen = bOnScreen;
}

int piIsConsoleOnScreen (void)
{
	return s_bConsoleOnScreen;
}

static void ConsoleWrite (const char *pText, size_t nLength)
{
	CKernel *pKernel = CKernel::Get ();
	if (pKernel == 0 || nLength == 0)
	{
		return;
	}

	if (s_bConsoleOnScreen)
	{
		pKernel->GetScreen ()->Write (pText, nLength);
		pKernel->GetSerial ()->Write (pText, nLength);
	}
#ifdef OMEGA_SERIAL_LOG
	else
	{
		pKernel->GetSerial ()->Write (pText, nLength);
	}
#endif
}

int vsnprintf (char *pBuffer, size_t nSize, const char *pFormat, va_list Args)
{
	CString Text;
	Text.FormatV (pFormat, Args);

	size_t nLength = Text.GetLength ();
	if (pBuffer != 0 && nSize > 0)
	{
		size_t nCopy = nLength < nSize - 1 ? nLength : nSize - 1;
		memcpy (pBuffer, (const char *) Text, nCopy);
		pBuffer[nCopy] = '\0';
	}
	return (int) nLength;
}

int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...)
{
	va_list Args;
	va_start (Args, pFormat);
	int nResult = vsnprintf (pBuffer, nSize, pFormat, Args);
	va_end (Args);
	return nResult;
}

int sprintf (char *pBuffer, const char *pFormat, ...)
{
	va_list Args;
	va_start (Args, pFormat);
	int nResult = vsnprintf (pBuffer, 0x7FFFFFFF, pFormat, Args);
	va_end (Args);
	return nResult;
}

int printf (const char *pFormat, ...)
{
#ifndef OMEGA_SERIAL_LOG
	if (!s_bConsoleOnScreen)
	{
		return 0;	// don't spend time formatting text nobody will see
	}
#endif
	va_list Args;
	va_start (Args, pFormat);
	CString Text;
	Text.FormatV (pFormat, Args);
	va_end (Args);

	ConsoleWrite ((const char *) Text, Text.GetLength ());
	return (int) Text.GetLength ();
}

int puts (const char *pText)
{
	ConsoleWrite (pText, strlen (pText));
	ConsoleWrite ("\n", 1);
	return 1;
}

int putchar (int c)
{
	char ch = (char) c;
	ConsoleWrite (&ch, 1);
	return c;
}

int fflush (FILE *pFile)
{
	return 0;
}

int abs (int x)
{
	return x < 0 ? -x : x;
}

void exit (int nCode)
{
	halt ();
}

// Only used once at start-up by the Musashi disassembler: a simple insertion sort is enough
void qsort (void *pBase, size_t nCount, size_t nSize, int (*pCompare) (const void *, const void *))
{
	char *pArray = (char *) pBase;
	char Temp[256];
	if (nSize > sizeof Temp)
	{
		return;
	}

	for (size_t i = 1; i < nCount; i++)
	{
		memcpy (Temp, pArray + i * nSize, nSize);
		size_t j = i;
		while (j > 0 && pCompare (pArray + (j - 1) * nSize, Temp) > 0)
		{
			memcpy (pArray + j * nSize, pArray + (j - 1) * nSize, nSize);
			j--;
		}
		memcpy (pArray + j * nSize, Temp, nSize);
	}
}
