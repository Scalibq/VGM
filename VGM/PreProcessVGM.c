#include "PreProcessVGM.h"

uint32_t delayTable[4096];
uint8_t tableInited = 0;

static uint32_t divisor = 0;

void SetPITFreqVGM(uint32_t pitFreq)
{
	//#define DIVISOR		(55411U)	/* (1193182.0f/44100.0f) * 2048 */
	divisor = (pitFreq/44100.0) * 2048;
}

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

void DetectChips(VGM_HEADER* pHeader, uint16_t dataOffset)
{
	// Detect used chips
	preHeader.nrOfMIDI = 0;	// Not (yet?) supported in VGM
	preHeader.nrOfSN76489 = (pHeader->lngHzPSG != 0) + ((pHeader->lngHzPSG & 0x40000000L) != 0);
	preHeader.nrOfYM2151 = (pHeader->lngHzYM2151 != 0) + ((pHeader->lngHzYM2151 & 0x40000000L) != 0);
	
	preHeader.nrOfAY8930 = 0;
	preHeader.nrOfYM3812 = 0;
	preHeader.nrOfYMF262 = 0;
	preHeader.nrOfSAA1099 = 0;
	preHeader.nrOfYM2203 = 0;
	preHeader.nrOfYM2608 = 0;
	
	preHeader.speed = 0;

	if (pHeader->lngVersion >= 0x151)
	{
		if (dataOffset >= 0x48)
			preHeader.nrOfYM2203 = (pHeader->lngHzYM2203 != 0) + ((pHeader->lngHzYM2203 & 0x40000000L) != 0);
		
		if (dataOffset >= 0x4C)
			preHeader.nrOfYM2608 = (pHeader->lngHzYM2608 != 0) + ((pHeader->lngHzYM2608 & 0x40000000L) != 0);
		
		if (dataOffset >= 0x54)
			preHeader.nrOfYM3812 = (pHeader->lngHzYM3812 != 0) + ((pHeader->lngHzYM3812 & 0x40000000L) != 0);
		
		if (dataOffset >= 0x60)
			preHeader.nrOfYMF262 = (pHeader->lngHzYMF262 != 0) + ((pHeader->lngHzYMF262 & 0x40000000L) != 0);

		if (dataOffset >= 0x88)
			preHeader.nrOfAY8930 = (pHeader->lngHzAY8910 != 0) + ((pHeader->lngHzAY8910 & 0x40000000L) != 0);
	}
	
	if (pHeader->lngVersion >= 0x171)
	{
		if (dataOffset >= 0xCC)
			preHeader.nrOfSAA1099 = (pHeader->lngHzSAA1099 != 0) + ((pHeader->lngHzSAA1099 & 0x40000000L) != 0);
	}
	
	printf("# SN76489: %u\n", preHeader.nrOfSN76489);
	printf("# SAA1099: %u\n", preHeader.nrOfSAA1099);
	printf("# AY8930: %u\n", preHeader.nrOfAY8930);
	printf("# YM3812: %u\n", preHeader.nrOfYM3812);
	printf("# YMF262: %u\n", preHeader.nrOfYMF262);
	printf("# MIDI: %u\n", preHeader.nrOfMIDI);
	printf("# YM2151: %u\n", preHeader.nrOfYM2151);
	printf("# YM2203: %u\n", preHeader.nrOfYM2203);
	printf("# YM2608: %u\n", preHeader.nrOfYM2608);
}

#define DELAY_50HZ 0x1
#define DELAY_60HZ 0x2
#define DELAY_OTHER 0x4

uint8_t delayFlag = 0;
uint16_t fixedDelay = 0;

