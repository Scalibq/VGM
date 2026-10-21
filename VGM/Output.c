#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dos.h>
#include <conio.h>
#include <math.h>
#include <malloc.h>
#include <string.h>
#include "Types.h"
#include "8253.h"
#include "IBMPC.h"
#include "MPU401.h"
#include "IMFC.h"
#include "SB.h"
#include "DBS2P.h"
#include "OPL2LPT.h"
#include "OPL3LPT.h"
#include "TNDLPT.h"
#include "CMSLPT.h"
#include "Output.h"
#include "PreProcess.h"
#include "PrePlayer.h"
#include "PreProcessMIDI.h"
#include "MIDI.h"
#include "32bit.h"

// Dummy routines
void DummyInit(OutputDevice* pDev) {}
#define DummyDeInit DummyInit

// Single chip, single byte command
// For SN76489, MIDI
#define SkipSN76489 Skip1x1
#define SkipMIDI Skip1x1
uint8_t huge* Skip1x1(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count = *pBuf++;
	pBuf += count;

	return pBuf;
}

// Single chip, two byte command
// For SAA1099, AY8930, YM3812, YM2151, YM2203
#define SkipSAA1099 Skip1x2
#define SkipAY8930 Skip1x2
#define SkipYM3812 Skip1x2
#define SkipYM2151 Skip1x2
#define SkipYM2203 Skip1x2
uint8_t huge* Skip1x2(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint16_t count = *pBuf++;
	pBuf += (count + count);

	return pBuf;
}

// Dual chip, two byte command
// For YMF262, YM2608
#define SkipYMF262 Skip2x2
#define SkipYM2608 Skip2x2
uint8_t huge* Skip2x2(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint16_t count = *pBuf++;
	pBuf += (count + count);

	count = *pBuf++;
	pBuf += (count + count);

	return pBuf;
}

// Dummy devices
#define DummySN76489 Dummy1x1
#define DummyMIDI Dummy1x1
#define DummySAA1099 Dummy1x2
#define DummyAY8930 Dummy1x2
#define DummyYM3812 Dummy1x2
#define DummyYM2151 Dummy1x2
#define DummyYM2203 Dummy1x2
#define DummyYMF262 Dummy2x2
#define DummyYM2608 Dummy2x2

OutputDevice Dummy1x1 = { DummyInit, DummyDeInit, Skip1x1 };
OutputDevice Dummy1x2 = { DummyInit, DummyDeInit, Skip1x2 };
OutputDevice Dummy2x2 = { DummyInit, DummyDeInit, Skip2x2 };

// Device list, default to dummy output
OutputDevice* pSN76489[MAX_MULTICHIP] = { &DummySN76489, &DummySN76489 };
OutputDevice* pSAA1099[MAX_MULTICHIP] = { &DummySAA1099, &DummySAA1099 };
OutputDevice* pAY8930[MAX_MULTICHIP] = { &DummyAY8930, &DummyAY8930 };
OutputDevice* pOPL2[MAX_MULTICHIP] = { &DummyYM3812, &DummyYM3812 };
OutputDevice* pOPL3[MAX_MULTICHIP] = { &DummyYMF262, &DummyYMF262 };
OutputDevice* pMIDI[MAX_MULTICHIP] = { &DummyMIDI, &DummyMIDI };
OutputDevice* pYM2151[MAX_MULTICHIP] = { &DummyYM2151, &DummyYM2151 };
OutputDevice* pYM2203[MAX_MULTICHIP] = { &DummyYM2203, &DummyYM2203 };
OutputDevice* pYM2608[MAX_MULTICHIP] = { &DummyYM2608, &DummyYM2608 };

void SetOPL3(OutputDevice* pDev, uint16_t base)
{
	OPL3Dev* pOPL3 = (OPL3Dev*)pDev;

	pOPL3->port0.command = base;
	pOPL3->port0.data = base + 1;
	pOPL3->port1.command = base + 2;
	pOPL3->port1.data = base + 3;
	
	pOPL3->port0.command = base;
	pOPL3->port0.data = base + 1;
	pOPL3->port1.command = base + 2;
	pOPL3->port1.data = base + 3;

	// TODO
	/*
	if (opl322)
		SetYMF262(1, 0);
	else
		SetYMF262(0, 0);
	*/
}

