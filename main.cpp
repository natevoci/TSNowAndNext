/**
 *	TSNowAndNext, a tool for creating a projectX .Xcl file based on
 *	changes in the Now and Next information in the transport stream.
 *	Copyright (C) 2011 Nate
 *
 *	Nate can be reached on the forums at <http://forums.dvbowners.com/>.
 */

#include "stdafx.h"
#include "transport_packet.h"
#include "functions.h"
#include "stdlib.h"

void PrintPackets(HANDLE file, LONGLONG fileOffset, LONGLONG fileLength);
void ProcessNowAndNext(HANDLE file, char* filename);
void ListNowAndNext(HANDLE file);

transport_packet* ReadPacket(HANDLE file, LONGLONG fileOffset, LONGLONG fileLength, SHORT pid);
transport_packet* ReadSection(HANDLE file, LONGLONG fileOffset, LONGLONG fileLength, SHORT pid, SHORT table_id);
transport_packet* ReadEITSection(HANDLE file, LONGLONG fileOffset, LONGLONG fileLength, SHORT service_id, SHORT section_number = -1);

void WriteXclFile(char* filename, LONGLONG start, LONGLONG end);

int ParseRawTS(PBYTE pCurr, int length, LONGLONG filePos);
BOOL ProcessTSPacket(transport_packet* tsHeader, LONGLONG currPos);

void sprintfGeneralInfo(LPSTR line, transport_packet *tsHeader);

DWORD sizeErrors = 0, badPackets = 0, continuityErrors = 0;
LONGLONG _last47Pos = 0;
WORD _lastSeq[0x2000];
const int READ_SIZE = 15040;

SHORT _patVersion = -1;
transport_packet* _patPacket;
transport_packet* _pmtPackets[0x2000];

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		printf("Usage: TSNowAndNextChanges.exe [-events|-packets] <filename.ts>\n");
		printf("   -events  : Lists the changes of event through the entire file\n");
		printf("   -packets : Prints details about significant packets in the file\n");
		printf(" If neither of these options are specified then\n");
		printf(" filename.ts.Xcl will be created.\n");
		return 0;
	}
	HANDLE f;

	BOOL printEvents = FALSE;
	BOOL printPackets = FALSE;
	for (int i = 1; i < argc - 1; i++)
	{
		if (_stricmp(argv[i], "-events") == 0)
			printEvents = TRUE;
		if (_stricmp(argv[i], "-packets") == 0)
			printPackets = TRUE;
	}
	char* filename = argv[argc - 1];
	
#ifdef _DEBUG
	//filename = "D:\\TV\\Samples\\TENSampleTsMuxEPGChange.ts";
	//printEvents = TRUE;
	//filename = "D:\\TV\\Samples\\TENSampleTsMuxEPGChangeOrig.ts";
#endif

	printf("TS file analyser - written by Nate\n\n");
	printf("opening %s\n\n", filename);

	f = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_ARCHIVE, NULL);

	if (f == INVALID_HANDLE_VALUE)
	{
		printf("Could not open file\n");
	}
	else
	{
		if (printEvents == TRUE)
		{
			ListNowAndNext(f);
		}
		else if (printPackets == TRUE)
		{
			PrintPackets(f, 0, 0x100000);
			printf("%i size errors\n%i packets with the error bit set\n%i continuity errors\n", sizeErrors, badPackets, continuityErrors);
		}
		else
		{
			ProcessNowAndNext(f, filename);
		}
		CloseHandle(f);
	}

#ifdef _DEBUG
	printf("Press enter to exit");
	getchar();
#endif

	return 0;
}

