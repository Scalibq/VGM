#ifndef _PREPROCESS_H_
#define _PREPROCESS_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dos.h>
#include <math.h>
#include <malloc.h>
#include <string.h>
#include "8253.h"
#include "Endianness.h"

// Index for each sound chip in a command stream
#define SN76489 0
#define SAA1099 1
#define AY8930 2
#define YM3812 3
#define YMF262PORT0 4
#define YMF262PORT1 5
#define MIDI 6

#define NUM_CHIPS 7
#define MAX_MULTICHIP 2

// MIDI is sent at 31250 bits per second
//in 8-N-1 format, so 1 start bit and 1 stop bit added, no parity, 10 bits total
// Which is 31250 / 10 = 3125 bytes per second
#define MIDI_BYTE_DURATION	(PITFREQ/3125)	// About 381 PIT ticks per MIDI byte
#define INT_OVERHEAD (100)
#define EPSILON 381

#define	SN76489_COMMAND_DURATION	(12)
#define	SAA1099_COMMAND_DURATION	(12)
#define	AY8930_COMMAND_DURATION		(12)
#define	ADLIB_COMMAND_DURATION		(250)
#define	OPL3_COMMAND_DURATION		(250)

typedef struct _PreHeader
{
	char marker[4];				// = {'P','r','e','V'}; // ("Pre-processed VGM"? No idea, just 4 characters to detect that this is one of ours)
	uint32_t headerLen;			// = sizeof(_PreHeader); // Good MS-custom: always store the size of the header in your file, so you can add extra fields to the end later
	uint32_t size;				// Amount of data after header
	uint32_t loop;				// Offset in file to loop to
	uint8_t version;			// Including a version number may be a good idea
	uint8_t nrOfSN76489;
	uint8_t nrOfSAA1099;
	uint8_t nrOfAY8930;
	uint8_t nrOfYM3812;
	uint8_t nrOfYMF262;
	uint8_t nrOfMIDI;
} PreHeader;

extern PreHeader preHeader;

uint16_t GetCommandLengthCount(uint16_t chip, uint16_t type, uint16_t *pLength);
void OutputCommands(FILE* pOut);
void ClearCommands(void);
void AddCommand(uint16_t chip, uint16_t type, uint8_t cmd, FILE *pOut);
void AddCommandMulti(uint16_t chip, uint16_t type, uint8_t cmd1, uint8_t cmd2, FILE *pOut);
void AddCommandBuffer(uint16_t chip, uint16_t type, uint8_t far* pCmds, uint16_t length, FILE *pOut);
void AddDelay(uint32_t delay, FILE *pOut);

#endif /* _PREPROCESS_H_ */
