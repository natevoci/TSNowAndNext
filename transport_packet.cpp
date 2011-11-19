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

//////////////////////////////////////////////////////////////////////

FILETIME mjd_to_FILETIME(DWORD mjd, BYTE hour, BYTE minute, BYTE second)
{
	WORD year = (WORD)((mjd - 15078.2) / 365.25);
	WORD month = (WORD)((mjd - 14956.1 - year * 365.25) / 30.6001);
	WORD day = (WORD)(mjd - 14956.0 - (year * 365.25) - (month * 30.6001));

	WORD k = (month >= 14) ? 1 : 0;
	year = year + k;
	month = month - 1 - k * 12;

	SYSTEMTIME systemTime;
	memset(&systemTime, 0, sizeof(SYSTEMTIME));
	systemTime.wDay = day;
	systemTime.wMonth = month;
	systemTime.wYear = year + 1900;
	systemTime.wHour = hour;
	systemTime.wMinute = minute;
	systemTime.wSecond = second;

	FILETIME result;

	SystemTimeToFileTime(&systemTime, &result);
	return result;
}

//////////////////////////////////////////////////////////////////////

void FreeTSPacketHeader(transport_packet_header* object)
{
	if (object == NULL)
		return;
	object->ref_count--;
	if (object->ref_count == 0)
	{
		free(object);
	}
}

void FreeTSPacket(transport_packet* object)
{
	if (object == NULL)
		return;
	object->ref_count--;
	if (object->ref_count == 0)
	{
		if (object->packet_bytes != NULL)
			free(object->packet_bytes);
		free(object);
	}
}

void FreePATTable(pat_table* object)
{
	if (object == NULL)
		return;
	object->ref_count--;
	if (object->ref_count == 0)
	{
		if (object->programs != NULL)
			free(object->programs);
		free(object);
	}
}

void FreePMTTable(pmt_table* object)
{
	if (object == NULL)
		return;
	object->ref_count--;
	if (object->ref_count == 0)
	{
		if (object->programInfos != NULL)
			free(object->programInfos);
		free(object);
	}
}

void FreeEITTable(eit_table* object)
{
	if (object == NULL)
		return;
	object->ref_count--;
	if (object->ref_count == 0)
	{
		free(object);
	}
}

void FreeShortEventDescriptor(short_event_descriptor* object)
{
	if (object == NULL)
		return;
	object->ref_count--;
	if (object->ref_count == 0)
	{
		if (object->event_name != NULL)
			free(object->event_name);
		if (object->text)
			free(object->text);
		free(object);
	}
}

void FreeEITEventList(eit_event_list* object)
{
	if (object == NULL)
		return;
	object->ref_count--;
	if (object->ref_count == 0)
	{
		for (int i=0; i < object->count; i++)
		{
			FreeShortEventDescriptor(object->list[0].short_event_descriptor);
		}
		free(object);
	}
}

//////////////////////////////////////////////////////////////////////