void PrintPackets(HANDLE file, LONGLONG fileOffset, LONGLONG fileLength)
{
	DWORD dw = setFilePointer(file, fileOffset, FILE_BEGIN);

	BYTE buff[READ_SIZE + 188];
	memset((void*)&_lastSeq, 0xff, 0x2000);

	BYTE *pStartFrame = (BYTE *)&buff;

	printf(" E  = Error indicator bit\n");
	printf(" U  = Payload unit start indicator bit\n");
	printf(" P  = Priority bit\n");
	printf(" SC = Scrambling Control\n");
	printf(" AF = Adaption field control\n");
	printf(" C  = Continuity counter\n");
	printf(" S  = Status (* = out of sync)\n");
	printf(" Offset       e s p PID  ts ad c    d r p P O s p af  PCR    OPCR  \n");
	printf("              r t r      sc fe o    i a r C P p r e\n");
	printf("                  i            n    s n i R C l i x\n");
	printf("                  o            t      d o   R i v t\n");
	printf("----------    - - - ---- -- -- -    - - - - - - - - ---------------\n");

	LONGLONG filePos = fileOffset;
	LONGLONG fileLengthLeft = fileLength;
	DWORD bytesRead;
	DWORD bytesToRead = (fileLengthLeft < READ_SIZE) ? (DWORD)fileLengthLeft : READ_SIZE;
	BOOL bResult = ReadFile(file, &buff, bytesToRead, &bytesRead, NULL);


	while ((bytesRead > 0))
	{
		BYTE *pCurr;
		pCurr = (BYTE *)&buff;

		int bytesleft = bytesToRead + (pStartFrame - pCurr);
		int bytesProcessed = ParseRawTS(pCurr, bytesToRead + (pStartFrame - pCurr), filePos);

		filePos += bytesRead;
		fileLengthLeft -= bytesRead;
		bytesToRead = (fileLengthLeft < READ_SIZE) ? (DWORD)fileLengthLeft : READ_SIZE;
		if (bytesToRead <= 0)
			return;

		bytesleft -= bytesProcessed;
		if (bytesleft < 0)
			bytesleft = 0;
		if (bytesleft > 0)
		{
			Mymemcpy((char*)&buff, pCurr + bytesProcessed, bytesleft);
		}
		pStartFrame = ((BYTE*)&buff)+bytesleft;
		MyReadFile(file, pStartFrame, bytesToRead, &bytesRead, NULL);

		if (bytesRead <= 0)
		{
			Sleep(1000);
			MyReadFile(file, pStartFrame, bytesToRead, &bytesRead, NULL);
		}
	}
	if (bytesRead <= 0)
		printf("End of file reached\n");
}

