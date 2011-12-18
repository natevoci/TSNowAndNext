/**
 *	TSNowAndNext, a tool for creating a projectX .Xcl file based on
 *	changes in the Now and Next information in the transport stream.
 *	Copyright (C) 2011 Nate
 *
 *	Nate can be reached on the forums at <http://forums.dvbowners.com/>.
 */

#include "stdafx.h"
#include "functions.h"
#include "stdlib.h"


char* sprintToString(BYTE* pCurr, int count)
{
	DWORD stringLength = count * 8;
	char* result = (char*)malloc(stringLength);
	memset(result, 0, stringLength);

	char text[20];
	memset(&text, 0, 20);

	int i = 0;
	for (; i < count; i++)
	{
		BYTE chr = pCurr[i];
		text[i%16] = (chr >= 32) ? chr : 32;

		if ((i % 16) == 0)
		{
			sprintf(result, "%s    %.2x", result, chr);
		}
		else if ((i % 16) == 15)
		{
			sprintf(result, "%s %.2x   %s\n", result, chr, &text);
			memset(&text, 0, 20);
		}
		else if ((i % 4) == 0)
		{
			sprintf(result, "%s  %.2x", result, chr);
		}
		else
		{
			sprintf(result, "%s %.2x", result, chr);
		}
	}

	i = (i % 16);
	if (i == 0)
		return result;

	for (; i < 16 ; i++)
	{
		text[i%16] = 32;
		if ((i % 4) == 0)
		{
			sprintf(result, "%s    ", result);
		}
		else
		{
			sprintf(result, "%s   ", result);
		}
	}
	sprintf(result, "%s   %s\n", result, &text);

	return result;
}

void Mymemcpy(void *dest, const void*src, size_t length)
{
	memcpy(dest, src, length);
}

void MyReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
	ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

LONGLONG setFilePointer(HANDLE file, LONGLONG llDistanceToMove, DWORD dwMoveMethod)
{
	LARGE_INTEGER li;
	li.QuadPart = llDistanceToMove;
	li.LowPart = SetFilePointer(file, li.LowPart, &li.HighPart, dwMoveMethod);
	if (li.LowPart == (DWORD)-1)
		return -1LL;
	li.QuadPart;
}

LONGLONG getFileSize(HANDLE file)
{
	LARGE_INTEGER li;
	li.LowPart = GetFileSize(file, (LPDWORD)&li.HighPart);
	return li.QuadPart;
}