transport_packet_header* ParseTSPacketHeader(BYTE *pPacket, LONGLONG fileOffset)
{
	transport_packet_header* result = (transport_packet_header*)malloc(sizeof(transport_packet_header));
	memset(result, 0, sizeof(transport_packet_header));
	result->ref_count = 1;
	result->FileOffset = fileOffset;

	BYTE *pCurr = pPacket;
	
	result->sync_byte = pCurr[0];
	if (result->sync_byte != 0x47)
		return result;

	result->transport_error_indicator = ((pCurr[1] & 0x80) != 0);
	if (result->transport_error_indicator == TRUE)
		return result;

	result->payload_unit_start_indicator = ((pCurr[1] & 0x40) != 0);
	result->transport_priority = ((pCurr[1] & 0x20) != 0);
	result->PID = ((pCurr[1] & 0x1f) << 8) | pCurr[2];
	result->transport_scrambling_control = (pCurr[3] & 0xc0) >> 6;
	result->adaption_field_control = (pCurr[3] & 0x30) >> 4;
	result->continuity_counter = pCurr[3] & 0x0f;

	result->HasAdaptionField = ((result->adaption_field_control & 0x2) != 0);
	result->HasPayload = ((result->adaption_field_control & 0x1) != 0);

	pCurr += 4;

	if (result->HasAdaptionField)
	{
		BYTE adaption_field_length = pCurr[0];
		pCurr += adaption_field_length + 1;
	}

	if (result->HasPayload == FALSE)
		return result;

	result->payload_offset = pCurr - pPacket;

	if ((pCurr[0] == 0) &&
		(pCurr[1] == 0) &&
		(pCurr[2] == 1))
	{
		result->IsPES = TRUE;
		return result;
	}

	if (result->payload_unit_start_indicator == 1)
	{
		BYTE pointer = pCurr[0];
		pCurr += 1 + pointer;

		result->payload_offset = pCurr - pPacket;

		result->table.table_id = pCurr[0];
		result->table.section_syntax_indicator = ((pCurr[1] & 0x80) != 0);
		result->table.section_length = ((pCurr[1] & 0x0F) << 8) + pCurr[2];
		result->table.section_length_including_header = result->table.section_length + (pCurr - pPacket) + 3; // 3 is the position of the end of the section_length field relative to pCurr
		result->table.id_after_length = (pCurr[3] << 8) + pCurr[4];
	}

	return result;
}