void ProcessNowAndNext(HANDLE file, char* filename)
{
	const float testPositionFilePercentage = 0.35f;

	transport_packet* patPacket = ReadSection(file, 0, 0x100000, 0x00, 0x00);
	transport_packet* pmtPacket = NULL;
	transport_packet* pcrPacket = NULL;
	transport_packet* eitPacket = NULL;
	pat_table* patTable = NULL;
	pmt_table* pmtTable = NULL;
	eit_table* eitTable = NULL;
	SHORT service_id;

	if (patPacket == NULL)
	{
		printf("No PAT found");
		return;
	}

	// Verify that PMT pids exist for the programs listed in the PAT
	patTable = ParsePATTable(patPacket);
	for (int i=0; i<patTable->programCount; i++)
	{
		pmtPacket = ReadSection(file, patPacket->FileOffset, 0x100000, patTable->programs[i].program_map_PID, 0x02);
		if (pmtPacket != NULL)
		{
			service_id = patTable->programs[i].program_number;
			// TODO: load PCR pid
			break;
		}
	}
	FreePATTable(patTable);
	if (pmtPacket == NULL)
	{
		printf("No PMT found");
		return;
	}

	pmtTable = ParsePMTTable(pmtPacket);

	LONGLONG PCR_base;
	pcrPacket = ReadPacket(file, 0, 0x20000, pmtTable->PCR_PID);
	if (pcrPacket->adaption_field.PCR_flag == 1)
	{
		LONGLONG PCR = pcrPacket->adaption_field.program_clock_reference_base * 300;
		PCR += pcrPacket->adaption_field.program_clock_reference_extention;
		PCR_base = PCR;

		double totalSeconds = (double)PCR / 300 / 90000;
		long hours = long(totalSeconds/3600);
		long minutes = long(totalSeconds/60) - hours*60;
		double seconds = totalSeconds - minutes*60 - hours*3600;

		printf("First PCR %.2u:%.2u:%.1u%f\n",
				hours, minutes, long(seconds)/10, seconds - double(long(seconds)/10)*10
				);
	}
	FreeTSPacket(pcrPacket);

	LONGLONG fileLength = getFileSize(file);
	LONGLONG fileOffsetStart;
	LONGLONG fileOffsetEnd;
	DWORD readSize = 0x2000000;

	printf("File Length %13llu\n", fileLength);

	LONGLONG testPositionFileOffset = (LONGLONG)(fileLength * testPositionFilePercentage);
	eitPacket = ReadEITSection(file, testPositionFileOffset, readSize, service_id, 0);
	if (eitPacket == NULL)
	{
		printf("No EIT within 0x%.8x bytes\n", readSize);
		return;
	}
	FreeEITTable(eitTable);
	eitTable = ParseEITTable(eitPacket);
	eit_event_list* event_list = ParseEITEvents(eitTable);

	WORD event_id_of_show = event_list->list[0].event_id;

	float percentage = eitPacket->FileOffset / (float)fileLength * 100.0f;
	printf("Sample      %13llu %6.2f%% name=%.40s\n", eitPacket->FileOffset, percentage, event_list->list[0].short_event_descriptor->event_name);

	// Find Show Start
	LONGLONG fileOffsetMin = 0;
	LONGLONG fileOffsetMax = testPositionFileOffset;

	while (TRUE)
	{
		fileOffsetStart = (fileOffsetMin + fileOffsetMax) / 2;

		FreeTSPacket(eitPacket);
		eitPacket = ReadEITSection(file, fileOffsetStart, readSize, service_id, 0);
		FreeEITTable(eitTable);
		eitTable = ParseEITTable(eitPacket);
		FreeEITEventList(event_list);
		event_list = ParseEITEvents(eitTable);

		if (eitPacket->FileOffset == fileOffsetMax)
		{
			fileOffsetStart = fileOffsetMin;
			FreeTSPacket(eitPacket);
			eitPacket = ReadEITSection(file, fileOffsetStart, readSize, service_id, 0);
			if (eitPacket->FileOffset != fileOffsetStart)
			{
				FreeTSPacket(eitPacket);
				eitPacket = NULL;
			}
			FreeEITTable(eitTable);
			eitTable = ParseEITTable(eitPacket);
			FreeEITEventList(event_list);
			event_list = ParseEITEvents(eitTable);
			break;
		}

		if (event_list->list[0].event_id == event_id_of_show)
			fileOffsetMax = eitPacket->FileOffset;
		else
			fileOffsetMin = eitPacket->FileOffset;
	}

	percentage = fileOffsetStart / (float)fileLength * 100.0f;
	if (eitTable != NULL)
	{
		printf("Show Start  %13llu %6.2f%% name=%.40s\n", fileOffsetStart, percentage, event_list->list[0].short_event_descriptor->event_name);
	}
	else
	{
		printf("Show Start  %13llu %6.2f%% Start Of File\n", fileOffsetStart, percentage);
	}

	// Find Show End
	fileOffsetMin = testPositionFileOffset;
	fileOffsetMax = fileLength + 1;

	while (TRUE)
	{
		fileOffsetEnd = (fileOffsetMin + fileOffsetMax) / 2;

		FreeTSPacket(eitPacket);
		eitPacket = ReadEITSection(file, fileOffsetEnd, readSize, service_id, 0);
		FreeEITTable(eitTable);
		eitTable = ParseEITTable(eitPacket);
		FreeEITEventList(event_list);
		event_list = ParseEITEvents(eitTable);

		if (eitTable == NULL)
		{
			fileOffsetEnd = fileLength;
			break;
		}

		if ((eitPacket->FileOffset == fileOffsetMin) ||
			(eitPacket->FileOffset == fileOffsetMax))
			break;

		if (event_list->list[0].event_id == event_id_of_show)
			fileOffsetMin = eitPacket->FileOffset;
		else
			fileOffsetMax = eitPacket->FileOffset;
	}

	percentage = fileOffsetEnd / (float)fileLength * 100.0f;
	if (eitTable != NULL)
	{
		printf("Show End    %13llu %6.2f%% name=%.40s\n", fileOffsetEnd, percentage, event_list->list[0].short_event_descriptor->event_name);
	}
	else
	{
		printf("Show End    %13llu %6.2f%% End Of File\n", fileOffsetEnd, percentage);
	}

	//Write the Xcl file
	char* xclFilename = (char*)malloc(strlen(filename) + 5); // 5 = ".Xcl\0"
	sprintf(xclFilename, "%s.Xcl", filename);
	WriteXclFile(xclFilename, fileOffsetStart, fileOffsetEnd);
	free(xclFilename);
}

