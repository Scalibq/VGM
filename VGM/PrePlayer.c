#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dos.h>
#include <conio.h>
#include <math.h>
#include <malloc.h>
#include <string.h>
#include "8253.h"
#include "8259A.h"
#include "MPU401.h"
#include "IMFC.h"
#include "SB.h"
#include "DBS2P.h"
#include "OPL2LPT.h"
#include "Endianness.h"
#include "PreProcess.h"
#include "PrePlayer.h"
#include "PreProcessMIDI.h"

#define SNFreq 3579540

uint16_t SNReg[MAX_MULTICHIP] = { 0xC0, 0xC0 };
uint16_t SAAReg[MAX_MULTICHIP] = { 0x210, 0x212 };
uint16_t AYReg[MAX_MULTICHIP] = { 0x220, 0x220 };
uint16_t OPL2Reg[MAX_MULTICHIP] = { 0x388, 0x388 };
uint16_t OPL3Reg[MAX_MULTICHIP*2] = { 0x220, 0x222, 0x220, 0x222 };	// Special case: there are two separate ports for the chip
uint16_t MPUReg[MAX_MULTICHIP] = { 0x330, 0x330 };
uint16_t IMFCReg[MAX_MULTICHIP] = { 0x2A20, 0x2A20 };
uint16_t SBReg[MAX_MULTICHIP] = { 0x220, 0x220 };

extern uint16_t lpt;

// Buffer format:
// uint16_t delay;
// uint8_t data_count;
// uint8_t data[data_count]
uint8_t huge* pPreprocessed;
uint8_t huge* pBuf;
uint8_t huge* pEndBuf;

void LoadPreprocessed(const char* pFileName)
{
	FILE* pFile = fopen(pFileName, "rb");
	
	// Load from file
	_farfread(&preHeader, sizeof(preHeader), 1, pFile);
	
	pPreprocessed = farmalloc(preHeader.size);
	
	printf("Preprocessed size: %lu\n", preHeader.size);
	
	printf("# SN76489: %u\n", preHeader.nrOfSN76489);
	printf("# SAA1099: %u\n", preHeader.nrOfSAA1099);
	printf("# AY8930: %u\n", preHeader.nrOfAY8930);
	printf("# YM3812: %u\n", preHeader.nrOfYM3812);
	printf("# YMF262: %u\n", preHeader.nrOfYMF262);
	printf("# MIDI: %u\n", preHeader.nrOfMIDI);
	
	// Search to start of data
	fseek(pFile, preHeader.headerLen, SEEK_SET);

	_farfread(pPreprocessed, preHeader.size, 1, pFile);
	fclose(pFile);

	pEndBuf = pPreprocessed + preHeader.size;
	
	// Set playing position to start of buffer
	pBuf = pPreprocessed;
}

