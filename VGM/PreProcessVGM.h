#ifndef _PREPROCESSVGM_H_
#define _PREPROCESSVGM_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dos.h>
#include <conio.h>
#include <math.h>
#include <malloc.h>
#include <string.h>
#include "Common.h"
#include "stdtype.h"	// VGMFile.h uses nonstandard types, define them separately
#include "VGMFile.h"
#include "PreProcess.h"

#define SampleRate 44100

#define	DIVISOR_SHIFT	(11U)

#define GETDELAY(n)	((uint32_t)(n*(uint32_t)divisor) >> DIVISOR_SHIFT)
//#define GETDELAY(n)	((uint32_t)(((n*(1193182.0/44100.0))+0.5)))

typedef struct
{
	uint8_t compatibility;	// 0x66 command
	uint8_t dataType;
	uint32_t dataSize;
} DataBlock;

void SetPITFreqVGM(uint32_t pitFreq);
void PreProcessVGM(FILE* pFile, const char* pOutFile);
void SplitTime(uint32_t time, uint16_t* pMinutes, uint16_t* pSeconds, uint16_t* pMillis);

#endif /* _PREPROCESSVGM_H_ */