void ListNowAndNext(HANDLE file)
{
	transport_packet* patPacket = ReadSection(file, 0, 0x10000, 0x00, 0x00);
	transport_packet* pmtPacket = NULL;
	transport_packet* pcrPacket = NULL;
	transport_packet* eitPacket = NULL;
	pat_table* patTable = NULL;
	pmt_table* pmtTable = NULL;
	eit_table* eitTable = NULL;
	eit_event_list* event_list = NULL;
	SHORT service_id;

	if (patPacket == NULL)
	{
		printf("No PAT found");
		return;
	}

	// Verify that PMT pids exist for the programs listed in the PAT
	patTable = ParsePATTable(patPacket);
	for (int i=0; i<patTable->programCount; i++)
	{
		pmtPacket = ReadSection(file, patPacket->FileOffset, 0x20000, patTable->programs[i].program_map_PID, 0x02);
		if (pmtPacket != NULL)
		{
			service_id = patTable->programs[i].program_number;
			break;
		}
	}
	FreePATTable(patTable);
	if (pmtPacket == NULL)
	{
		printf("No PMT found");
		return;
	}

	pmtTable = ParsePMTTable(pmtPacket);

	LONGLONG PCR_base;
	pcrPacket = ReadPacket(file, 0, 0x20000, pmtTable->PCR_PID);
	if (pcrPacket->adaption_field.PCR_flag == 1)
	{
		LONGLONG PCR = pcrPacket->adaption_field.program_clock_reference_base * 300;
		PCR += pcrPacket->adaption_field.program_clock_reference_extention;
		PCR_base = PCR;

		double totalSeconds = (double)PCR / 300 / 90000;
		long hours = long(totalSeconds/3600);
		long minutes = long(totalSeconds/60) - hours*60;
		double seconds = totalSeconds - minutes*60 - hours*3600;

		printf("First PCR %.2u:%.2u:%.1u%f\n\n",
				hours, minutes, long(seconds)/10, seconds - double(long(seconds)/10)*10
				);
	}
	FreeTSPacket(pcrPacket);

	WORD event_id_now = -1;
	WORD event_id_next = -1;

	LONGLONG fileLength = getFileSize(file);
	DWORD readSize = 0x2000000;
	
	LONGLONG fileOffset = patPacket->FileOffset;

	while (fileOffset < fileLength)
	{
		FreeTSPacket(eitPacket);
		eitPacket = ReadEITSection(file, fileOffset, readSize, service_id);

		if (eitPacket == NULL)
		{
			printf("No EIT within 0x%.8x bytes\n", readSize);
			fileOffset += readSize - 188;
			continue;
		}

		FreeEITTable(eitTable);
		eitTable = ParseEITTable(eitPacket);
		FreeEITEventList(event_list);
		event_list = ParseEITEvents(eitTable);

		BOOL print = FALSE;
		if (eitTable->section_number == 0) // Now
		{
			if (event_id_now != event_list->list[0].event_id)
			{
				printf("NOW CHANGED\n");
				event_id_now = event_list->list[0].event_id;
				print = TRUE;
			}
		}
		else if (eitTable->section_number == 1) // Next
		{
			if (event_id_next != event_list->list[0].event_id)
			{
				printf("NEXT CHANGED\n");
				event_id_next = event_list->list[0].event_id;
				print = TRUE;
			}
		}

		if (print)
		{
			char line[1024];
			sprintfGeneralInfo((char*)&line, eitPacket);
			sprintf((char*)&line, "%s EIT 0x%.2x %i 0x%.4x %.2i %i %i %i %.4x %.4x %i %.2x", line,
				eitTable->table_id,
				eitTable->section_length,
				eitTable->service_id,
				eitTable->version_number,
				eitTable->current_next_indicator,
				eitTable->section_number,
				eitTable->last_section_number,
				eitTable->transport_stream_id,
				eitTable->original_network_id,
				eitTable->segment_last_section_number,
				eitTable->last_table_id);
			printf("%s\n", line);

			pcrPacket = ReadPacket(file, eitPacket->FileOffset, 0x20000, pmtTable->PCR_PID);
			if (pcrPacket->adaption_field.PCR_flag == 1)
			{
				LONGLONG PCR = pcrPacket->adaption_field.program_clock_reference_base * 300;
				PCR += pcrPacket->adaption_field.program_clock_reference_extention;
				PCR -= PCR_base;

				double totalSeconds = (double)PCR / 300 / 90000;
				long hours = long(totalSeconds/3600);
				long minutes = long(totalSeconds/60) - hours*60;
				double seconds = totalSeconds - minutes*60 - hours*3600;

				char line[1024];
				sprintf((char*)&line, "PCR %.2u:%.2u:%.1u%f",
						hours, minutes, long(seconds)/10, seconds - double(long(seconds)/10)*10
						);
				printf("%s\n", &line);
			}

			eit_event_list* eventList = ParseEITEvents(eitTable);

			for (int i=0; i<eventList->count; i++)
			{
				eit_event* ev = &eventList->list[i];

				char szLocalDate[255];
				char szLocalTime[255];
				FILETIME localFileTime;
				SYSTEMTIME localSystemTime;

				FileTimeToLocalFileTime(&ev->start_time_utc, &localFileTime);
				FileTimeToSystemTime(&localFileTime, &localSystemTime);
				GetDateFormat(LOCALE_USER_DEFAULT, 0, &localSystemTime, NULL, szLocalDate, 255);
				GetTimeFormat(LOCALE_USER_DEFAULT, 0, &localSystemTime, NULL, szLocalTime, 255);

				int durationHours = (ev->duration_seconds / 3600);
				int durationMinutes = (ev->duration_seconds % 3600) / 60;
				int durationSeconds = (ev->duration_seconds % 60);

				printf("0x%.4x %s %s %i:%.2i:%.2i %i %i\n",
					ev->event_id,
					szLocalDate, szLocalTime,
					durationHours,
					durationMinutes,
					durationSeconds,
					ev->running_status,
					ev->free_CA_mode);
				if (ev->short_event_descriptor != NULL)
				{
					printf("%s\n%s\n\n",
						ev->short_event_descriptor->event_name,
						ev->short_event_descriptor->text);
				}
			}

			FreeEITEventList(eventList);
		}

		fileOffset = eitPacket->FileOffset + 188;
	}
	FreeEITTable(eitTable);

	printf("File Length: 0x%.8x\n", fileLength);

}