void PlayData(void)
{
	uint8_t count;
	uint16_t i, j;
	volatile uint8_t delay;
	
	// Get note data
	for (i = 0; i < preHeader.nrOfSN76489; i++)
	{
		count = *pBuf++;
		while (count--)
			outp(SNReg[i], *pBuf++);
	}
		
	for (i = 0; i < preHeader.nrOfSAA1099; i++)
	{
		count = *pBuf++;
	
		while (count--)
		{
			outp(SAAReg[i]+1, *pBuf++);
			outp(SAAReg[i], *pBuf++);
		}
	}
				
	for (i = 0; i < preHeader.nrOfAY8930; i++)
	{
		count = *pBuf++;
	
		while (count--)
		{
			outp(AYReg[i], *pBuf++);
			outp(AYReg[i]+4, *pBuf++);
		}
	}
		
	for (i = 0; i < preHeader.nrOfYM3812; i++)
	{
		count = *pBuf++;
	
		while (count--)
		{
#if defined(OPL2LPT)
			WriteOPL2LPTAddr(lpt, *pBuf++);
			WriteOPL2LPTData(lpt, *pBuf++);
#else
			outp(OPL2Reg[i], *pBuf++);
			for (j = 0; j < 6; j++)
				delay = inp(OPL2Reg[i]);
			outp(OPL2Reg[i]+1, *pBuf++);
			for (j = 0; j < 35; j++)
				delay = inp(OPL2Reg[i]);
#endif
		}
	}
		
	for (i = 0; i < preHeader.nrOfYMF262; i++)
	{
		// First port 1 commands
		count = *pBuf++;
			
		while (count--)
		{
			outp(OPL3Reg[i*2 + 1], *pBuf++);
			for (j = 0; j < 6; j++)
				delay = inp(OPL3Reg[i*2 + 1]);
			outp(OPL3Reg[i*2 + 1]+1, *pBuf++);
			for (j = 0; j < 35; j++)
				delay = inp(OPL3Reg[i*2 + 1]);
		}

		// Then port 0 commands
		count = *pBuf++;
			
		while (count--)
		{
			outp(OPL3Reg[i*2], *pBuf++);
			for (j = 0; j < 6; j++)
				delay = inp(OPL3Reg[i*2]);
			outp(OPL3Reg[i*2]+1, *pBuf++);
			for (j = 0; j < 35; j++)
				delay = inp(OPL3Reg[i*2]);
		}
	}
	
	for (i = 0; i < preHeader.nrOfMIDI; i++)
	{
		count = *pBuf++;

#if defined(MPU401)		
		OutputMIDI(MPUReg[i], pBuf, count);
#elif defined(IMFC)
		OutputMIDI(IMFCReg[i], pBuf, count);
#elif defined(SB)
		OutputMIDI(SBReg[i], pBuf, count);
#elif defined(DBS2P)
		OutputMIDI(lpt, pBuf, count);
#endif

		pBuf += count;
	}
}

void OutputMIDI(uint16_t base, uint8_t huge* pBuf, uint16_t len)
{
	uint16_t i;
	
	for (i = 0; i < len; i++)
	{
#if defined(MPU401)
		put_mpu_out(base, pBuf[i]);
#elif defined(IMFC)
		WriteDataToIMFC(base, pBuf[i]);
#elif defined(SB)
		WriteDSP(base, 0x38);
		WriteDSP(base, pBuf[i]);
#elif defined(DBS2P)
		WriteDBS2PData(base, pBuf[i]);
#endif
	}
}

// Special SysEx message, 'Turn General MIDI System On'
uint8_t GMReset[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };

void InitMPU401(void)
{
	uint8_t c;
	
	set_uart(MPUReg[0]);
	
	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		put_mpu_out(MPUReg[0], 0xB0 + c);
		put_mpu_out(MPUReg[0], 123);
		
		// Send All Sound Off
		put_mpu_out(MPUReg[0], 0xB0 + c);
		put_mpu_out(MPUReg[0], 120);
	}
	
	OutputMIDI(MPUReg[0], GMReset, _countof(GMReset));
}

void CloseMPU401(void)
{
	uint8_t c;
	
	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		put_mpu_out(MPUReg[0], 0xB0 + c);
		put_mpu_out(MPUReg[0], 123);
		
		// Send All Sound Off
		put_mpu_out(MPUReg[0], 0xB0 + c);
		put_mpu_out(MPUReg[0], 120);
	}
	
	OutputMIDI(MPUReg[0], GMReset, _countof(GMReset));
	
	reset_mpu(MPUReg[0]);
}

void InitIMFCAll(void)
{
	static uint8_t SetConfig[] = { 0xF0, 0x43, 0x75, 0x00, 0x10, 0x22, 17, 0xF7 };
	
	uint8_t c;
	
	InitIMFC(IMFCReg[0], IMFC_MUSIC_MODE);
	
	// Turn all notes off
	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		WriteDataToIMFC(IMFCReg[0], 0xB0 + c);
		WriteDataToIMFC(IMFCReg[0], 123);
		
		// Reset all controllers
		WriteDataToIMFC(IMFCReg[0], 0xB0 + c);
		WriteDataToIMFC(IMFCReg[0], 121);
		
		// Send All Sound Off
		WriteDataToIMFC(IMFCReg[0], 0xB0 + c);
		WriteDataToIMFC(IMFCReg[0], 120);
	}
	
	// Set MONO 8 configuration (config 17)
	OutputMIDI(IMFCReg[0], SetConfig, _countof(SetConfig));
}

