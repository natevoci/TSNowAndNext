/**
 *	TSNowAndNext, a tool for creating a projectX .Xcl file based on
 *	changes in the Now and Next information in the transport stream.
 *	Copyright (C) 2011 Nate
 *
 *	Nate can be reached on the forums at <http://forums.dvbowners.com/>.
 */

#ifndef TRANSPORT_PACKET_H
#define TRANSPORT_PACKET_H

struct transport_packet_header
{
	int ref_count;

	LONGLONG FileOffset;

	BYTE sync_byte;
	BOOL transport_error_indicator;
	BOOL payload_unit_start_indicator;
	BOOL transport_priority;
	WORD PID;
	BYTE transport_scrambling_control;
	BYTE adaption_field_control;
	BYTE continuity_counter;

	BOOL HasAdaptionField;
	BOOL HasPayload;

	BYTE payload_offset;

	BOOL IsPES;
	struct table_tag
	{
		BYTE table_id;
		BOOL section_syntax_indicator;
		SHORT section_length;
		SHORT section_length_including_header;
		WORD id_after_length;
	} table;

};

struct transport_packet
{
	int ref_count;

	LONGLONG FileOffset;

	BYTE sync_byte;
	BOOL transport_error_indicator;
	BOOL payload_unit_start_indicator;
	BOOL transport_priority;
	WORD PID;
	BYTE transport_scrambling_control;
	BYTE adaption_field_control;
	BYTE continuity_counter;

	BOOL HasAdaptionField;
	BOOL HasPayload;

	BYTE payload_offset;

	BYTE *packet_bytes;
	LONG packet_bytes_count;

	struct adaption_field_tag
	{
		BYTE adaption_field_length;
		BOOL discontinuity_indicator;
		BOOL random_access_indicator;
		BOOL elementary_stream_priority_indicator;
		BOOL PCR_flag;
		BOOL OPCR_flag;
		BOOL splicing_point_flag;
		BOOL transport_private_data_flag;
		BOOL adaption_field_entention_flag;
		
		LONGLONG program_clock_reference_base;
		BYTE program_clock_reference_reserved;
		WORD program_clock_reference_extention;

		LONGLONG original_program_clock_reference_base;
		BYTE original_program_clock_reference_reserved;
		WORD original_program_clock_reference_extention;

		BYTE splice_countdown;

		BYTE transport_private_data_length;

		struct adaption_field_extention_tag
		{
			BYTE adaption_field_extention_length;
			BOOL ltw_flag;
			BOOL piecewise_rate_flag;
			BOOL seamless_splice_flag;
			BYTE adaption_field_entention_reserved;

			BOOL ltw_valid_flag;
			WORD ltw_offset;

			BYTE piecewise_rate_reserved;
			WORD piecewise_rate;
		} extention;
	} adaption_field;

	BOOL IsPES;
	struct pes_tag
	{
		BYTE stream_id;
		SHORT PES_packet_length;
	} pes;

	BYTE pointer;

	struct table_tag
	{
		BYTE table_id;
		BOOL section_syntax_indicator;
		SHORT section_length;
		SHORT section_length_including_header;
		WORD id_after_length;
	} table;
};

struct pat_table
{
	int ref_count;

	BYTE table_id;
	BOOL section_syntax_indicator;
	BOOL reserved_0;
	BYTE reserved_1;
	SHORT section_length;
	WORD transport_stream_id;
	BYTE reserved_2;
	BYTE version_number;
	BOOL current_next_indicator;
	BYTE section_number;
	BYTE last_section_number;
	struct program_tag
	{
		WORD program_number;
		BYTE reserved;
		WORD program_map_PID;
	};
	WORD network_PID;
	program_tag* programs;
	BYTE programCount;
};

struct pmt_table
{
	int ref_count;

	BYTE table_id;
	BOOL section_syntax_indicator;
	BOOL reserved_0;
	BYTE reserved_1;
	SHORT section_length;
	WORD program_number;
	BYTE reserved_2;
	BYTE version_number;
	BOOL current_next_indicator;
	BYTE section_number;
	BYTE last_section_number;
	BYTE reserved_3;
	WORD PCR_PID;
	BYTE reserved_4;
	SHORT program_info_length;
	struct program_info_tag
	{
		WORD stream_type;
		BYTE reserved_0;
		WORD elementary_PID;
		BYTE reserved_1;
		SHORT ES_info_length;
	};
	program_info_tag* programInfos;
	BYTE programInfoCount;
};

struct eit_table
{
	int ref_count;

	BYTE table_id;
	BOOL section_syntax_indicator;
	BOOL reserved_future_use;
	BYTE reserved_1;
	SHORT section_length;
	WORD service_id;
	BYTE reserved_2;
	BYTE version_number;
	BOOL current_next_indicator;
	BYTE section_number;
	BYTE last_section_number;
	WORD transport_stream_id;
	WORD original_network_id;
	BYTE segment_last_section_number;
	BYTE last_table_id;
	BYTE *events;
	BYTE events_length;
};

struct short_event_descriptor
{
	int ref_count;

	BYTE descriptor_tag;
	BYTE descriptor_length;
	BYTE language_code[4];

	int event_name_length;
	char* event_name;

	int text_length;
	char* text;

};

struct eit_event
{
	WORD event_id;
	FILETIME start_time_utc;
	LONG duration_seconds;
	BYTE running_status;
	BOOL free_CA_mode;
	SHORT descriptors_loop_length;
	BYTE *descriptors;

	short_event_descriptor* short_event_descriptor;
};

struct eit_event_list
{
	int ref_count;

	BYTE count;
	eit_event *list;

	BYTE indexOfRunningEvent;
	eit_event *running_event;
};

void FreeTSPacketHeader(transport_packet_header* packet);
void FreeTSPacket(transport_packet* packet);
void FreePATTable(pat_table* table);
void FreePMTTable(pmt_table* object);
void FreeEITTable(eit_table* table);
void FreeShortEventDescriptor(short_event_descriptor* object);
void FreeEITEventList(eit_event_list* eventList);

transport_packet_header* ParseTSPacketHeader(BYTE *pPacket, LONGLONG fileOffset);
transport_packet* ParseTSPacketFull(BYTE *pPacket, LONG packetByteCount, LONGLONG fileOffset);

pat_table* ParsePATTable(transport_packet *pPacket);
pmt_table* ParsePMTTable(transport_packet *pPacket);
eit_table* ParseEITTable(transport_packet *pPacket);
eit_event_list* ParseEITEvents(eit_table *pEITTable);

#endif