transport_packet* ReadPacket(HANDLE file, LONGLONG fileOffset, LONGLONG fileLength, SHORT pid)
{
	transport_packet* result = NULL;

	DWORD dw = setFilePointer(file, fileOffset, FILE_BEGIN);
	
	BYTE buff[READ_SIZE + 188];

	LONGLONG currentPos = fileOffset;
	LONGLONG endPos = fileOffset + fileLength;
	LONGLONG bufferFileOffset = fileOffset;
	DWORD bufferByteCount = 0;

	transport_packet_header* tsHeader = NULL;

	while ((currentPos + 188) < endPos)
	{
		// See if we need to load more buffer from the file
		DWORD bufferCurrentPos = DWORD(currentPos - bufferFileOffset);
		if ((bufferCurrentPos + 188) > bufferByteCount)
		{
			DWORD unusedBufferByteCount = bufferByteCount - bufferCurrentPos;

			if (unusedBufferByteCount < 0)
				unusedBufferByteCount = 0;
			if (unusedBufferByteCount > 0)
			{
				Mymemcpy((BYTE *)&buff, (BYTE *)&buff + bufferCurrentPos, unusedBufferByteCount);
			}

			BYTE *pReadIntoBufferHere = (BYTE *)&buff + unusedBufferByteCount;

			DWORD bytesToRead = (DWORD)(endPos - currentPos);
			if (bytesToRead > READ_SIZE)
				bytesToRead = READ_SIZE;
			if (bytesToRead <= 0)
				break;

			DWORD bytesRead = 0;
			MyReadFile(file, pReadIntoBufferHere, bytesToRead, &bytesRead, NULL);

			if (bytesRead <= 0)
			{
				Sleep(1000);
				MyReadFile(file, pReadIntoBufferHere, bytesToRead, &bytesRead, NULL);
				if (bytesRead <= 0)
					break;
			}

			bufferFileOffset += bufferCurrentPos;
			bufferByteCount = unusedBufferByteCount + bytesRead;
			continue;
		}

		BYTE *pCurr = (BYTE *)&buff + (currentPos - bufferFileOffset);
		FreeTSPacketHeader(tsHeader);
		tsHeader = ParseTSPacketHeader(pCurr, currentPos);

		if ((tsHeader->sync_byte != 0x47) || (tsHeader->transport_error_indicator == TRUE))
		{
			currentPos++;
			continue;
		}

		if ((tsHeader->PID == pid) || (pid == -1))
		{
			result = ParseTSPacketFull(pCurr, 188, tsHeader->FileOffset);
			break;
		}

		currentPos += 188;
	}

	return result;
}

