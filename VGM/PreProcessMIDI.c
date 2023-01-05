#include "Common.h"
#include "IBMPC.h"
#include "PreProcessMIDI.h"

typedef struct
{
	uint8_t huge* pData;
	uint8_t huge* pCur;
	uint32_t delta;
	uint8_t runningStatus;
	uint16_t lastLength;
} MIDITrack;

uint16_t nrOfTracks = 0;
MIDITrack tracks[2048];
uint16_t tracksStopped = 0;

uint8_t mt32Mode = 0;	// Special mode to prefix any program change with a special command for DreamBlaster S2(P) for MT-32 instruments

// Speed data
uint32_t tempo = 500000L;		// Microseconds per MIDI quarter-note, 24-bit value, default is 500000, which is 120 bpm
uint16_t division = 0;	// Ticks per quarter-note, 16-bit value
float divisor = 0;		// Precalc this at every tempo-change

// tempo/division = microseconds per MIDI tick
// 1 microsecond == 1/10000000 second

//#define MICROSEC_PER_TICK (tempo/(float)division)
//#define DIVISOR	(MICROSEC_PER_TICK*(PC_PITFREQ/1000000.0f))
//#define GETMIDIDELAY(x) ((uint32_t)((float)x*DIVISOR))
#define GETMIDIDELAY(x) ((uint32_t)(x*divisor))

// Variable-length integers are maximum 0FFFFFFF, so 28-bit.
/*
void WriteVarLen(FILE* pFile, uint32_t value)
{
	uint32_t buffer;
	
	buffer = value & 0x7f;
	
	while ((value >>= 7) > 0)
	{
		buffer <<= 8;
		buffer |= 0x80;
		buffer += (value & 0x7f);
	}

	while (1)
	{
		fputc(buffer, pFile);
		if (buffer & 0x80)
			buffer >>= 8;
		else
			break;
	}
}

uint32_t ReadVarLen(FILE* pFile)
{
	uint32_t value;
	uint8_t c;

	if ((value = fgetc(pFile)) & 0x80)
	{
		value &= 0x7f;
		
		do
		{
			value = (value << 7) + ((c = fgetc(pFile)) & 0x7f);
		} while (c & 0x80);
	}
	
	return value;
}
*/
uint8_t huge* ReadVarLen(uint8_t huge* pData, uint32_t *pValue)
{
	uint32_t value;
	uint8_t c;

	if ((value = *pData++) & 0x80)
	{
		value &= 0x7f;
		
		do
		{
			value = (value << 7) + ((c = *pData++) & 0x7f);
		} while (c & 0x80);
	}
	
	*pValue = value;
	
	return pData;
}

uint8_t huge* DoMetaEvent(uint8_t huge* pData)
{
	uint8_t type;
	uint32_t length;
	uint8_t data[16384];
	
	type = *pData++;
	pData = ReadVarLen(pData, &length);
				
	switch (type)
	{
		case 0x2F:	// End song
			tracksStopped++;
			break;
			
		// Text-based events
		case 0x01:	// Text Event
		case 0x02:	// Copyright Notice
		case 0x03:	// Sequence/Track Name
		case 0x04:	// Instrument Name
		case 0x05:	// Lyric
		case 0x06:	// Marker
		case 0x07:	// Cue Point
			_fmemcpy(data, pData, length);
			// Zero-terminate string
			data[length] = 0;
			printf("Meta-event %u: %s\n", type, data);
			pData += length;
			break;

		case 0x51:	// Set Tempo, in microseconds per MIDI quarter-note
			// Tempo is a 24-bit value, Big-Endian
			tempo = ((uint32_t)pData[0] << 16) | ((uint16_t)pData[1] << 8) | pData[2];
			printf("Tempo: %lu\n", tempo);
			pData += length;
			
			// Precalc the divisor for this tempo, for better performance
			divisor = (tempo*(PC_PITFREQ/1000000.0f))/division;
			break;
		
		// Skip
		case 0x00:	// Sequence Number
		case 0x20:	// MIDI Channel Prefix
		case 0x54:	// SMPTE Offset
		case 0x58:	// Time Signature
		case 0x59:	// Key Signature
		case 0x7F:	// Sequencer-Specific Meta-Event
		default:
			printf("Meta-event %02X, length: %lu\n", type, length);
			pData += length;
			break;
	}
	
	return pData;
}