void CloseIMFC(void)
{
	uint8_t c;
	
	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		WriteDataToIMFC(IMFCReg[0], 0xB0 + c);
		WriteDataToIMFC(IMFCReg[0], 123);
		
		// Reset all controllers
		WriteDataToIMFC(IMFCReg[0], 0xB0 + c);
		WriteDataToIMFC(IMFCReg[0], 121);

		// Send All Sound Off
		WriteDataToIMFC(IMFCReg[0], 0xB0 + c);
		WriteDataToIMFC(IMFCReg[0], 120);
	}
	
	// Music Card Message (1e5 - Reboot)
	//WriteCommandToIMFC(IMFCReg[0], 0xe5);
}

void InitSB(void)
{
	uint8_t c;
	
	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		WriteDSP(SBReg[0], 0x38);
		WriteDSP(SBReg[0], 0xB0 + c);
		WriteDSP(SBReg[0], 0x38);
		WriteDSP(SBReg[0], 123);
		
		// Send All Sound Off
		WriteDSP(SBReg[0], 0x38);
		WriteDSP(SBReg[0], 0xB0 + c);
		WriteDSP(SBReg[0], 0x38);
		WriteDSP(SBReg[0], 120);
	}

	OutputMIDI(SBReg[0], GMReset, _countof(GMReset));
}

void CloseSB(void)
{
	uint8_t c;
		
	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		WriteDSP(SBReg[0], 0x38);
		WriteDSP(SBReg[0], 0xB0 + c);
		WriteDSP(SBReg[0], 0x38);
		WriteDSP(SBReg[0], 123);
		
		// Send All Sound Off
		WriteDSP(SBReg[0], 0x38);
		WriteDSP(SBReg[0], 0xB0 + c);
		WriteDSP(SBReg[0], 0x38);
		WriteDSP(SBReg[0], 120);
	}
	
	OutputMIDI(SBReg[0], GMReset, _countof(GMReset));
	
	ResetDSP(SBReg[0]);
}

typedef struct {
	uint8_t channel;
	uint8_t program;
} ChannelProgram;

ChannelProgram channelPart[] = {
	{ 2, 68 },
	{ 3, 48 },
	{ 4, 95 },
	{ 5, 78 },
	{ 6, 41 },
	{ 7, 3 },
	{ 8, 110 },
	{ 9, 122 },
	{ 10, 127 },
	{ 1, 0 },
	{ 11, 0 },
	{ 12, 0 },
	{ 13, 0 },
	{ 14, 0 },
	{ 15, 0 },
	{ 16, 0 }
};

void InitDBS2P(uint16_t base)
{
	volatile uint8_t delay;
	
	// Enable parallel mode
	WriteDBS2PCtrl(base, 0x3F);
	
	// Discard reply byte
	delay = ReadDBS2PData(base);

	OutputMIDI(base, GMReset, _countof(GMReset));
	
	// Perform MT-32 mode for DreamBlaster S2(P)
	if (mt32Mode)
	{
		uint16_t i;

		// Insert MT-32 command, CC 0 = 127
		static uint8_t MT32[] = { CC, 0, 127, PC, 0 };
		static uint8_t MT32_PB[] = { 
			CC, 0x65, 0x00, // 101 00 MSB
			CC, 0x64, 0x00, // 100 00 LSB
			CC, 0x06, 0x0C, //  06 12 MSB
			CC, 0x26, 0x00  //  38 00 LSB
		};
		
		for (i = 0; i < _countof(channelPart); i++)
		{
			// Set proper channel
			MT32[0] = CC | (channelPart[i].channel - 1);
			MT32[3] = PC | (channelPart[i].channel - 1);
			
			// Set proper program
			MT32[4] = channelPart[i].program;
					
			OutputMIDI(base, MT32, _countof(MT32));
		}
		
		// Adjust pitch-bend range
		for (i = 0; i < 16; i++)
		{
			// Set proper channel
			MT32_PB[0] = CC | i;
			MT32_PB[3] = CC | i;
			MT32_PB[6] = CC | i;
			MT32_PB[9] = CC | i;
			
			OutputMIDI(base, MT32_PB, _countof(MT32_PB));
		}
	}
}