transport_packet* ReadSection(HANDLE file, LONGLONG fileOffset, LONGLONG fileLength, SHORT pid, SHORT table_id)
{
	transport_packet* result = NULL;

	DWORD dw = setFilePointer(file, fileOffset, FILE_BEGIN);

	BYTE buff[READ_SIZE + 188];

	LONGLONG currentPos = fileOffset;
	LONGLONG endPos = fileOffset + fileLength;
	LONGLONG bufferFileOffset = fileOffset;
	DWORD bufferByteCount = 0;

	SHORT sectionLength = 0;
	SHORT sectionLengthLeft = 0;
	BYTE* pSectionData = (BYTE*)malloc(4096);
	SHORT sectionDataOffset = 0;

	transport_packet_header* tsHeader = NULL;
	
	LONGLONG sectionFileOffset = 0;

	while ((currentPos + 188) < endPos)
	{
		// See if we need to load more buffer from the file
		DWORD bufferCurrentPos = DWORD(currentPos - bufferFileOffset);
		if ((bufferCurrentPos + 188) > bufferByteCount)
		{
			DWORD unusedBufferByteCount = bufferByteCount - bufferCurrentPos;

			if (unusedBufferByteCount < 0)
				unusedBufferByteCount = 0;
			if (unusedBufferByteCount > 0)
			{
				Mymemcpy((BYTE *)&buff, (BYTE *)&buff + bufferCurrentPos, unusedBufferByteCount);
			}

			BYTE *pReadIntoBufferHere = (BYTE *)&buff + unusedBufferByteCount;

			DWORD bytesToRead = (DWORD)(endPos - currentPos);
			if (bytesToRead > READ_SIZE)
				bytesToRead = READ_SIZE;
			if (bytesToRead <= 0)
				break;

			DWORD bytesRead = 0;
			MyReadFile(file, pReadIntoBufferHere, bytesToRead, &bytesRead, NULL);

			if (bytesRead <= 0)
			{
				Sleep(1000);
				MyReadFile(file, pReadIntoBufferHere, bytesToRead, &bytesRead, NULL);
				if (bytesRead <= 0)
					break;
			}

			bufferFileOffset += bufferCurrentPos;
			bufferByteCount = unusedBufferByteCount + bytesRead;
			continue;
		}

		BYTE *pCurr = (BYTE *)&buff + (currentPos - bufferFileOffset);
		FreeTSPacketHeader(tsHeader);
		tsHeader = ParseTSPacketHeader(pCurr, currentPos);

		if ((tsHeader->sync_byte != 0x47) || (tsHeader->transport_error_indicator == TRUE))
		{
			currentPos++;
			continue;
		}

		if ((tsHeader->PID == pid) || (pid == -1))
		{
			if (sectionLengthLeft > 0)
			{
				SHORT byteCount = 188 - tsHeader->payload_offset;
				memcpy(pSectionData + sectionDataOffset, pCurr + tsHeader->payload_offset, byteCount);

				sectionDataOffset += byteCount;
				sectionLengthLeft -= byteCount;
			}

			if ((tsHeader->payload_unit_start_indicator == 1) &&
				(tsHeader->IsPES == FALSE) &&
				(tsHeader->table.table_id == table_id))
			{
				memset(pSectionData, 0, 4096);

				memcpy(pSectionData, pCurr, 188);
				char* sectionText = sprintToString(pSectionData, 188);
				free(sectionText);

				sectionLength = tsHeader->table.section_length_including_header;
				sectionLengthLeft = sectionLength - 188;
				sectionDataOffset = 188;

				sectionFileOffset = tsHeader->FileOffset;
				pid = tsHeader->PID;
			}

			if ((sectionLengthLeft <= 0) && (sectionDataOffset > 0))
			{
				result = ParseTSPacketFull(pSectionData, sectionLength, sectionFileOffset);
				break;
			}
		}

		currentPos += 188;
	}

	free(pSectionData);

	return result;
}

transport_packet* ReadEITSection(HANDLE file, LONGLONG fileOffset, LONGLONG fileLength, SHORT service_id, SHORT section_number /*= -1*/)
{
	transport_packet* eitSection = NULL;
	LONGLONG currOffset = fileOffset;

	while (TRUE)
	{
		FreeTSPacket(eitSection);
		eitSection = ReadSection(file, currOffset, fileLength - (currOffset - fileOffset), -1, 0x4E);
		
		if (eitSection == NULL)
			break;
		if (eitSection->table.id_after_length == service_id)
		{
			if (section_number == -1)
				return eitSection;
			eit_table* eitTable = ParseEITTable(eitSection);
			BYTE sectionNum = eitTable->section_number;
			FreeEITTable(eitTable);
			if (section_number == sectionNum)
				return eitSection;
		}

		currOffset = eitSection->FileOffset + 188;
	}
	return NULL;
}

