#include "PreProcess.h"

PreHeader preHeader = {
	{'P','r','e','V'},	// marker
	sizeof(PreHeader),	// headerLen
	0,					// size
	0,					// loop
	0x01,				// version
	0,					// nrOfSN76489;
	0,					// nrOfSAA1099;
	0,					// nrOfAY8930;
	0,					// nrOfYM3812;
	0,					// nrOfYMF262;
	0,					// nrOfMIDI;
};

// Buffer format:
// uint16_t delay;
// uint8_t data_count;
// uint8_t data[data_count]

uint8_t commands[MAX_MULTICHIP][NUM_CHIPS][(MAX_COMMANDS*MAX_COMMAND_SIZE)+1];	// We currently support a max count of 255 commands, and largest is 2 byte commands, count is stored first
uint8_t* pCommands[MAX_MULTICHIP][NUM_CHIPS];

uint16_t GetCommandLengthCount(uint16_t chip, uint16_t type, uint16_t *pLength)
{
	uint16_t count;
	uint16_t length = pCommands[chip][type] - commands[chip][type];
	if (pLength != NULL)
		*pLength = length;
	
	count = length - 1;
	
	// Adjust count for chips that need multiple bytes per command
	switch (type)
	{
		case SAA1099:
		case AY8930:
		case YM3812:
		case YMF262PORT0:
		case YMF262PORT1:
			count >>= 1;
			break;
	}

	return count;	
}

void OutputCommands(FILE* pOut)
{
	uint16_t count, length, i;

	for (i = 0; i < preHeader.nrOfSN76489; i++)
	{
		count = GetCommandLengthCount(i, SN76489, &length);

		commands[i][SN76489][0] = count;
		fwrite(commands[i][SN76489], length, 1, pOut);
	}
		
	for (i = 0; i < preHeader.nrOfSAA1099; i++)
	{
		count = GetCommandLengthCount(i, SAA1099, &length);

		commands[i][SAA1099][0] = count;
		fwrite(commands[i][SAA1099], length, 1, pOut);
	}
				
	for (i = 0; i < preHeader.nrOfAY8930; i++)
	{
		count = GetCommandLengthCount(i, AY8930, &length);

		commands[i][AY8930][0] = count;
		fwrite(commands[i][AY8930], length, 1, pOut);
	}

	for (i = 0; i < preHeader.nrOfYM3812; i++)
	{
		count = GetCommandLengthCount(i, YM3812, &length);

		commands[i][YM3812][0] = count;
		fwrite(commands[i][YM3812], length, 1, pOut);
	}

	for (i = 0; i < preHeader.nrOfYMF262; i++)
	{
		// First port 1 commands (includes global OPL3 config registers)
		count = GetCommandLengthCount(i, YMF262PORT1, &length);

		commands[i][YMF262PORT1][0] = count;
		fwrite(commands[i][YMF262PORT1], length, 1, pOut);
		
		// Then port 0 commands
		count = GetCommandLengthCount(i, YMF262PORT0, &length);

		commands[i][YMF262PORT0][0] = count;
		fwrite(commands[i][YMF262PORT0], length, 1, pOut);

	}
	
	for (i = 0; i < preHeader.nrOfMIDI; i++)
	{
		count = GetCommandLengthCount(i, MIDI, &length);
	
		commands[i][MIDI][0] = count;
		fwrite(commands[i][MIDI], length, 1, pOut);
	}
}

void ClearCommands(void)
{
	uint16_t i, j;
	
	for (i = 0; i < MAX_MULTICHIP; i++)
		for (j = 0; j < NUM_CHIPS; j++)
			pCommands[i][j] = commands[i][j] + 1;
}

void AddCommand(uint16_t chip, uint16_t type, uint8_t cmd, FILE *pOut)
{
	uint16_t count = GetCommandLengthCount(chip, type, NULL);
	
	// If we exceed 255 commands, we need to flush, because we cannot handle more commands
	if (count >= 255)
	{
		uint16_t firstDelay = 1;
		
		// First write delay value
		fwrite(&firstDelay, sizeof(firstDelay), 1, pOut);
				
		// Now output commands
		OutputCommands(pOut);
		ClearCommands();
	}
	
	// Add new command
	*pCommands[chip][type]++ = cmd;
}

void AddCommandMulti(uint16_t chip, uint16_t type, uint8_t cmd1, uint8_t cmd2, FILE *pOut)
{
	uint16_t count = GetCommandLengthCount(chip, type, NULL);
	
	// If we exceed 255 commands, we need to flush, because we cannot handle more commands
	if (count >= 255)
	{
		uint16_t firstDelay = 1;
		
		// First write delay value
		fwrite(&firstDelay, sizeof(firstDelay), 1, pOut);
				
		// Now output commands
		OutputCommands(pOut);
		ClearCommands();
	}
	
	// Add new command
	*pCommands[chip][type]++ = cmd1;
	*pCommands[chip][type]++ = cmd2;
}

void AddCommandBuffer(uint16_t chip, uint16_t type, uint8_t far* pCmds, uint16_t length, FILE *pOut)
{
	uint16_t i;
	
	for (i = 0; i < length; i++)
		AddCommand(chip, type, pCmds[i], pOut);
}

void AddDelay(uint32_t delay, FILE *pOut)
{
	// Break up into multiple delays with no notes
	while (delay > 0)
	{
		uint16_t firstDelay;
		
		if (delay >= 65536L)
		{
			firstDelay = 0;
			delay -= 65536L;
		}
		else
		{
			firstDelay = delay;
			delay = 0;
		}
	
		// First write delay value
		fwrite(&firstDelay, sizeof(firstDelay), 1, pOut);
		
		// Now output commands
		OutputCommands(pOut);
		
		// Reset command buffers
		// (Next delays will get 0 notes exported
		ClearCommands();
	}
}
