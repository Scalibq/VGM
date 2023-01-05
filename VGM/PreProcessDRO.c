#include "Common.h"
#include "IBMPC.h"
#include "PreProcessDRO.h"

void PreProcessDRO(FILE* pFile, const char* pOutFile)
{
	DROHeader header;
	FILE* pOut;
	uint8_t conversionTable[128];
	uint32_t lengthPairs = 0;
	uint32_t delay = 0, totalDelay = 0;
	uint32_t size;
	uint16_t firstDelay;
	
	fread(&header, sizeof(header), 1, pFile);
	
	printf("DRO file version %u.%u\n", header.versionHigh, header.versionLow);
	if (!((header.versionHigh == 2) && (header.versionLow == 0)))
	{
		printf("Unsupported version!\n");
		return;
	}
	
	switch (header.hardware)
	{
		case 0:
			printf("OPL2\n");
			preHeader.nrOfYM3812 = 1;
			break;
		case 1:
			printf("Dual OPL2\n");
			preHeader.nrOfYM3812 = 2;
			break;
		case 2:
			printf("OPL3\n");
			preHeader.nrOfYMF262 = 1;
			break;
	}
	
	// Load conversion table
	fread(conversionTable, header.conversionTableSize, 1, pFile);
	
	pOut = fopen(pOutFile, "wb");

	printf("# SN76489: %u\n", preHeader.nrOfSN76489);
	printf("# SAA1099: %u\n", preHeader.nrOfSAA1099);
	printf("# AY8930: %u\n", preHeader.nrOfAY8930);
	printf("# YM3812: %u\n", preHeader.nrOfYM3812);
	printf("# YMF262: %u\n", preHeader.nrOfYMF262);
	printf("# MIDI: %u\n", preHeader.nrOfMIDI);
	
	// Save header
	_farfwrite(&preHeader, sizeof(preHeader), 1, pOut);
	
	// Reset all pointers
	ClearCommands();
	
	printf("Start preprocessing DRO\n");

	// Process all register/data pairs in the file
	while (lengthPairs < header.commands)
	{
		uint32_t minDelay, length;
		uint8_t data[2];
		
		fread(data, sizeof(data), 1, pFile);
		
		// Handle special-case delay commands
		if (data[0] == header.delay256)
		{
			// Delay 1-256 ms
			// Accumulate delays until next command
			totalDelay += data[1] + 1L;
		}
		else if (data[0] == header.delayShift8)
		{
			// Delay (1-256) << 8 ms
			// Accumulate delays until next command
			totalDelay += (data[1] + 1L) << 8;
		}
		else
		{
			uint8_t chip = 0;
			
			// Write delays first
			// Break up into multiple delays with no notes
			delay = 0;
			if (totalDelay > 0)
			{
				// Convert to PIT ticks
				delay = totalDelay*(PC_PITFREQ/1000.0);
				
				// Calculate PIT ticks required for data so far
				switch (header.hardware)
				{
					case 0:	// OPL2
						length = GetCommandLengthCount(0, YM3812, NULL);
						minDelay = INT_OVERHEAD + (ADLIB_COMMAND_DURATION*length);
						break;
					case 1:	// Dual OPL2
						length = GetCommandLengthCount(0, YM3812, NULL) + GetCommandLengthCount(1, YM3812, NULL);
						minDelay = INT_OVERHEAD + (ADLIB_COMMAND_DURATION*length);
						break;
					case 2:	// OPL3
						length = GetCommandLengthCount(0, YMF262PORT0, NULL) + GetCommandLengthCount(0, YMF262PORT1, NULL);
						minDelay = INT_OVERHEAD + (OPL3_COMMAND_DURATION*length);
						break;
				}
		
				
				if (delay > minDelay)
				{
					totalDelay = 0;
					
					AddDelay(delay, pOut);
				}
				/*/else
				{
					if (delay > 0)
						printf("Very small delay detected: %lu!\n", delay);
				}*/
			}
			
			// Normal register/data pair
			if (data[0] & 0x80)
			{
				chip = 1;
				data[0] &= 0x7F;
			}
			
			if (header.hardware == 2)
			{
				// OPL3
				if (!chip)
					AddCommandMulti(0, YMF262PORT0,conversionTable[data[0]], data[1], pOut);
				else
					AddCommandMulti(0, YMF262PORT1,conversionTable[data[0]], data[1], pOut);
			}
			else
			{
				// OPL2
				AddCommandMulti(chip, YM3812,conversionTable[data[0]], data[1], pOut);
			}
		}
		
		lengthPairs++;
	}
	
	// Output last delay of 0
	AddDelay(0, pOut);

	// And a final delay of 0, which would get fetched by the last int handler
	firstDelay = 0;
	fwrite(&firstDelay, sizeof(firstDelay), 1, pOut);
	
	// Update size field
	size = ftell(pOut);
	size -= sizeof(preHeader);
	fseek(pOut, 8, SEEK_SET);
	fwrite(&size, sizeof(size), 1, pOut);

	fclose(pOut);
	
	printf("Done preprocessing DRO\n");		
}