void CloseDBS2P(uint16_t base)
{
	// ??
}

void InitPCjrAudio(void)
{
	/*
	uint8_t mplx;
	
	mplx = inp(SNMplxr);
	mplx |= 0x60;	// set bits 6 and 5 to route SN audio through multiplexor
	outp(SNMplxr, mplx);
	*/
	// Audio Multiplexer is Int1A AH=80 AL=Audio source (0=PC speaker, 1=Cassette, 2=I/O channel "Audio In", 3=SN76496).
	union REGS in, out;

	in.h.al = 3;
	int86(0x1A, &in, &out);
}

void ClosePCjrAudio(void)
{
	union REGS in, out;
	uint8_t chan, mplx;

	for (chan = 0; chan < 3; chan++)
		SetPCjrAudio(chan,440,15);

	// Disable noise channel
	SetPCjrAudioVolume(3,15);

	// Reset the multiplexor
	/*
	mplx = inp(SNMplxr);
	mplx &= 0x9C;	// clear 6 and 5 to route PC speaker through multiplexor; 1 and 0 turn off timer signal
	outp(SNMplxr, mplx);
	*/
	// Audio Multiplexer is Int1A AH=80 AL=Audio source (0=PC speaker, 1=Cassette, 2=I/O channel "Audio In", 3=SN76496).
	in.h.al = 0;
	int86(0x1A, &in, &out);
}

// Sets an SN voice with volume and a desired frequency
// volume is 0-15
void SetPCjrAudioPeriod(uint8_t chan, uint16_t period)
{
	uint8_t command;
	
/*
  To set a channel, we first send frequency, then volume.
  Frequency:
  76543210 76543210
  1                 - set bit to tell chip we are selecting a register
   xx0              - set channel.  4, 2, and 0 are valid values.
      xxxx          - low 4 bits of period
           0        - clear bit to tell chip more freq. coming
            x       - unused
             xxxxxx - least sig. 6 bits of period

  Sending a word value will not work on PCjr, so send bytes individally.
  (It does work on Tandy, but we want to be nice.)

  Set attenuation (volume):

  76543210
  1                 - set bit to tell chip we are selecting a register
   xx1              - register number (valid values are 1, 3, 5, 7)
      xxxx          - 4-bit volume where 0 is full volume and 15 is silent)

*/
    // build MSB
	command = chan << 5;	// get voice reg in place
	command |= 0x80;		// tell chip we are selecting a reg
	command |= (period & 0xF);	// grab least sig 4 bits of period...
	outp(SNReg[0],command);
	
    // build LSB
	command = period >> 4;	// isolate upper 6 bits
	//command &= 0x7F;		// clear bit 7 to indicate rest of freq
    outp(SNReg[0],command);
}

// Sets an SN voice with volume
// volume is 0-15
void SetPCjrAudioVolume(uint8_t chan, uint8_t volume)
{
	uint8_t command;

/*
  Set attenuation (volume):

  76543210
  1                 - set bit to tell chip we are selecting a register
   xx1              - register number (valid values are 1, 3, 5, 7)
      xxxx          - 4-bit volume where 0 is full volume and 15 is silent)
*/
    // set the volume
	command = chan << 5;	// get voice reg in place
	command |= 0x90;	// tell chip we're selecting a reg for volume
	command |= volume;	// adjust to attenuation; register expects 0 = full, 15 = quiet
	outp(SNReg[0], command);
}

// Sets an SN voice with volume and a desired frequency
// volume is 0-15
void SetPCjrAudio(uint8_t chan, uint16_t freq, uint8_t volume)
{
	uint16_t period = 0;
	
	if (freq != 0)
		period = SNFreq / (32*freq);
	
	SetPCjrAudioPeriod(chan, period);

	SetPCjrAudioVolume(chan, volume);
}

