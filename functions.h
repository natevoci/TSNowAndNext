/**
 *	TSNowAndNext, a tool for creating a projectX .Xcl file based on
 *	changes in the Now and Next information in the transport stream.
 *	Copyright (C) 2011 Nate
 *
 *	Nate can be reached on the forums at <http://forums.dvbowners.com/>.
 */

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

char* sprintToString(BYTE* pCurr, int count);

void Mymemcpy(void *dest, const void*src, size_t length);
void MyReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);

LONGLONG setFilePointer(HANDLE file, LONGLONG llDistanceToMove, DWORD dwMoveMethod);
LONGLONG getFileSize(HANDLE file);

#endif