transport_packet* ParseTSPacketFull(BYTE *pPacket, LONG packetByteCount, LONGLONG fileOffset)
{
	transport_packet* result = (transport_packet*)malloc(sizeof(transport_packet));
	memset(result, 0, sizeof(transport_packet));
	result->ref_count = 1;
	result->FileOffset = fileOffset;

	result->packet_bytes_count = packetByteCount;
	result->packet_bytes = (BYTE*)malloc(packetByteCount);
	memcpy(result->packet_bytes, pPacket, packetByteCount);
	
	pPacket = result->packet_bytes;
	BYTE *pCurr = pPacket;

	result->sync_byte = pCurr[0];
	if (result->sync_byte != 0x47)
		return result;

	result->transport_error_indicator = ((pCurr[1] & 0x80) != 0);
	if (result->transport_error_indicator == TRUE)
		return result;

	result->payload_unit_start_indicator = ((pCurr[1] & 0x40) != 0);
	result->transport_priority = ((pCurr[1] & 0x20) != 0);
	result->PID = ((pCurr[1] & 0x1f) << 8) | pCurr[2];
	result->transport_scrambling_control = (pCurr[3] & 0xc0) >> 6;
	result->adaption_field_control = (pCurr[3] & 0x30) >> 4;
	result->continuity_counter = pCurr[3] & 0x0f;

	result->HasAdaptionField = ((result->adaption_field_control & 0x2) != 0);
	result->HasPayload = ((result->adaption_field_control & 0x1) != 0);

	pCurr += 4;

	if (result->HasAdaptionField)
	{
		BYTE *pAF = pCurr;

		result->adaption_field.adaption_field_length = pAF[0];
		pCurr += result->adaption_field.adaption_field_length + 1;

		if (result->adaption_field.adaption_field_length > 0)
		{
			result->adaption_field.discontinuity_indicator = ((pAF[1] & 0x80) != 0);
			result->adaption_field.random_access_indicator = ((pAF[1] & 0x40) != 0);
			result->adaption_field.elementary_stream_priority_indicator = ((pAF[1] & 0x20) != 0);
			result->adaption_field.PCR_flag = ((pAF[1] & 0x10) != 0);
			result->adaption_field.OPCR_flag = ((pAF[1] & 0x08) != 0);
			result->adaption_field.splicing_point_flag = ((pAF[1] & 0x04) != 0);
			result->adaption_field.transport_private_data_flag = ((pAF[1] & 0x02) != 0);
			result->adaption_field.adaption_field_entention_flag = ((pAF[1] & 0x01) != 0);

			BYTE *pAFCurr = pAF + 2;
			if (result->adaption_field.PCR_flag == 1)
			{
				LONGLONG pcr_base = 0;
				pcr_base += pAFCurr[0];
				pcr_base = pcr_base << 8;
				pcr_base += pAFCurr[1];
				pcr_base = pcr_base << 8;
				pcr_base += pAFCurr[2];
				pcr_base = pcr_base << 8;
				pcr_base += pAFCurr[3];
				pcr_base = pcr_base << 1;
				pcr_base += ((pAFCurr[4] & 0x80) >> 7);
				
				result->adaption_field.program_clock_reference_base = pcr_base;
				result->adaption_field.program_clock_reference_reserved = ((pAFCurr[4] & 0x7e) >> 1);
				result->adaption_field.program_clock_reference_extention = ((pAFCurr[4] & 0x01) << 8) + pAFCurr[5];
				pAFCurr += 6;
			}
			if (result->adaption_field.OPCR_flag == 1)
			{
				LONGLONG pcr_base = 0;
				pcr_base += pAFCurr[0];
				pcr_base = pcr_base << 8;
				pcr_base += pAFCurr[1];
				pcr_base = pcr_base << 8;
				pcr_base += pAFCurr[2];
				pcr_base = pcr_base << 8;
				pcr_base += pAFCurr[3];
				pcr_base = pcr_base << 1;
				pcr_base += ((pAFCurr[4] & 0x80) >> 7);

				result->adaption_field.original_program_clock_reference_base = pcr_base;
				result->adaption_field.original_program_clock_reference_reserved = ((pAFCurr[4] & 0x7e) >> 1);
				result->adaption_field.original_program_clock_reference_extention = ((pAFCurr[4] & 0x01) << 8) + pAFCurr[5];
				pAFCurr += 6;
			}
			if (result->adaption_field.splicing_point_flag == 1)
			{
				result->adaption_field.splice_countdown = pAFCurr[0];
				pAFCurr += 1;
			}
			if (result->adaption_field.transport_private_data_flag == 1)
			{
				result->adaption_field.transport_private_data_length = pAFCurr[0];
				pAFCurr += 1 + result->adaption_field.transport_private_data_length;
			}
			if (result->adaption_field.adaption_field_entention_flag == 1)
			{
				result->adaption_field.extention.adaption_field_extention_length = pAFCurr[0];
				result->adaption_field.extention.ltw_flag = ((pAFCurr[1] & 0x80) != 0);
				result->adaption_field.extention.piecewise_rate_flag = ((pAFCurr[1] & 0x40) != 0);
				result->adaption_field.extention.seamless_splice_flag = ((pAFCurr[1] & 0x20) != 0);
				result->adaption_field.extention.adaption_field_entention_reserved = ((pAFCurr[1] & 0x18) != 0);

				pAFCurr += 2;
				if (result->adaption_field.extention.ltw_flag == 1)
				{
					result->adaption_field.extention.ltw_valid_flag = ((pAFCurr[0] & 0x80) != 0);
					result->adaption_field.extention.ltw_offset += ((pAFCurr[0] & 0x7f) << 8) + pAFCurr[1];
					pAFCurr += 2;
				}
				if (result->adaption_field.extention.piecewise_rate_flag == 1)
				{
					result->adaption_field.extention.piecewise_rate_reserved = (pAFCurr[0] & 0xc0) >> 6;
					result->adaption_field.extention.piecewise_rate = ((pAFCurr[0] & 0x3F) << 16);
					result->adaption_field.extention.piecewise_rate += (pAFCurr[1] << 8);
					result->adaption_field.extention.piecewise_rate += pAFCurr[2];
					pAFCurr += 3;
				}
			}
		}
	}

	if (result->HasPayload == FALSE)
		return result;

	result->payload_offset = pCurr - pPacket;

	if ((pCurr[0] == 0) &&
		(pCurr[1] == 0) &&
		(pCurr[2] == 1))
	{
		result->IsPES = TRUE;
		result->pes.stream_id = pCurr[3];
		result->pes.PES_packet_length = (pCurr[4] << 8);
		result->pes.PES_packet_length += pCurr[5];
		return result;
	}

	if (result->payload_unit_start_indicator == 1)
	{
		result->pointer = pCurr[0];
		pCurr += 1 + result->pointer;

		result->payload_offset = pCurr - pPacket;

		result->table.table_id = pCurr[0];
		result->table.section_syntax_indicator = ((pCurr[1] & 0x80) != 0);
		result->table.section_length = ((pCurr[1] & 0x0F) << 8) + pCurr[2];
		result->table.section_length_including_header = result->table.section_length + (pCurr - pPacket) + 3; // 3 is the position of the end of the section_length field relative to pCurr
		result->table.id_after_length = (pCurr[3] << 8) + pCurr[4];
	}

	return result;
}