void WriteXclFile(char* filename, LONGLONG start, LONGLONG end)
{
	printf("writing %s\n", filename);

	HANDLE file = CreateFile(filename, GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		printf("Failed to create Xcl file");
		ExitProcess(-5);
		return;
	}

	char text[256];
	sprintf((char*)&text, "CollectionPanel.CutMode=0\r\n%llu\r\n%llu\r\n", start, end);

	DWORD written = 0;
	WriteFile(file, &text, strlen((char*)&text), &written, NULL);

	CloseHandle(file);
}


int ParseRawTS(PBYTE pCurr, int length, LONGLONG filePos)
{
	transport_packet* tsHeader = NULL;

	BYTE *pStart = pCurr;
	while ((pCurr - pStart) <= length - 188)
	{
		LONGLONG currFilePos = filePos + (pCurr - pStart);

		FreeTSPacket(tsHeader);
		tsHeader = ParseTSPacketFull(pCurr, 188, currFilePos);

		if (tsHeader->sync_byte != 0x47)
		{
			pCurr++;
			continue;
		}

		BOOL validPacket = ProcessTSPacket(tsHeader, currFilePos);
		if (validPacket == FALSE)
		{
			pCurr++;
			continue;
		}

		pCurr += 188;
		_lastSeq[tsHeader->PID] = tsHeader->continuity_counter;
		_last47Pos = currFilePos;
	}
	return (pCurr - pStart);
}