void PreProcessMIDI(FILE* pFile, const char* pOutFile)
{
	MIDIHeader header;
	MIDIChunk track;
	uint16_t i, t = UINT32_MAX;
	uint8_t huge* pData;
	uint16_t firstDelay;
	uint32_t size, oldDelay = 0;
	FILE* pOut;
	uint8_t lastStatus = 0;
	uint8_t playing = 1;
	
	fread(&header, sizeof(header), 1, pFile);
	
	// File appears sane?
	if (memcmp(header.chunk.chunkType, "MThd", 4) != 0)
	{
		printf("Header of %c%c%c%c does not appear to be a MIDI file\n", header.chunk.chunkType[0], header.chunk.chunkType[1], header.chunk.chunkType[2], header.chunk.chunkType[3]);
		
		return;
	}
	
	header.chunk.length = SWAPL(header.chunk.length);
	header.format = SWAPS(header.format);
	header.ntrks = SWAPS(header.ntrks);
	header.division = SWAPS(header.division);
	
	printf("Header type: %c%c%c%c\n", header.chunk.chunkType[0], header.chunk.chunkType[1], header.chunk.chunkType[2], header.chunk.chunkType[3]);
	printf("Header size: %lu\n", header.chunk.length);
	printf("Format: %u\n", header.format);
	printf("Nr of tracks: %u\n", header.ntrks);
	printf("Division: %u\n", header.division);
	
	// If this is not a quarter-note divider, but rather SMPTE, we don't support it yet
	if (header.division & 0x8000)
	{
		printf("Unsupported SMPTE time detected.\n");
		
		return;
	}
	
	// Process relevant data from header	
	division = header.division;
	nrOfTracks = header.ntrks;
	if (nrOfTracks > _countof(tracks))
	{
		printf("Too many tracks: %u\nClamping to %u tracks", nrOfTracks, _countof(tracks));
		nrOfTracks = _countof(tracks);
	}
	
	// Calculate a default tempo, in case there is no specific Meta-Event
	divisor = (tempo*(PC_PITFREQ/1000000.0f))/division;
	
	// Read all tracks into memory
	for (i = 0; i < nrOfTracks; i++)
	{
		fread(&track, sizeof(track), 1, pFile);

		// File appears sane?
		if (memcmp(track.chunkType, "MTrk", 4) != 0)
		{
			printf("Chunk of %c%c%c%c does not appear to be a MIDI track\n", track.chunkType[0], track.chunkType[1], track.chunkType[2], track.chunkType[3]);
		
			return;
		}
	
		track.length = SWAPL(track.length);
	
		printf("MIDI track type: %c%c%c%c\n", track.chunkType[0], track.chunkType[1], track.chunkType[2], track.chunkType[3]);
		printf("Track size: %lu\n", track.length);
		
		// Process releveant data from header
		// Read into memory
		tracks[i].pData = farmalloc(track.length);
		_farfread(tracks[i].pData, track.length, 1, pFile);
		tracks[i].pCur = tracks[i].pData;
	}
	
	fclose(pFile);
	
	// Reset all pointers
	ClearCommands();
	
	printf("Start preprocessing MIDI\n");
	
	pOut = fopen(pOutFile, "wb");

	preHeader.nrOfMIDI = 1;
	
	printf("# SN76489: %u\n", preHeader.nrOfSN76489);
	printf("# SAA1099: %u\n", preHeader.nrOfSAA1099);
	printf("# AY8930: %u\n", preHeader.nrOfAY8930);
	printf("# YM3812: %u\n", preHeader.nrOfYM3812);
	printf("# YMF262: %u\n", preHeader.nrOfYMF262);
	printf("# MIDI: %u\n", preHeader.nrOfMIDI);
	
	// Save header
	_farfwrite(&preHeader, sizeof(preHeader), 1, pOut);

	// Get the first deltas for each stream
	for (i = 0; i < nrOfTracks; i++)
	{
		// Get a delta-time from the stream
		tracks[i].pCur = ReadVarLen(tracks[i].pCur, &tracks[i].delta);
	}
	
	while (playing && (tracksStopped < nrOfTracks))
	{
		uint32_t delta, delay, minDelay, length;
		uint8_t value, type;
		uint16_t oldT, oldTracksStopped;
		
		// Detect if a track was stopped
		oldTracksStopped = tracksStopped;
		
		// Select the track with the next event
		delta = UINT32_MAX - 1;
		oldT = t;
		t = 0;
		
		for (i = 0; i < nrOfTracks; i++)
		{
			if (delta > tracks[i].delta)
			{
				t = i;
				delta = tracks[i].delta;
			}
			else if ((delta == tracks[i].delta) && (oldT == i))
			{
				// Prefer same track as previous, for possible running status
				t = i;
			}
		}
		
		// Select data for track
		pData = tracks[t].pCur;
		
		// Adjust all other tracks to the new delta
		for (i = 0; i < nrOfTracks; i++)
		{
			if (tracks[i].delta < UINT32_MAX)
				tracks[i].delta -= delta;
		}
		
		delay = GETMIDIDELAY(delta) + oldDelay;
		
		length = GetCommandLengthCount(0, MIDI, NULL);
		
		// Calculate PIT ticks required for data so far
		minDelay = INT_OVERHEAD + (MIDI_BYTE_DURATION*length);
		
		// Is the delay smaller than the time required to send the notes?
		// Then skip the delay here, concatenate data to previous event, and
		// fix the total delay later
		if (delay > minDelay)
		{
			oldDelay = 0;
			
			AddDelay(delay, pOut);
			delay = 0;
		}
		else
		{
			//if (delay > 0)
			//	printf("Very small delay detected: %lu!\n", delay);
			
			oldDelay += delay;
		}

	
		// Get a MIDI command from the stream
		value = *pData++;
		
		switch (value)
		{
			// Regular SysEx (resets running status)
			case 0xF0:
				lastStatus = 0;
				pData = ReadVarLen(pData, &length);
				
				printf("SysEx F0, length: %lu\n", length);
				
				// Pre-pend 0xF0, it is implicit
				pData[-1] = value;
				AddCommandBuffer(0, MIDI, pData-1, length+1, pOut);
				pData += length;
				break;
			// Escaped SysEx (resets running status)
			case 0xF7:
				lastStatus = 0;
				pData = ReadVarLen(pData, &length);
				
				printf("SysEx F7, length: %lu\n", length);
				
				AddCommandBuffer(0, MIDI, pData, length, pOut);
				pData += length;
				break;
			// Meta event (resets running status)
			case 0xFF:
				lastStatus = 0;
				pData = DoMetaEvent(pData);
				break;
				
			// System messages (resets running status)
			case 0xF1:
				// Time code quarter frame (0nnndddd)
				lastStatus = 0;
				pData++;
				break;
			case 0xF2:
				// Song position pointer
				lastStatus = 0;
				pData += 2;
				break;
			case 0xF3:
				// Song select
				lastStatus = 0;
				pData++;
				break;
			case 0xF6:
				// Tune request (single byte)
				lastStatus = 0;
				AddCommand(0, MIDI, value, pOut);
				break;
			// Real-time messages (do not participate in running status)
			case 0xF8:
			case 0xF9:
			case 0xFA:
			case 0xFB:
			case 0xFC:
			case 0xFD:
			case 0xFE:
				// Just a single byte, should never occur in a MIDI file?
				AddCommand(0, MIDI, value, pOut);
				break;

			// Reserved (resets running status)
			case 0xF4:
			case 0xF5:
				lastStatus = 0;
				break;

			// Regular MIDI channel message
			default:
				// Not a status byte, use running-status
				if (value < 0x80)
				{
					pData--;
					pData[-1] = tracks[t].runningStatus;
					length = tracks[t].lastLength + 1;
				}
				else
				{
					type = value & 0xF0;
					
					switch (type)
					{
						// Single-byte message?
						case PC:
						case CHANNEL_AFTERTOUCH:
							length = 1;
							break;
						// Double-byte message
						default:
							length = 2;
							break;
					}
					
					// Store data for running-status mode
					// Length will be 1 shorter, because the status byte will be omitted
					tracks[t].lastLength = length - 1;
					tracks[t].runningStatus = value;					
				}
				
				// Perform MT-32 mode for DreamBlaster S2(P)
				if (mt32Mode && (pData[-1] & 0xF0) == PC)
				{
					// Insert MT-32 command, CC 0 = 127
					static uint8_t MT32[] = { CC, 0, 127 };

					// Set proper channel
					MT32[0] = CC | (pData[-1] & 0x0F);
					
					AddCommandBuffer(0, MIDI, MT32, _countof(MT32), pOut);
					lastStatus = MT32[0];
				}

				// Replace NOTE OFF messages with NOTE ON with velocity 0
				if ((pData[-1] & 0xF0) == NOTE_OFF)
				{
					pData[-1] = (pData[-1] & 0x0F) | NOTE_ON;
					pData[1] = 0;
				}
				
				// See if we can perform a new running status
				if (lastStatus == pData[-1])
				{
					// Skip first byte, as it's the same as before
					AddCommandBuffer(0, MIDI, pData, length, pOut);
				}
				else
				{
					// Send first byte as well
					AddCommandBuffer(0, MIDI, pData-1, length+1, pOut);
				}
				lastStatus = pData[-1];
				pData += length;
				break;
		}
		
		// Get a delta-time from the stream
		if (oldTracksStopped == tracksStopped)
			tracks[t].pCur = ReadVarLen(pData, &tracks[t].delta);
		else
			tracks[t].delta = UINT32_MAX;
	}
	
	// Output last delay of 0
	AddDelay(65536L, pOut);
	
	// And a final delay of 0, which would get fetched by the last int handler
	firstDelay = 0;
	fwrite(&firstDelay, sizeof(firstDelay), 1, pOut);
	
	// Update size field
	size = ftell(pOut);
	size -= sizeof(preHeader);
	fseek(pOut, 8, SEEK_SET);
	fwrite(&size, sizeof(size), 1, pOut);

	fclose(pOut);

	// Free all memory
	for (i = 0; i < nrOfTracks; i++)
		farfree(tracks[i].pData);

	printf("Done preprocessing MIDI\n");
}