pat_table* ParsePATTable(transport_packet *pPacket)
{
	if (pPacket == NULL)
		return NULL;

	pat_table* pPAT = (pat_table*)malloc(sizeof(pat_table));
	memset(pPAT, 0, sizeof(pat_table));
	pPAT->ref_count = 1;

	BYTE* pCurr = pPacket->packet_bytes + pPacket->payload_offset;

	pPAT->table_id = pCurr[0];
	pPAT->section_syntax_indicator = ((pCurr[1] & 0x80) != 0);
	pPAT->reserved_0 = ((pCurr[1] & 0x40) != 0);
	pPAT->reserved_1 = ((pCurr[1] & 0x30) != 0);
		
	pPAT->section_length = ((pCurr[1] & 0x0F) << 8) + pCurr[2];
	BYTE* pEnd = pCurr + 3 + pPAT->section_length; // end = section_length + end byte position of section_length field
		
	pPAT->transport_stream_id = (pCurr[3] << 8) + pCurr[4];
		
	pPAT->reserved_2 = (pCurr[5] & 0xC0);
	pPAT->version_number = ((pCurr[5] & 0x3E) >> 1);
	pPAT->current_next_indicator = ((pCurr[5] & 0x01) != 0);

	pPAT->section_number = pCurr[6];
	pPAT->last_section_number = pCurr[7];

	BYTE* pStart = pCurr + 8;
	int count = (pEnd - pStart - 4) / 4; // -4 to ignore CRC. /4 because each PAT entry is 4 bytes long.

	if (count >= 0)
	{
		int used = 0;
		SHORT *program_numbers = (SHORT *)malloc(sizeof(SHORT) * count);
		SHORT *program_map_PIDs = (SHORT *)malloc(sizeof(SHORT) * count);
		for (int i=0; i<count; i++)
		{
			SHORT program_number = (pStart[0] << 8) + pStart[1];
			SHORT program_map_PID = ((pStart[2] & 0x1F) << 8) + pStart[3];

			if (program_number == 0)
			{
				pPAT->network_PID = program_map_PID;
			}
			else
			{
				program_numbers[used] = program_number;
				program_map_PIDs[used] = program_map_PID;
				used++;
			}
			pStart += 4;
		}
		pPAT->programs = (pat_table::program_tag *)malloc(sizeof(pat_table::program_tag) * used);
		pPAT->programCount = used;
		for (int i=0; i<used; i++)
		{
			pPAT->programs[i].program_number = program_numbers[i];
			pPAT->programs[i].program_map_PID = program_map_PIDs[i];
		}
		free(program_numbers);
		free(program_map_PIDs);
	}

	return pPAT;
}