void DetectChipsFromData(FILE* pFile)
{
	uint8_t playing = 1;
	
	// Detect used chips
	preHeader.nrOfMIDI = 0;	// Not (yet?) supported in VGM
	preHeader.nrOfSN76489 = 0;
	preHeader.nrOfYM2151 = 0;	
	preHeader.nrOfAY8930 = 0;
	preHeader.nrOfYM3812 = 0;
	preHeader.nrOfYMF262 = 0;
	preHeader.nrOfSAA1099 = 0;
	preHeader.nrOfYM2203 = 0;
	preHeader.nrOfYM2608 = 0;
	
	while (playing)
	{
		uint8_t data[2];
		DataBlock dataBlock;
		uint8_t value, i;
		uint16_t srcDelay;

		value = fgetc(pFile);
		
		switch (value)
		{
			case 0x66:
				// end of VGM data
				playing = 0;
				break;
				
			// Wait-commands
			case 0x61:	// wait n samples
				delayFlag |= DELAY_OTHER;
				fread(&srcDelay, sizeof(srcDelay), 1, pFile);
				srcDelay = GETDELAY(srcDelay);
				if (fixedDelay == 0)
					fixedDelay = srcDelay;
				else
				{
					if (fixedDelay != -1)
						if (fixedDelay != srcDelay)
							fixedDelay = -1;
				}
				break;
			case 0x62:	// wait 1/60th second: 735 samples
				delayFlag |= DELAY_60HZ;
				break;
			case 0x63:	// wait 1/50th second: 882 samples
				delayFlag |= DELAY_50HZ;
				break;
			case 0x70:	// wait n+1 samples, n can range from 0 to 15.
			case 0x71:	// wait n+1 samples, n can range from 0 to 15.
			case 0x72:	// wait n+1 samples, n can range from 0 to 15.
			case 0x73:	// wait n+1 samples, n can range from 0 to 15.
			case 0x74:	// wait n+1 samples, n can range from 0 to 15.
			case 0x75:	// wait n+1 samples, n can range from 0 to 15.
			case 0x76:	// wait n+1 samples, n can range from 0 to 15.
			case 0x77:	// wait n+1 samples, n can range from 0 to 15.
			case 0x78:	// wait n+1 samples, n can range from 0 to 15.
			case 0x79:	// wait n+1 samples, n can range from 0 to 15.
			case 0x7A:	// wait n+1 samples, n can range from 0 to 15.
			case 0x7B:	// wait n+1 samples, n can range from 0 to 15.
			case 0x7C:	// wait n+1 samples, n can range from 0 to 15.
			case 0x7D:	// wait n+1 samples, n can range from 0 to 15.
			case 0x7E:	// wait n+1 samples, n can range from 0 to 15.
			case 0x7F:	// wait n+1 samples, n can range from 0 to 15.
				delayFlag |= DELAY_OTHER;
				fixedDelay = -1;
				break;

				// SN76489 commands
			case 0x4F:	// dd : Game Gear PSG stereo, write dd to port 0x06
				if (preHeader.nrOfSN76489 < 1)
					preHeader.nrOfSN76489 = 1;
				fseek(pFile, 1, SEEK_CUR);
				break;
			case 0x3F: 	// dd : Second Game Gear PSG stereo, write dd to port 0x06
				if (preHeader.nrOfSN76489 < 2)
					preHeader.nrOfSN76489 = 2;
				fseek(pFile, 1, SEEK_CUR);
				break;
			case 0x50:	// dd : PSG (SN76489/SN76496) write value dd
				if (preHeader.nrOfSN76489 < 1)
					preHeader.nrOfSN76489 = 1;
				fseek(pFile, 1, SEEK_CUR);
				break;
			case 0x30:	// dd : Second PSG (SN76489/SN76496) write value dd
				if (preHeader.nrOfSN76489 < 2)
					preHeader.nrOfSN76489 = 2;
				fseek(pFile, 1, SEEK_CUR);
				break;
			case 0x5A:	// aa dd : YM3812, write value dd to register aa
				if (preHeader.nrOfYM3812 < 1)
					preHeader.nrOfYM3812 = 1;
				fseek(pFile, 2, SEEK_CUR);
				break;
			case 0xAA:	// aa dd : Second YM3812, write value dd to register aa
				if (preHeader.nrOfYM3812 < 2)
					preHeader.nrOfYM3812 = 2;
				fseek(pFile, 2, SEEK_CUR);
				break;
			case 0x5E:	// aa dd : YMF262 port 0, write value dd to register aa
				if (preHeader.nrOfYMF262 < 1)
					preHeader.nrOfYMF262 = 1;
				fseek(pFile, 2, SEEK_CUR);
				break;
			case 0x5F:	// aa dd : YMF262 port 1, write value dd to register aa
				if (preHeader.nrOfYMF262 < 1)
					preHeader.nrOfYMF262 = 1;
				fseek(pFile, 2, SEEK_CUR);
				break;
			case 0xA0:	// aa dd : AY8910, write value dd to register aa
				// Second chip is indicated by msb in first byte
				fread(data, sizeof(data), 1, pFile);
				i = (data[0] & 0x80) ? 2 : 1;
				if (preHeader.nrOfAY8930 < i)
					preHeader.nrOfAY8930 = i;
				break;
			case 0xBD:	// aa dd : SAA1099, write value dd to register aa
				// Second chip is indicated by msb in first byte
				fread(data, sizeof(data), 1, pFile);
				i = (data[0] & 0x80) ? 2 : 1;
				if (preHeader.nrOfSAA1099 < i)
					preHeader.nrOfSAA1099 = i;
				break;
			case 0xAE:	// aa dd : Second YMF262 port 0, write value dd to register aa
				if (preHeader.nrOfYMF262 < 2)
					preHeader.nrOfYMF262 = 2;
				fseek(pFile, 2, SEEK_CUR);
				break;
			case 0xAF:	// aa dd : Second YMF262 port 1, write value dd to register aa
				if (preHeader.nrOfYMF262 < 2)
					preHeader.nrOfYMF262 = 2;
				fseek(pFile, 2, SEEK_CUR);
				break;

			case 0x54:	// aa dd : YM2151, write value dd to register aa
				if (preHeader.nrOfYM2151 < 1)
					preHeader.nrOfYM2151 = 1;
				fseek(pFile, 2, SEEK_CUR);
				break;
			case 0xA4:	// aa dd : Second YM2151, write value dd to register aa
				if (preHeader.nrOfYM2151 < 2)
					preHeader.nrOfYM2151 = 2;
				fseek(pFile, 2, SEEK_CUR);
				break;
		
			case 0x55:	// aa dd : YM2203, write value dd to register aa
				if (preHeader.nrOfYM2203 < 1)
					preHeader.nrOfYM2203 = 1;
				fseek(pFile, 2, SEEK_CUR);
				break;
				
			case 0xA5:	// aa dd : Second YM2203, write value dd to register aa
				if (preHeader.nrOfYM2203 < 2)
					preHeader.nrOfYM2203 = 2;
				fseek(pFile, 2, SEEK_CUR);
				break;

			case 0x56:	// aa dd : YM2608 port 0, write value dd to register aa
				if (preHeader.nrOfYM2608 < 1)
					preHeader.nrOfYM2608 = 1;
				fseek(pFile, 2, SEEK_CUR);
				break;

			case 0x57:	// aa dd : YM2608 port 1, write value dd to register aa
				if (preHeader.nrOfYM2608 < 1)
					preHeader.nrOfYM2608 = 1;
				fseek(pFile, 2, SEEK_CUR);
				break;

			case 0xA6:	// aa dd : Second YM2608 port 0, write value dd to register aa
				if (preHeader.nrOfYM2608 < 2)
					preHeader.nrOfYM2608 = 2;
				fseek(pFile, 2, SEEK_CUR);
				break;

			case 0xA7:	// aa dd : Second YM2608 port 1, write value dd to register aa
				if (preHeader.nrOfYM2608 < 2)
					preHeader.nrOfYM2608 = 2;
				fseek(pFile, 2, SEEK_CUR);
				break;
				
			case 0x67:	// Data block: skip for now
				fread(&dataBlock, sizeof(dataBlock), 1, pFile);
				fseek(pFile, dataBlock.dataSize, SEEK_CUR);
				break;

			case 0x51:	// aa dd : YM2413, write value dd to register aa
			case 0xA1:	// aa dd : Second YM2413, write value dd to register aa
			case 0x52:	// aa dd : YM2612 port 0, write value dd to register aa
			case 0x53:	// aa dd : YM2612 port 1, write value dd to register aa
			case 0xA2:	// aa dd : Second Second YM2612 port 0, write value dd to register aa
			case 0xA3:	// aa dd : Second YM2612 port 1, write value dd to register aa
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
				break;
		}
	}

	printf("# SN76489: %u\n", preHeader.nrOfSN76489);
	printf("# SAA1099: %u\n", preHeader.nrOfSAA1099);
	printf("# AY8930: %u\n", preHeader.nrOfAY8930);
	printf("# YM3812: %u\n", preHeader.nrOfYM3812);
	printf("# YMF262: %u\n", preHeader.nrOfYMF262);
	printf("# MIDI: %u\n", preHeader.nrOfMIDI);
	printf("# YM2151: %u\n", preHeader.nrOfYM2151);
	printf("# YM2203: %u\n", preHeader.nrOfYM2203);
	printf("# YM2608: %u\n", preHeader.nrOfYM2608);
	
	if (delayFlag == DELAY_50HZ)
	{
		printf("Speed: 50 Hz\n");
		preHeader.speed = PC_PITFREQ/50;
	}
	else if (delayFlag == DELAY_60HZ)	
	{
		printf("Speed: 60 Hz\n");
		preHeader.speed = PC_PITFREQ/60;
	}
	else
	{
		printf("Speed: other\n");
		if (fixedDelay != -1)
			preHeader.speed = fixedDelay;
	}
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
	
	//DetectChips(&header, dataOffset);
	DetectChipsFromData(pFile);
	
	// Seek to VGM data
	fseek(pFile, dataOffset, SEEK_SET);
	
	pOut = fopen(pOutFile, "wb");

	// Save header
	_farfwrite(&preHeader, sizeof(preHeader), 1, pOut);
	
	// Reset all pointers
	ClearCommands();
		
	while (playing)
	{
		uint8_t data[2];
		DataBlock dataBlock;
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
		
			case 0x55:	// aa dd : YM2203, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(0, YM2203, data[0], data[1], pOut);
				break;
				
			case 0xA5:	// aa dd : Second YM2203, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(1, YM2203, data[0], data[1], pOut);
				break;

			case 0x56:	// aa dd : YM2608 port 0, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(0, YM2608PORT0, data[0], data[1], pOut);
				break;

			case 0x57:	// aa dd : YM2608 port 1, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(0, YM2608PORT1, data[0], data[1], pOut);
				break;

			case 0xA6:	// aa dd : Second YM2608 port 0, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(1, YM2608PORT0, data[0], data[1], pOut);
				break;

			case 0xA7:	// aa dd : Second YM2608 port 1, write value dd to register aa
				fread(data, sizeof(data), 1, pFile);
				AddCommandMulti(1, YM2608PORT1, data[0], data[1], pOut);
				break;
				
			case 0x67:	// Data block: skip for now
				fread(&dataBlock, sizeof(dataBlock), 1, pFile);
				fseek(pFile, dataBlock.dataSize, SEEK_CUR);
				break;

			case 0x51:	// aa dd : YM2413, write value dd to register aa
			case 0xA1:	// aa dd : Second YM2413, write value dd to register aa
			case 0x52:	// aa dd : YM2612 port 0, write value dd to register aa
			case 0x53:	// aa dd : YM2612 port 1, write value dd to register aa
			case 0xA2:	// aa dd : Second Second YM2612 port 0, write value dd to register aa
			case 0xA3:	// aa dd : Second YM2612 port 1, write value dd to register aa
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

		for (i = 0; i < preHeader.nrOfYM2203; i++)
		{
			length = GetCommandLengthCount(i, YM2203, NULL);
			minDelay += (YM2203_COMMAND_DURATION*length);
		}

		for (i = 0; i < preHeader.nrOfYM2608; i++)
		{
			length = GetCommandLengthCount(i, YM2608PORT0, NULL) + GetCommandLengthCount(i, YM2608PORT1, NULL);
			minDelay += (YM2608_COMMAND_DURATION*length);
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