void InitPCSpeaker(void)
{
	// Enable speaker and tie input pin to CTC Chan 2 by setting bits 1 and 0
	uint8_t ppi = inp(PPIPORTB);
	ppi |= 0x3;
	outp(PPIPORTB, ppi);
	
	outp(CTCMODECMDREG, CHAN2 | AMLOBYTE | MODE0 | BINARY);
	outp(CHAN2PORT, 0x01);	// Counter 2 count = 1 - terminate count quickly
}

void ClosePCSpeaker(void)
{
	// Disable speaker by clearing bits 1 and 0
	uint8_t ppi = inp(PPIPORTB);
	ppi &= ~0x3;
	outp(PPIPORTB, ppi);
	
	// Reset timer
	outp(CTCMODECMDREG, CHAN2 | AMBOTH | MODE3 | BINARY);
	outp(CHAN2PORT, 0);
	outp(CHAN2PORT, 0);
}

void ResetYM3812(void)
{
	uint16_t r, j;
	volatile uint8_t delay;
	
	// Write 0 to all YM3812 registers
	for (r = 0; r < 256; r++)
	{
#if defined(OPL2LPT)
		WriteOPL2LPTAddr(lpt, r);
		WriteOPL2LPTData(lpt, 0);
#else
		outp(OPL2Reg[0], r);
		for (j = 0; j < 6; j++)
			delay = inp(OPL2Reg[0]);
		outp(OPL2Reg[0]+1, 0);
		for (j = 0; j < 35; j++)
			delay = inp(OPL2Reg[0]);
#endif
	}
}

void SetYMF262(uint8_t opl3, uint8_t fourOp)
{
	uint16_t r, j;
	volatile uint8_t delay;
	
	// Enable OPL3-mode so both ports can be reset
	// Enable OPL3 mode
	outp(OPL3Reg[1], 5);
	for (j = 0; j < 6; j++)
		delay = inp(OPL3Reg[1]);
	outp(OPL3Reg[1]+1, 1);
	for (j = 0; j < 35; j++)
		delay = inp(OPL3Reg[1]);
	
	// Write 0 to all YMF262 registers
	// First port
	for (r = 0; r < 256; r++)
	{
		outp(OPL3Reg[0], r);
		for (j = 0; j < 6; j++)
			delay = inp(OPL3Reg[0]);
		outp(OPL3Reg[0]+1, 0);
		for (j = 0; j < 35; j++)
			delay = inp(OPL3Reg[0]);
	}

	// Second port
	for (r = 0; r < 4; r++)
	{
		outp(OPL3Reg[1], r);
		for (j = 0; j < 6; j++)
			delay = inp(OPL3Reg[1]);
		outp(OPL3Reg[1]+1, 0);
		for (j = 0; j < 35; j++)
			delay = inp(OPL3Reg[1]);
	}
	
	// Skip registers 4 and 5, they enable/disable OPL3 functionalities	
	for (r = 6; r < 255; r++)
	{
		outp(OPL3Reg[1], r);
		for (j = 0; j < 6; j++)
			delay = inp(OPL3Reg[1]);
		outp(OPL3Reg[1]+1, 0);
		for (j = 0; j < 35; j++)
			delay = inp(OPL3Reg[1]);
	}
	
	// Enable 4-OP mode
	// Second port
	outp(OPL3Reg[1], 4);
	for (j = 0; j < 6; j++)
		delay = inp(OPL3Reg[1]);
	outp(OPL3Reg[1]+1, fourOp);
	for (j = 0; j < 35; j++)
		delay = inp(OPL3Reg[1]);

	// Enable OPL3 mode
	outp(OPL3Reg[1], 5);
	for (j = 0; j < 6; j++)
		delay = inp(OPL3Reg[1]);
	outp(OPL3Reg[1]+1, opl3);
	for (j = 0; j < 35; j++)
		delay = inp(OPL3Reg[1]);
}

void ResetYMF262(void)
{
	SetYMF262(0, 0);
}