// Special SysEx message, 'Turn General MIDI System On'
uint8_t GMReset[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };

void InitMPU401(OutputDevice* pDev)
{
	uint8_t c;

	MPUDev* pMPU = (MPUDev*)pDev;
	
	set_uart(pMPU->command);
	
	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		put_mpu_out(pMPU->command, 0xB0 + c);
		put_mpu_out(pMPU->command, 123);
		
		// Send All Sound Off
		put_mpu_out(pMPU->command, 0xB0 + c);
		put_mpu_out(pMPU->command, 120);
	}
	
	OutputMIDI(pMPU->command, GMReset, _countof(GMReset));
}

void CloseMPU401(OutputDevice* pDev)
{
	uint8_t c;
	
	MPUDev* pMPU = (MPUDev*)pDev;

	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		put_mpu_out(pMPU->command, 0xB0 + c);
		put_mpu_out(pMPU->command, 123);
		
		// Send All Sound Off
		put_mpu_out(pMPU->command, 0xB0 + c);
		put_mpu_out(pMPU->command, 120);
	}
	
	OutputMIDI(pMPU->command, GMReset, _countof(GMReset));
	
	reset_mpu(pMPU->command);
}

void InitIMFCAll(OutputDevice* pDev)
{
	static uint8_t SetConfig[] = { 0xF0, 0x43, 0x75, 0x00, 0x10, 0x22, 17, 0xF7 };
	
	uint8_t c;

	IMFCDev* pIMFC = (IMFCDev*)pDev;
	
	InitIMFC(pIMFC->command, IMFC_MUSIC_MODE);
	
	// Turn all notes off
	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		WriteDataToIMFC(pIMFC->command, 0xB0 + c);
		WriteDataToIMFC(pIMFC->command, 123);
		
		// Reset all controllers
		WriteDataToIMFC(pIMFC->command, 0xB0 + c);
		WriteDataToIMFC(pIMFC->command, 121);
		
		// Send All Sound Off
		WriteDataToIMFC(pIMFC->command, 0xB0 + c);
		WriteDataToIMFC(pIMFC->command, 120);
	}
	
	// Set MONO 8 configuration (config 17)
	OutputMIDI(pIMFC->command, SetConfig, _countof(SetConfig));
}

void CloseIMFC(OutputDevice* pDev)
{
	uint8_t c;

	IMFCDev* pIMFC = (IMFCDev*)pDev;
	
	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		WriteDataToIMFC(pIMFC->command, 0xB0 + c);
		WriteDataToIMFC(pIMFC->command, 123);
		
		// Reset all controllers
		WriteDataToIMFC(pIMFC->command, 0xB0 + c);
		WriteDataToIMFC(pIMFC->command, 121);

		// Send All Sound Off
		WriteDataToIMFC(pIMFC->command, 0xB0 + c);
		WriteDataToIMFC(pIMFC->command, 120);
	}
	
	// Music Card Message (1e5 - Reboot)
	//WriteCommandToIMFC(pIMFC->command, 0xe5);
}

void InitSB(OutputDevice* pDev)
{
	uint8_t c;

	SBMIDIDev* pSB = (SBMIDIDev*)pDev;
	
	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		WriteDSP(pSB->base, 0x38);
		WriteDSP(pSB->base, 0xB0 + c);
		WriteDSP(pSB->base, 0x38);
		WriteDSP(pSB->base, 123);
		
		// Send All Sound Off
		WriteDSP(pSB->base, 0x38);
		WriteDSP(pSB->base, 0xB0 + c);
		WriteDSP(pSB->base, 0x38);
		WriteDSP(pSB->base, 120);
	}

	OutputMIDI(pSB->base, GMReset, _countof(GMReset));
}