BOOL ProcessTSPacket(transport_packet* tsHeader, LONGLONG currPos)
{
	char line[1024];

	sprintfGeneralInfo((char*)&line, tsHeader);

	if (tsHeader->HasAdaptionField)
	{
		if (tsHeader->adaption_field.adaption_field_length > 0)
		{
					
			sprintf((char*)&line, "%s AF %i %i %i %i %i %i %i %i", line,
					tsHeader->adaption_field.discontinuity_indicator,
					tsHeader->adaption_field.random_access_indicator,
					tsHeader->adaption_field.elementary_stream_priority_indicator,
					tsHeader->adaption_field.PCR_flag,
					tsHeader->adaption_field.OPCR_flag,
					tsHeader->adaption_field.splicing_point_flag,
					tsHeader->adaption_field.transport_private_data_flag,
					tsHeader->adaption_field.adaption_field_entention_flag);

			if (tsHeader->adaption_field.PCR_flag == 1)
			{
				LONGLONG PCR = tsHeader->adaption_field.program_clock_reference_base * 300;
				PCR += tsHeader->adaption_field.program_clock_reference_extention;

				double totalSeconds = (double)tsHeader->adaption_field.program_clock_reference_base / 90000;
				long hours = long(totalSeconds/3600);
				long minutes = long(totalSeconds/60) - hours*60;
				double seconds = totalSeconds - minutes*60 - hours*3600;

				sprintf((char*)&line, "%s %.2u:%.2u:%.1u%f", line,
						hours, minutes, long(seconds)/10, seconds - double(long(seconds)/10)*10
						);
			}
			if (tsHeader->adaption_field.OPCR_flag == 1)
			{
				sprintf((char*)&line, "%s  %u%u %u %u", line,
						LONG(tsHeader->adaption_field.original_program_clock_reference_base / 1000000000),
						LONG(tsHeader->adaption_field.original_program_clock_reference_base % 1000000000),
						tsHeader->adaption_field.original_program_clock_reference_reserved,
						tsHeader->adaption_field.original_program_clock_reference_extention
						);
			}
		}
	}


	if (tsHeader->transport_error_indicator)
	{
		sprintf((char*)&line, "%s  error bit set.", line);
		badPackets++;
		return FALSE;
	}
	else
	{
		if ((currPos - _last47Pos != 188) && (currPos != 0))
		{
			sprintf((char*)&line, "%s  bad packet size.", line);
			sizeErrors++;
		}

		BYTE nextSeq = (_lastSeq[tsHeader->PID]+1) & 0x0F;
		if ((tsHeader->PID != 0x1fff) && (_lastSeq[tsHeader->PID] != 0xffff) && (nextSeq != tsHeader->continuity_counter) && (tsHeader->adaption_field_control < 0x02))
		{
			sprintf((char*)&line, "%s  *", line);
			continuityErrors++;
		}
	}

	if (tsHeader->adaption_field.adaption_field_length > 0)
	{
		//return TRUE;
		printf("%s\n", (char*)&line);
	}

	//if (tsHeader->payload_unit_start_indicator)
	//if ((tsHeader->PID == 0x503) && (tsHeader->PID <= 0x1000))
	//if (tsHeader->PID == 0x12)
	{
	}

	if (tsHeader->payload_unit_start_indicator == 1)
	{
		//printf("%s ", (char*)&line);

		if (tsHeader->IsPES == FALSE)
		{
			//switch (tsHeader->table.table_id)
			//{
			//case 0x00:
			//	if (_patVersion == tsHeader->pat_table.version_number)
			//	{
			//		return TRUE;
			//		sprintf((char*)&line, "%s PAT Unchanged", line);
			//	}
			//	else
			//	{
			//		_patVersion = tsHeader->pat_table.version_number;
			//		FreeTSPacket(_patPacket);
			//		_patPacket = tsHeader;
			//		_patPacket->ref_count++;

			//		sprintf((char*)&line, "%s PAT %i 0x%.4x %.2i %i %i %i 0x%.4x", line,
			//			tsHeader->pat_table.section_length,
			//			tsHeader->pat_table.transport_stream_id,
			//			tsHeader->pat_table.version_number,
			//			tsHeader->pat_table.current_next_indicator,
			//			tsHeader->pat_table.section_number,
			//			tsHeader->pat_table.last_section_number,
			//			tsHeader->pat_table.network_PID);
			//		for (int i=0; i<tsHeader->pat_table.programCount; i++)
			//		{
			//			sprintf((char*)&line, "%s\n program_number=0x%.4x  PMT_PID=0x%.4x", line,
			//				tsHeader->pat_table.programs[i].program_number,
			//				tsHeader->pat_table.programs[i].program_map_PID);
			//		}
			//	}
			//	break;
			//case 0x01:
			//	return TRUE;
			//	sprintf((char*)&line, "%s CAT", line);
			//	break;
			//case 0x02:
			//	//return TRUE;
			//	sprintf((char*)&line, "%s PMT", line);
			//	break;
			//case 0x4e:
			//	sprintf((char*)&line, "%s EIT 0x%.2x %i 0x%.4x %.2i %i %i %i %.4x %.4x %i %.2x", line,
			//		tsHeader->eit_table.table_id,
			//		tsHeader->eit_table.section_length,
			//		tsHeader->eit_table.service_id,
			//		tsHeader->eit_table.version_number,
			//		tsHeader->eit_table.current_next_indicator,
			//		tsHeader->eit_table.section_number,
			//		tsHeader->eit_table.last_section_number,
			//		tsHeader->eit_table.transport_stream_id,
			//		tsHeader->eit_table.original_network_id,
			//		tsHeader->eit_table.segment_last_section_number,
			//		tsHeader->eit_table.last_table_id);
			//	break;
			//default:
			//	sprintf((char*)&line, "%s table_id=0x%.2x", line,
			//		tsHeader->table.table_id);
			//	break;
			//}
		}
		else
		{
			return TRUE;
			//sprintf((char*)&line, "%s PES %.2x %.2x %.2x %.2x %.2x %.2x", line,
			//	tsHeader->payload[0],
			//	tsHeader->payload[1],
			//	tsHeader->payload[2],
			//	tsHeader->payload[3],
			//	tsHeader->payload[4],
			//	tsHeader->payload[5]
			//	);
		}

		printf("%s\n", line);
		return TRUE;
	}
	
	return TRUE;
}

void sprintfGeneralInfo(LPSTR line, transport_packet *tsHeader)
{
	sprintf(line, "0x%.8x    %i %i %i %.4x %.2x %.2x %.1x",
			(DWORD)tsHeader->FileOffset,
			tsHeader->transport_error_indicator,
			tsHeader->payload_unit_start_indicator,
			tsHeader->transport_priority,
			tsHeader->PID,
			tsHeader->transport_scrambling_control,
			tsHeader->adaption_field_control,
			tsHeader->continuity_counter);
}