pmt_table* ParsePMTTable(transport_packet *pPacket)
{
	if (pPacket == NULL)
		return NULL;

	pmt_table* pPMT = (pmt_table*)malloc(sizeof(pmt_table));
	memset(pPMT, 0, sizeof(pmt_table));
	pPMT->ref_count = 1;

	BYTE* pCurr = pPacket->packet_bytes + pPacket->payload_offset;

	pPMT->table_id = pCurr[0];
	pPMT->section_syntax_indicator = ((pCurr[1] & 0x80) != 0);
	pPMT->reserved_0 = ((pCurr[1] & 0x40) != 0);
	pPMT->reserved_1 = ((pCurr[1] & 0x30) != 0);
		
	pPMT->section_length = ((pCurr[1] & 0x0F) << 8) + pCurr[2];
	BYTE* pEnd = pCurr + 3 + pPMT->section_length; // end = section_length + end byte position of section_length field
		
	pPMT->program_number = (pCurr[3] << 8) + pCurr[4];
		
	pPMT->reserved_2 = (pCurr[5] & 0xC0);
	pPMT->version_number = ((pCurr[5] & 0x3E) >> 1);
	pPMT->current_next_indicator = ((pCurr[5] & 0x01) != 0);

	pPMT->section_number = pCurr[6];
	pPMT->last_section_number = pCurr[7];

	pPMT->reserved_3 = pCurr[8] & 0xE0;
	pPMT->PCR_PID = ((pCurr[8] & 0x1F) << 8) + pCurr[9];

	pPMT->reserved_4 = pCurr[10] & 0xF0;
	pPMT->program_info_length = ((pCurr[10] & 0x0F) << 8) + pCurr[11];

	BYTE* pStart = pCurr + 12;
	//TODO: parse the pPMT->programInfos

	return pPMT;
}

eit_table* ParseEITTable(transport_packet *pPacket)
{
	if (pPacket == NULL)
		return NULL;

	eit_table* pEIT = (eit_table*)malloc(sizeof(eit_table));
	memset(pEIT, 0, sizeof(eit_table));
	pEIT->ref_count = 1;

	BYTE* pCurr = pPacket->packet_bytes + pPacket->payload_offset;

	pEIT->table_id = pCurr[0];
	pEIT->section_syntax_indicator = ((pCurr[1] & 0x80) != 0);
	pEIT->reserved_future_use = ((pCurr[1] & 0x40) != 0);
	pEIT->reserved_1 = ((pCurr[1] & 0x30) != 0);
		
	pEIT->section_length = ((pCurr[1] & 0x0F) << 8) + pCurr[2];
	pEIT->events_length = pEIT->section_length - 11; // there's 11 bytes after section_length before events start
		
	pEIT->service_id = (pCurr[3] << 8) + pCurr[4];
		
	pEIT->reserved_2 = (pCurr[5] & 0xC0);
	pEIT->version_number = ((pCurr[5] & 0x3E) >> 1);
	pEIT->current_next_indicator = ((pCurr[5] & 0x01) != 0);

	pEIT->section_number = pCurr[6];
	pEIT->last_section_number = pCurr[7];
		
	pEIT->transport_stream_id = (pCurr[8] << 8) + pCurr[9];

	pEIT->original_network_id = (pCurr[10] << 8) + pCurr[11];

	pEIT->segment_last_section_number = pCurr[12];
	pEIT->last_table_id = pCurr[13];

	pEIT->events = pCurr + 14;

	return pEIT;
}