void CloseSB(OutputDevice* pDev)
{
	uint8_t c;
		
	SBMIDIDev* pSB = (SBMIDIDev*)pDev;

	// For all channels
	for (c = 0; c < 16; c++)
	{
		// Send All Notes Off
		WriteDSP(pSB->base, 0x38);
		WriteDSP(pSB->base, 0xB0 + c);
		WriteDSP(pSB->base, 0x38);
		WriteDSP(pSB->base, 123);
		
		// Send All Sound Off
		WriteDSP(pSB->base, 0x38);
		WriteDSP(pSB->base, 0xB0 + c);
		WriteDSP(pSB->base, 0x38);
		WriteDSP(pSB->base, 120);
	}
	
	OutputMIDI(pSB->base, GMReset, _countof(GMReset));
	
	ResetDSP(pSB->base);
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

void InitPCjrAudio(OutputDevice* pDev)
{
	/*
	uint8_t mplx;
	
	mplx = inp(SNMplxr);
	mplx |= 0x60;	// set bits 6 and 5 to route SN audio through multiplexor
	outp(SNMplxr, mplx);
	*/
	// Audio Multiplexer is Int1A AH=80 AL=Audio source (0=PC speaker, 1=Cassette, 2=I/O channel "Audio In", 3=SN76496).
	union REGS in, out;

    in.h.ah = 0x80;
	in.h.al = 3;
	int86(0x1A, &in, &out);
}

void ClosePCjrAudio(OutputDevice* pDev)
{
	union REGS in, out;
	uint8_t chan, mplx;

	for (chan = 0; chan < 3; chan++)
		SetPCjrAudio(pDev, chan, 440, 15);

	// Disable noise channel
	SetPCjrAudioVolume(pDev, 3, 15);

	// Reset the multiplexor
	/*
	mplx = inp(SNMplxr);
	mplx &= 0x9C;	// clear 6 and 5 to route PC speaker through multiplexor; 1 and 0 turn off timer signal
	outp(SNMplxr, mplx);
	*/
	// Audio Multiplexer is Int1A AH=80 AL=Audio source (0=PC speaker, 1=Cassette, 2=I/O channel "Audio In", 3=SN76496).
	in.h.ah = 0x80;
	in.h.al = 0;
	int86(0x1A, &in, &out);
}

// Sets an SN voice with volume and a desired frequency
// volume is 0-15
void SetPCjrAudioPeriod(OutputDevice* pDev, uint8_t chan, uint16_t period)
{
	uint8_t command;

	SN76489Dev* pSN = (SN76489Dev*)pDev;
	
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
	outp(pSN->command, command);
	
    // build LSB
	command = period >> 4;	// isolate upper 6 bits
	//command &= 0x7F;		// clear bit 7 to indicate rest of freq
    outp(pSN->command, command);
}

// Sets an SN voice with volume
// volume is 0-15
void SetPCjrAudioVolume(OutputDevice* pDev, uint8_t chan, uint8_t volume)
{
	uint8_t command;

	SN76489Dev* pSN = (SN76489Dev*)pDev;

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
	outp(pSN->command, command);
}

// Sets an SN voice with volume and a desired frequency
// volume is 0-15
void SetPCjrAudio(OutputDevice* pDev, uint8_t chan, uint16_t freq, uint8_t volume)
{
	uint16_t period = 0;
	
	if (freq != 0)
		period = SNFreq / (32*freq);
	
	SetPCjrAudioPeriod(pDev, chan, period);

	SetPCjrAudioVolume(pDev, chan, volume);
}

void InitPCSpeaker(void)
{
	// Enable speaker and tie input pin to CTC Chan 2 by setting bits 1 and 0
	uint8_t ppi = inp(PC_PPIPORTB);
	ppi |= 0x3;
	outp(PC_PPIPORTB, ppi);
	
	outp(PC_CTCMODECMDREG, CHAN2 | AMLOBYTE | MODE0 | BINARY);
	outp(PC_CHAN2PORT, 0x01);	// Counter 2 count = 1 - terminate count quickly
}

void ClosePCSpeaker(void)
{
	// Disable speaker by clearing bits 1 and 0
	uint8_t ppi = inp(PC_PPIPORTB);
	ppi &= ~0x3;
	outp(PC_PPIPORTB, ppi);
	
	// Reset timer
	outp(PC_CTCMODECMDREG, CHAN2 | AMBOTH | MODE3 | BINARY);
	outp(PC_CHAN2PORT, 0);
	outp(PC_CHAN2PORT, 0);
}

void ResetYM3812(OutputDevice* pDev)
{
	uint16_t r, j;
	volatile uint8_t delay;
	
	// TODO
	/*
	// Write 0 to all YM3812 registers
	for (r = 0; r < 256; r++)
	{
#if defined(OPL2LPT)
		WriteOPL2LPTAddr(lpt, r);
		WriteOPL2LPTData(lpt, 0);
#else
		outp(OPL2Reg[0].command, r);
		for (j = 0; j < 6; j++)
			delay = inp(OPL2Reg[0].command);
		outp(OPL2Reg[0].data+1, 0);
		for (j = 0; j < 35; j++)
			delay = inp(OPL2Reg[0].command);
#endif
	}
	*/
}

void SetYMF262(OutputDevice* pDev, uint8_t opl3, uint8_t fourOp)
{
	uint16_t r, j;
	volatile uint8_t delay;
	
	// Enable OPL3-mode so both ports can be reset
	// Enable OPL3 mode
	// TODO
	/*
#if defined(OPL3LPT)
	WriteOPL3LPTPort1Addr(lpt, 5);
	WriteOPL3LPTData(lpt, 1);
#else
	outp(OPL3Reg[0].port1.command, 5);
	for (j = 0; j < 3; j++)
		delay = inp(OPL3Reg[0].port1.command);
	outp(OPL3Reg[0].port1.data, 1);
	for (j = 0; j < 3; j++)
		delay = inp(OPL3Reg[0].port1.command);
#endif
	
	// Write 0 to all YMF262 registers
	// First port
	for (r = 0; r < 256; r++)
	{
#if defined(OPL3LPT)
		WriteOPL3LPTPort0Addr(lpt, r);
		WriteOPL3LPTData(lpt, 0);
#else
		outp(OPL3Reg[0].port0.command, r);
		for (j = 0; j < 3; j++)
			delay = inp(OPL3Reg[0].port0.command);
		outp(OPL3Reg[0].port0.data, 0);
		for (j = 0; j < 3; j++)
			delay = inp(OPL3Reg[0].port0.command);
#endif
	}

	// Second port
	for (r = 0; r < 4; r++)
	{
#if defined(OPL3LPT)
		WriteOPL3LPTPort1Addr(lpt, r);
		WriteOPL3LPTData(lpt, 0);
#else
		outp(OPL3Reg[0].port1.command, r);
		for (j = 0; j < 3; j++)
			delay = inp(OPL3Reg[0].port1.command);
		outp(OPL3Reg[0].port1.data, 0);
		for (j = 0; j < 3; j++)
			delay = inp(OPL3Reg[0].port1.command);
#endif
	}
	
	// Skip registers 4 and 5, they enable/disable OPL3 functionalities	
	for (r = 6; r < 255; r++)
	{
#if defined(OPL3LPT)
		WriteOPL3LPTPort1Addr(lpt, r);
		WriteOPL3LPTData(lpt, 0);
#else
		outp(OPL3Reg[0].port1.command, r);
		for (j = 0; j < 3; j++)
			delay = inp(OPL3Reg[0].port1.command);
		outp(OPL3Reg[0].port1.data, 0);
		for (j = 0; j < 3; j++)
			delay = inp(OPL3Reg[0].port1.command);
#endif
	}
	
	// Enable 4-OP mode
	// Second port
#if defined(OPL3LPT)
	WriteOPL3LPTPort1Addr(lpt, 4);
	WriteOPL3LPTData(lpt, fourOp);
#else
	outp(OPL3Reg[0].port1.command, 4);
	for (j = 0; j < 3; j++)
		delay = inp(OPL3Reg[0].port1.command);
	outp(OPL3Reg[0].port1.data, fourOp);
	for (j = 0; j < 3; j++)
		delay = inp(OPL3Reg[0].port1.command);
#endif

	// Enable OPL3 mode
#if defined(OPL3LPT)
	WriteOPL3LPTPort1Addr(lpt, 5);
	WriteOPL3LPTData(lpt, opl3);
#else
	outp(OPL3Reg[0].port1.command, 5);
	for (j = 0; j < 3; j++)
		delay = inp(OPL3Reg[0].port1.command);
	outp(OPL3Reg[0].port1.data, opl3);
	for (j = 0; j < 3; j++)
		delay = inp(OPL3Reg[0].port1.command);
#endif
	*/
}

void ResetYMF262(OutputDevice* pDev)
{
	SetYMF262(pDev, 0, 0);
}

// Playback routines
uint8_t huge* PlaySN76489(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;

	SN76489Dev* pSN = (SN76489Dev*)pDev;
	
	// Get note data
	count = *pBuf++;
	while (count--)
		outp(pSN->command, *pBuf++);

	return pBuf;
}

uint8_t huge* PlayTNDLPT(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;

	TNDLPTDev* pTNDLPT = (TNDLPTDev*)pDev;
	
	// Get note data
	count = *pBuf++;
	while (count--)
		WriteTNDLPT(pTNDLPT->base, *pBuf++);

	return pBuf;
}

uint8_t huge* PlaySAA1099(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;

	SAA1099Dev* pSAA = (SAA1099Dev*)pDev;

	count = *pBuf++;

	while (count--)
	{
		outp(pSAA->command, *pBuf++);
		outp(pSAA->data, *pBuf++);
	}

	return pBuf;
}

uint8_t huge* PlayCMSLPTPort0(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;

	CMSLPTDev* pCMSLPT = (CMSLPTDev*)pDev;

	count = *pBuf++;

	while (count--)
	{
		WriteCMSLPTPort0Addr(pCMSLPT->base, *pBuf++);
		WriteCMSLPTPort0Data(pCMSLPT->base, *pBuf++);
	}

	return pBuf;
}

uint8_t huge* PlayCMSLPTPort1(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;

	CMSLPTDev* pCMSLPT = (CMSLPTDev*)pDev;

	count = *pBuf++;

	while (count--)
	{
		WriteCMSLPTPort1Addr(pCMSLPT->base, *pBuf++);
		WriteCMSLPTPort1Data(pCMSLPT->base, *pBuf++);
	}

	return pBuf;
}

uint8_t huge* PlayAY8930(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;

	AY8930Dev* pAY = (AY8930Dev*)pDev;

	count = *pBuf++;

	while (count--)
	{
		outp(pAY->command, *pBuf++);
		outp(pAY->data, *pBuf++);
	}

	return pBuf;
}

uint8_t huge* PlayYM3812(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;
	uint16_t j;
	volatile uint8_t delay;
		
	OPL2Dev* pOPL2 = (OPL2Dev*)pDev;

	count = *pBuf++;

	while (count--)
	{
		outp(pOPL2->command, *pBuf++);
		for (j = 0; j < 6; j++)
			delay = inp(pOPL2->command);
		outp(pOPL2->data, *pBuf++);
		for (j = 0; j < 35; j++)
			delay = inp(pOPL2->command);
	}

	return pBuf;
}

uint8_t huge* PlayDualOPL2OnOPL3(OutputDevice* pDev, uint8_t huge* pBuf)
{
	// TODO
	uint8_t count;
	uint16_t i, j;
	volatile uint8_t delay;
		
	OPL2Dev* pOPL2 = (OPL2Dev*)pDev;

	count = *pBuf++;

	/*
	while (count--)
	{
		// OPL3-compatible mode for dual OPL2
		uint8_t idx, data;
		uint16_t o;
		idx = *pBuf++;
		data = *pBuf++;
		
		o = i >> 1;

		if (i &= 1)
		{
			// Send to port 1 of OPL3
			if ((idx == 4) || (idx == 5))
				continue;
			
			// Adjust panning
			if ((idx >= 0xC0) && (idx <= 0xC8))
			{
				data &= 0x0F;
				data |= 0x20;
			}
			
			outp(OPL3Reg[o].port1.command, idx);
			for (j = 0; j < 3; j++)
				delay = inp(OPL3Reg[o].port1.command);
			outp(OPL3Reg[o].port1.data, data);
			for (j = 0; j < 3; j++)
				delay = inp(OPL3Reg[o].port1.command);
		}
		else
		{
			// Adjust panning
			if ((idx >= 0xC0) && (idx <= 0xC8))
			{
				data &= 0x0F;
				data |= 0x10;
			}

			// Send to port 0 of OPL3
			outp(OPL3Reg[o].port0.command, idx);
			for (j = 0; j < 3; j++)
				delay = inp(OPL3Reg[o].port0.command);
			outp(OPL3Reg[o].port0.data, data);
			for (j = 0; j < 3; j++)
				delay = inp(OPL3Reg[o].port0.command);
		}
	}
	*/
	
	return pBuf;
}

uint8_t huge* PlayOPL2LPT(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;
		
	OPL2LPTDev* pOPL2LPT = (OPL2LPTDev*)pDev;

	count = *pBuf++;

	while (count--)
	{
		WriteOPL2LPTAddr(pOPL2LPT->base, *pBuf++);
		WriteOPL2LPTData(pOPL2LPT->base, *pBuf++);
	}

	return pBuf;
}

uint8_t huge* PlayYMF262(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;
	uint16_t j;
	volatile uint8_t delay;

	OPL3Dev* pOPL3 = (OPL3Dev*)pDev;
		
	// First port 1 commands
	count = *pBuf++;
		
	while (count--)
	{
		outp(pOPL3->port1.command, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pOPL3->port1.command);
		outp(pOPL3->port1.data, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pOPL3->port1.command);
	}

	// Then port 0 commands
	count = *pBuf++;
		
	while (count--)
	{
		outp(pOPL3->port0.command, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pOPL3->port0.command);
		outp(pOPL3->port0.data, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pOPL3->port0.command);
	}

	return pBuf;
}

uint8_t huge* PlayOPL3LPT(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;

	OPL3LPTDev* pOPL3LPT = (OPL3LPTDev*)pDev;
		
	// First port 1 commands
	count = *pBuf++;
		
	while (count--)
	{
		WriteOPL3LPTPort1Addr(pOPL3LPT->base, *pBuf++);
		WriteOPL3LPTData(pOPL3LPT->base, *pBuf++);
	}

	// Then port 0 commands
	count = *pBuf++;
		
	while (count--)
	{
		WriteOPL3LPTPort0Addr(pOPL3LPT->base, *pBuf++);
		WriteOPL3LPTData(pOPL3LPT->base, *pBuf++);
	}

	return pBuf;
}

uint8_t huge* PlayMIDI(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;
	uint16_t i;
	
	count = *pBuf++;

	// TODO
#if defined(MPU401)		
	OutputMIDI(MPUReg[i].command, pBuf, count);
#elif defined(IMFC)
	OutputMIDI(IMFCReg[i], pBuf, count);
#elif defined(SB)
	OutputMIDI(SBReg[i], pBuf, count);
#elif defined(DBS2P)
	OutputMIDI(lpt, pBuf, count);
#endif

	pBuf += count;

	return pBuf;
}

uint8_t huge* PlayYM2151(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;
	uint16_t j;
	volatile uint8_t delay;

	YM2151Dev* pYM = (YM2151Dev*)pDev;
	
	count = *pBuf++;

	while (count--)
	{
		outp(pYM->command, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pYM->command);
		outp(pYM->data, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pYM->command);
	}

	return pBuf;
}

uint8_t huge* PlayYM2203(OutputDevice* pDev, uint8_t huge* pBufid)
{
	uint8_t count;
	uint16_t j;
	volatile uint8_t delay;

	YM2203Dev* pYM = (YM2203Dev*)pDev;

	count = *pBuf++;

	while (count--)
	{
		outp(pYM->command, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pYM->command);
		outp(pYM->data, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pYM->command);
	}

	return pBuf;
}

uint8_t huge* PlayYM2608(OutputDevice* pDev, uint8_t huge* pBuf)
{
	uint8_t count;
	uint16_t j;
	volatile uint8_t delay;

	YM2608Dev* pYM = (YM2608Dev*)pDev;
	
	// First port 1 commands
	count = *pBuf++;
		
	while (count--)
	{
		outp(pYM->port1.command, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pYM->port1.command);
		outp(pYM->port1.data, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pYM->port1.command);
	}

	// Then port 0 commands
	count = *pBuf++;
		
	while (count--)
	{
		outp(pYM->port0.command, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pYM->port0.command);
		outp(pYM->port0.data, *pBuf++);
		for (j = 0; j < 3; j++)
			delay = inp(pYM->port0.command);
	}

	return pBuf;
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
