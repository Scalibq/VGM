#include "PreProcessVGM.h"

uint32_t delayTable[4096];
uint8_t tableInited = 0;

void SplitTime(uint32_t time, uint16_t* pMinutes, uint16_t* pSeconds, uint16_t* pMillis)
{
	*pMillis = time % 1000;
	time /= 1000;
	*pSeconds = time % 60;
	time /= 60;
	*pMinutes = time;
}

void InitDelayTable()
{
	int i;
	
	// Prepare delay table
	delayTable[0] = 2;
	for (i = 1; i < _countof(delayTable); i++)
		delayTable[i] = GETDELAY(i);
	
	tableInited = 1;
}

void PreProcessVGM(FILE* pFile, const char* pOutFile)
{
	FILE* pOut;
	uint32_t delay = 0, minDelay, length, lastDelay = 0, loopDelay = 0;
	uint16_t srcDelay;
	VGM_HEADER header;
	uint32_t dataOffset;
	uint16_t firstDelay;
	uint16_t i;
	uint8_t playing = 1;
	uint16_t minutes, seconds, ms;

	if (!tableInited)
		InitDelayTable();

	fread(&header, sizeof(header), 1, pFile);
	
	// File appears sane?
	if (header.fccVGM != FCC_VGM)
	{
		printf("Header of %08X does not appear to be a VGM file\n", header.fccVGM);
		
		return;
	}
	
	printf("VGM file details:\n");
	printf("EoF Offset: %08X\n", header.lngEOFOffset);
	printf("Version: %08X\n", header.lngVersion);
	printf("GD3 Offset: %08X\n", header.lngGD3Offset);
	printf("Total # samples: %lu\n", header.lngTotalSamples);
	printf("Loop offset: %08X\n", header.lngLoopOffset + 0x1C);
	printf("Loop samples: %lu\n", header.lngLoopSamples);
	printf("Playback Rate: %08X\n", header.lngRate);
	printf("VGM Data Offset: %08X\n", header.lngDataOffset);

	SplitTime(header.lngTotalSamples / (44100/1000), &minutes, &seconds, &ms);

	printf("Time: %u:%02u.%03u\n", minutes, seconds, ms);

	SplitTime(header.lngLoopSamples / (44100/1000), &minutes, &seconds, &ms);

	printf("Loop: %u:%02u.%03u\n", minutes, seconds, ms);

	if (header.lngDataOffset == 0)
		dataOffset = 0x40;
	else
		dataOffset = header.lngDataOffset + 0x34;
	
	printf("VGM Data starts at %08X\n", dataOffset);
	
    // Can we play this on PCjr hardware?
    /*if (header.SN76489clock != 0)
		printf("SN76489 Clock: %lu Hz\n", header.SN76489clock);
	else
	{
		printf("File does not contain data for our hardware\n");
		
		return;
	}*/
	
	// Seek to VGM data
	fseek(pFile, dataOffset, SEEK_SET);
	
	printf("Start preprocessing VGM\n");
	
	pOut = fopen(pOutFile, "wb");
	
	// Detect used chips
	preHeader.nrOfMIDI = 0;	// Not (yet?) supported in VGM
	preHeader.nrOfSN76489 = (header.lngHzPSG != 0) + ((header.lngHzPSG & 0x40000000L) != 0);
	preHeader.nrOfYM2151 = (header.lngHzYM2151 != 0) + ((header.lngHzYM2151 & 0x40000000L) != 0);
	
	preHeader.nrOfAY8930 = 0;
	preHeader.nrOfYM3812 = 0;
	preHeader.nrOfYMF262 = 0;
	preHeader.nrOfSAA1099 = 0;
	

	if (header.lngVersion >= 0x151)
	{
		if (dataOffset >= 0x54)
			preHeader.nrOfYM3812 = (header.lngHzYM3812 != 0) + ((header.lngHzYM3812 & 0x40000000L) != 0);
		
		if (dataOffset >= 0x60)
			preHeader.nrOfYMF262 = (header.lngHzYMF262 != 0) + ((header.lngHzYMF262 & 0x40000000L) != 0);

		if (dataOffset >= 0x88)
			preHeader.nrOfAY8930 = (header.lngHzAY8910 != 0) + ((header.lngHzAY8910 & 0x40000000L) != 0);
	}
	
	if (header.lngVersion >= 0x170)
	{
		if (dataOffset >= 0xCC)
			preHeader.nrOfSAA1099 = (header.lngHzSAA1099 != 0) + ((header.lngHzSAA1099 & 0x40000000L) != 0);
	}
	
	printf("# SN76489: %u\n", preHeader.nrOfSN76489);
	printf("# SAA1099: %u\n", preHeader.nrOfSAA1099);
	printf("# AY8930: %u\n", preHeader.nrOfAY8930);
	printf("# YM3812: %u\n", preHeader.nrOfYM3812);
	printf("# YMF262: %u\n", preHeader.nrOfYMF262);
	printf("# MIDI: %u\n", preHeader.nrOfMIDI);
	printf("# YM2151: %u\n", preHeader.nrOfYM2151);
	
	// Save header
	_farfwrite(&preHeader, sizeof(preHeader), 1, pOut);
	
	// Reset all pointers
	ClearCommands();
		
	while (playing)
	{
		uint8_t data[2];
		uint32_t pos;
		uint8_t value;

		// Handle loop
		pos = ftell(pFile);
		if (pos == (header.lngLoopOffset + 0x1C))
		{
			preHeader.loop = ftell(pOut) - sizeof(preHeader);
			loopDelay = lastDelay;
			
			printf("Loop point at %X\n", preHeader.loop);
		}

		value = fgetc(pFile);
		
		switch (value)
		{
			case 0x66:
				// end of VGM data
				playing = 0;
				break;
				
			// Wait-commands
			case 0x61:	// wait n samples
				{
					fread(&srcDelay, sizeof(srcDelay), 1, pFile);

					// For small values, use a quick table lookup
					if (srcDelay < _countof(delayTable))
						delay += delayTable[srcDelay];
					else
						delay += GETDELAY(srcDelay);
					
					goto endDelay;
					break;
				}
			case 0x62:	// wait 1/60th second: 735 samples
				delay += GETDELAY(735);
				goto endDelay;
				break;
			case 0x63:	// wait 1/50th second: 882 samples
				delay += GETDELAY(882);
				goto endDelay;
				break;
			case 0x70:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(1);
				goto endDelay;
				break;
			case 0x71:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(2);
				goto endDelay;
				break;
			case 0x72:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(3);
				goto endDelay;
				break;
			case 0x73:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(4);
				goto endDelay;
				break;
			case 0x74:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(5);
				goto endDelay;
				break;
			case 0x75:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(6);
				goto endDelay;
				break;
			case 0x76:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(7);
				goto endDelay;
				break;
			case 0x77:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(8);
				goto endDelay;
				break;
			case 0x78:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(9);
				goto endDelay;
				break;
			case 0x79:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(10);
				goto endDelay;
				break;
			case 0x7A:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(11);
				goto endDelay;
				break;
			case 0x7B:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(12);
				goto endDelay;
				break;
			case 0x7C:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(13);
				goto endDelay;
				break;
			case 0x7D:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(14);
				goto endDelay;
				break;
			case 0x7E:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(15);
				goto endDelay;
				break;
			case 0x7F:	// wait n+1 samples, n can range from 0 to 15.
				delay += GETDELAY(16);
				goto endDelay;
				break;

				// SN76489 commands
			case 0x4F:	// dd : Game Gear PSG stereo, write dd to port 0x06
			case 0x3F: 	// dd : Second Game Gear PSG stereo, write dd to port 0x06
				// stereo PSG cmd, ignored
				fseek(pFile, 1, SEEK_CUR);
				break;
			case 0x50:	// dd : PSG (SN76489/SN76496) write value dd
				AddCommand(0, SN76489, fgetc(pFile), pOut);
				break;
			case 0x30:	// dd : Second PSG (SN76489/SN76496) write value dd
				AddCommand(1, SN76489, fgetc(pFile), pOut);
				break;
			case 0x5A:	// aa dd : YM3812, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(0, YM3812, data[0], data[1], pOut);
				break;
			case 0xAA:	// aa dd : Second YM3812, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(1, YM3812, data[0], data[1], pOut);
				break;
			case 0x5E:	// aa dd : YMF262 port 0, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(0, YMF262PORT0, data[0], data[1], pOut);
				break;
			case 0x5F:	// aa dd : YMF262 port 1, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(0, YMF262PORT1, data[0], data[1], pOut);
				break;
			case 0xA0:	// aa dd : AY8910, write value dd to register aa
				// Second chip is indicated by msb in first byte
				fread(data, sizeof(data), 1, pFile);
				i = (data[0] & 0x80) ? 1 : 0;
				AddCommandMulti(i, AY8930, data[0] & 0x7F, data[1], pOut);
				break;
			case 0xBD:	// aa dd : SAA1099, write value dd to register aa
				// Second chip is indicated by msb in first byte
				fread(data, sizeof(data), 1, pFile);
				i = (data[0] & 0x80) ? 1 : 0;
				AddCommandMulti(i, SAA1099, data[0] & 0x7F, data[1], pOut);
				break;
			case 0xAE:	// aa dd : Second YMF262 port 0, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(1, YMF262PORT0, data[0], data[1], pOut);
				break;
			case 0xAF:	// aa dd : Second YMF262 port 1, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(1, YMF262PORT1, data[0], data[1], pOut);
				break;

			case 0x54:	// aa dd : YM2151, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(0, YM2151, data[0], data[1], pOut);
				break;
			case 0xA4:	// aa dd : Second YM2151, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(1, YM2151, data[0], data[1], pOut);
				break;
		
			case 0x51:	// aa dd : YM2413, write value dd to register aa
			case 0xA1:	// aa dd : Second YM2413, write value dd to register aa
			case 0x52:	// aa dd : YM2612 port 0, write value dd to register aa
			case 0x53:	// aa dd : YM2612 port 1, write value dd to register aa
			case 0xA2:	// aa dd : Second Second YM2612 port 0, write value dd to register aa
			case 0xA3:	// aa dd : Second YM2612 port 1, write value dd to register aa
			case 0x55:	// aa dd : YM2203, write value dd to register aa
			case 0xA5:	// aa dd : Second YM2203, write value dd to register aa
			case 0x56:	// aa dd : YM2608 port 0, write value dd to register aa
			case 0x57:	// aa dd : YM2608 port 1, write value dd to register aa
			case 0xA6:	// aa dd : Second YM2608 port 0, write value dd to register aa
			case 0xA7:	// aa dd : Second YM2608 port 1, write value dd to register aa
			case 0x58:	// aa dd : YM2610 port 0, write value dd to register aa
			case 0x59:	// aa dd : YM2610 port 1, write value dd to register aa
			case 0xA8:	// aa dd : Second YM2610 port 0, write value dd to register aa
			case 0xA9:	// aa dd : Second YM2610 port 1, write value dd to register aa
			case 0x5B:	// aa dd : YM3526, write value dd to register aa
			case 0xAB:	// aa dd : Second YM3526, write value dd to register aa
			case 0x5C:	// aa dd : Y8950, write value dd to register aa
			case 0xAC:	// aa dd : Second Y8950, write value dd to register aa
			case 0x5D:	// aa dd : YMZ280B, write value dd to register aa
			case 0xAD:	// aa dd : Second YMZ280B, write value dd to register aa
				// Skip
				fseek(pFile, 2, SEEK_CUR);
				break;

			default:
				printf("PreProcessVGM(): Invalid: %02X\n", value);
				break;
		}
		
		continue;
		
	endDelay:
		lastDelay = delay;
	
		// Filter out delays that are too small between commands
		// OPL2 needs 12 cycles for the data delay and 84 cycles for the address delay
		// This is at the base clock of 14.31818 / 4 = 3.579545 MHz
		// The PIT runs at a base clock of 14.31818 / 12 = 1.193182 MHz
		// So there are exactly 3 OPL2 cycles to every PIT cycle. Translating that is:
		// 4 PIT cycles for the data delay and 28 PIT cycles for the address delay
		// That is a total of 32 PIT cycles for every write
		
		// Calculate PIT ticks required for data so far
		minDelay = INT_OVERHEAD;
		
		for (i = 0; i < preHeader.nrOfSN76489; i++)
		{
			length = GetCommandLengthCount(i, SN76489, NULL);
			minDelay += (SN76489_COMMAND_DURATION*length);
		}

		for (i = 0; i < preHeader.nrOfSAA1099; i++)
		{
			length = GetCommandLengthCount(i, SAA1099, NULL);
			minDelay += (SAA1099_COMMAND_DURATION*length);
		}

		for (i = 0; i < preHeader.nrOfAY8930; i++)
		{
			length = GetCommandLengthCount(i, AY8930, NULL);
			minDelay += (AY8930_COMMAND_DURATION*length);
		}
		
		for (i = 0; i < preHeader.nrOfYM3812; i++)
		{
			length = GetCommandLengthCount(i, YM3812, NULL);
			minDelay += (ADLIB_COMMAND_DURATION*length);
		}

		for (i = 0; i < preHeader.nrOfYMF262; i++)
		{
			length = GetCommandLengthCount(i, YMF262PORT0, NULL) + GetCommandLengthCount(i, YMF262PORT1, NULL);
			minDelay += (OPL3_COMMAND_DURATION*length);
		}
		
		for (i = 0; i < preHeader.nrOfMIDI; i++)
		{
			length = GetCommandLengthCount(i, MIDI, NULL);
			minDelay += (MIDI_BYTE_DURATION*length);
		}

		for (i = 0; i < preHeader.nrOfYM2151; i++)
		{
			length = GetCommandLengthCount(i, YM2151, NULL);
			minDelay += (YM2151_COMMAND_DURATION*length);
		}

		if (delay > minDelay)
		{
			AddDelay(delay, pOut);
			delay = 0;
		}
		/*else
		{
			if (delay > 0)
				printf("Very small delay detected: %lu!\n", delay);
		}*/

	}
	
	if (preHeader.loop != 0)
	{
		// Output delay for loop
		AddDelay(loopDelay, pOut);
	}
	else
	{
		// Output last delay of 0
		AddDelay(65536L, pOut);
	
		// And a final delay of 0, which would get fetched by the last int handler
		firstDelay = 0;
		fwrite(&firstDelay, sizeof(firstDelay), 1, pOut);
	}
	
	// Update header for size and loop
	preHeader.size = ftell(pOut);
	preHeader.size -= sizeof(preHeader);
	fseek(pOut, 0, SEEK_SET);
	fwrite(&preHeader, sizeof(preHeader), 1, pOut);

	fclose(pOut);
	
	printf("Done preprocessing VGM\n");
}