eit_event_list* ParseEITEvents(eit_table *pEITTable)
{
	if (pEITTable == NULL)
		return NULL;

	eit_event_list* pList = (eit_event_list*)malloc(sizeof(eit_event_list));
	memset(pList, 0, sizeof(eit_event_list));
	pList->ref_count = 1;

	BYTE* pStart = pEITTable->events;
	BYTE* pEnd = pStart + pEITTable->events_length;
				
	int arrayCount = 10;
	eit_event *events = (eit_event *)malloc(sizeof(eit_event) * arrayCount);
	memset(events, 0, sizeof(eit_event) * arrayCount);
	int used = 0;

	while (pStart < pEnd - 4)
	{
		eit_event *ev = &events[used];
		ev->event_id = (pStart[0] << 8) + pStart[1];

		DWORD mjd = (pStart[2] << 8) + pStart[3];
		BYTE hour = (pStart[4] & 0x0F) + (((pStart[4] & 0xF0) >> 4) * 10);
		BYTE minute = (pStart[5] & 0x0F) + (((pStart[5] & 0xF0) >> 4) * 10);
		BYTE second = (pStart[6] & 0x0F) + (((pStart[6] & 0xF0) >> 4) * 10);
		ev->start_time_utc = mjd_to_FILETIME(mjd, hour, minute, second);
					
		hour = (pStart[7] & 0x0F) + (((pStart[7] & 0xF0) >> 4) * 10);
		minute = (pStart[8] & 0x0F) + (((pStart[8] & 0xF0) >> 4) * 10);
		second = (pStart[9] & 0x0F) + (((pStart[9] & 0xF0) >> 4) * 10);
		ev->duration_seconds = second + (minute * 60) + (hour * 3600);

		ev->running_status = (pStart[10] & 0xE0) >> 5;
		ev->free_CA_mode = (pStart[10] & 0x10) >> 4;

		ev->descriptors_loop_length = ((pStart[10] & 0x0F) << 8) + pStart[11];

		BYTE* pDesc = pStart + 12;
		BYTE* pDescEnd = pDesc + ev->descriptors_loop_length;

		ev->descriptors = pDesc;

		while (pDesc < pDescEnd)
		{
			BYTE descriptor_tag = pDesc[0];
			BYTE descriptor_length = pDesc[1];
			if (descriptor_length == 0)
				break;

			switch (descriptor_tag)
			{
			case 0x4D: // short_event_descriptor
				{
					if (ev->short_event_descriptor != NULL)
						break;
					ev->short_event_descriptor = (short_event_descriptor*)malloc(sizeof(short_event_descriptor));
					memset(ev->short_event_descriptor, 0, sizeof(short_event_descriptor));
					ev->short_event_descriptor->ref_count = 1;

					ev->short_event_descriptor->descriptor_tag = descriptor_tag;
					ev->short_event_descriptor->descriptor_length = descriptor_length;
					ev->short_event_descriptor->language_code[0] = pDesc[2];
					ev->short_event_descriptor->language_code[1] = pDesc[3];
					ev->short_event_descriptor->language_code[2] = pDesc[4];
					ev->short_event_descriptor->language_code[3] = 0;

					BYTE* pName = pDesc + 5;
					BYTE length = pName[0];
					pName++;
					ev->short_event_descriptor->event_name_length = length;
					ev->short_event_descriptor->event_name = (char*)malloc(length + 1);
					ev->short_event_descriptor->event_name[length] = 0;
					memcpy(ev->short_event_descriptor->event_name, pName, length);

					pName += length;
					length = pName[0];
					pName++;
					ev->short_event_descriptor->text_length = length;
					ev->short_event_descriptor->text = (char*)malloc(length + 1);
					ev->short_event_descriptor->text[length] = 0;
					memcpy(ev->short_event_descriptor->text, pName, length);
				}
				break;
			case 0x4E: // extended_event_descriptor
			case 0x50: // component_descriptor
			case 0x54: // content_descriptor
			case 0x55: // parental_rating_descriptor
			case 0x76: // content_identifier_descriptor
				{
				}
				break;
			default:
				{
					descriptor_tag = descriptor_tag; // just a nothing line so i can put a breakpoint here to look for unknown descriptors
				}
				break;
			}

			pDesc += descriptor_length + 2;
		}

		// if buffer is too small then enlarge
		if (used >= arrayCount)
		{
			eit_event *newEvents = (eit_event *)malloc(sizeof(eit_event) * arrayCount * 2);
			memcpy(newEvents, events, sizeof(eit_event) * arrayCount);
			arrayCount *= 2;
		}

		pStart += 12 + events[used].descriptors_loop_length;
		used++;
	}

	pList->count = used;
	pList->list = (eit_event *)malloc(sizeof(eit_event) * used);
	memcpy(pList->list, events, sizeof(eit_event) * used);

	for (; pList->indexOfRunningEvent < pList->count; pList->indexOfRunningEvent++)
	{
		if (pList->list[pList->indexOfRunningEvent].running_status == 4) // running
		{
			pList->running_event = &pList->list[pList->indexOfRunningEvent];
			break;
		}
	}
	if (pList->indexOfRunningEvent >= pList->count)
		pList->indexOfRunningEvent = -1;

	return pList;
}
