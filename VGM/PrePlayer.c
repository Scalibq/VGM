#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <malloc.h>
#include <string.h>
#include "Types.h"
#include "Output.h"
#include "PreProcess.h"
#include "PrePlayer.h"
#include "32bit.h"

uint8_t nrOfDevices = 0;
OutputDevice** pOutputs = NULL;

void GetDevices(void)
{
	uint8_t i;
	OutputDevice** pDev;

	nrOfDevices = preHeader.nrOfSN76489
		+ preHeader.nrOfSAA1099
		+ preHeader.nrOfAY8930
		+ preHeader.nrOfYM3812
		+ preHeader.nrOfYMF262
		+ preHeader.nrOfMIDI
		+ preHeader.nrOfYM2151
		+ preHeader.nrOfYM2203
		+ preHeader.nrOfYM2608;

	printf("Total: %u\n", nrOfDevices);

	// Allocate the required memory for all devices
	pOutputs = (OutputDevice**)malloc(nrOfDevices * sizeof(OutputDevice*));
	pDev = pOutputs;

	// Fill all required outputs, use dummy outputs if devices not available
	for (i = 0; i < preHeader.nrOfSN76489; i++)
	{
		*pDev++ = pSN76489[i];
	}
		
	for (i = 0; i < preHeader.nrOfSAA1099; i++)
	{
		*pDev++ = pSAA1099[i];
	}
				
	for (i = 0; i < preHeader.nrOfAY8930; i++)
	{
		*pDev++ = pAY8930[i];
	}
		
	for (i = 0; i < preHeader.nrOfYM3812; i++)
	{
		*pDev++ = pOPL2[i];
	}
		
	for (i = 0; i < preHeader.nrOfYMF262; i++)
	{
		*pDev++ = pOPL3[i];
	}
	
	for (i = 0; i < preHeader.nrOfMIDI; i++)
	{
		*pDev++ = pMIDI[i];
	}
	
	for (i = 0; i < preHeader.nrOfYM2151; i++)
	{
		*pDev++ = pYM2151[i];
	}

	for (i = 0; i < preHeader.nrOfYM2203; i++)
	{
		*pDev++ = pYM2203[i];
	}
	
	for (i = 0; i < preHeader.nrOfYM2608; i++)
	{
		*pDev++ = pYM2608[i];
	}
}

void InitDevices(void)
{
	uint8_t i;
	OutputDevice** pDev = pOutputs;
	
	// Get note data
	for (i = 0; i < nrOfDevices; i++)
	{
		(*pDev)->Init(*pDev);

		pDev++;
	}
}

void DeInitDevices(void)
{
	uint8_t i;
	OutputDevice** pDev = pOutputs;
	
	// Get note data
	for (i = 0; i < nrOfDevices; i++)
	{
		(*pDev)->DeInit(*pDev);

		pDev++;
	}
}

// Buffer format:
// uint16_t delay;
// uint8_t data_count;
// uint8_t data[data_count]
uint8_t huge* pPreprocessed;
uint8_t huge* pBuf;
uint8_t huge* pEndBuf;
uint8_t huge* pLoopStart;
uint8_t huge* pLoopEnd;

void LoadPreprocessed(const char* pFileName)
{
	FILE* pFile = fopen(pFileName, "rb");
	
	// Load from file
	_farfread(&preHeader, sizeof(preHeader), 1, pFile);
	
	pPreprocessed = farmalloc(preHeader.size);
	
	printf("Preprocessed size: %lu\n", preHeader.size);
	printf("Loop point: %X\n", preHeader.loop);
	
	printf("# SN76489: %u\n", preHeader.nrOfSN76489);
	printf("# SAA1099: %u\n", preHeader.nrOfSAA1099);
	printf("# AY8930: %u\n", preHeader.nrOfAY8930);
	printf("# YM3812: %u\n", preHeader.nrOfYM3812);
	printf("# YMF262: %u\n", preHeader.nrOfYMF262);
	printf("# MIDI: %u\n", preHeader.nrOfMIDI);
	printf("# YM2151: %u\n", preHeader.nrOfYM2151);
	printf("# YM2203: %u\n", preHeader.nrOfYM2203);
	printf("# YM2608: %u\n", preHeader.nrOfYM2608);
	
	// Search to start of data
	fseek(pFile, preHeader.headerLen, SEEK_SET);

	_farfread(pPreprocessed, preHeader.size, 1, pFile);
	fclose(pFile);

	pEndBuf = pPreprocessed + preHeader.size;
	
	// Set playing position to start of buffer
	pBuf = pPreprocessed;
	pLoopStart = pPreprocessed + preHeader.loop;
	pLoopEnd = pEndBuf;
}

void PlayData(void)
{
	uint8_t i;
	OutputDevice** pDev = pOutputs;
	
	// Get note data
	for (i = 0; i < nrOfDevices; i++)
	{
		pBuf = (*pDev)->Play(*pDev, pBuf);

		pDev++;
	}
}
