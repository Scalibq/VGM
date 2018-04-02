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
#include "stdtype.h"	// VGMFile.h uses nonstandard types, define them separately
#include "VGMFile.h"
#include "MIDI.h"
#include "DRO.h"
#include "MPU401.h"
#include "IMFC.h"
#include "SB.h"
#include "DBS2P.h"
#include "OPL2LPT.h"
#include "Endianness.h"
#include "PreProcess.h"

//#define MPU401
//#define IMFC
//#define SB
#define DBS2P
#define OPL2LPT

#define M_PI 3.1415926535897932384626433832795

#define SNFreq 3579540
#define SNMplxr 0x61	// MC14529b sound multiplexor chip in the PCjr
#define SampleRate 44100

#define DIVISOR		(55411U)	/* (1193182.0f/44100.0f) * 2048 */
#define	DIVISOR_SHIFT	(11U)

#define GETDELAY(n)	((uint32_t)(n*(uint32_t)DIVISOR) >> DIVISOR_SHIFT)
//#define GETDELAY(n)	((uint32_t)(((n*(1193182.0/44100.0))+0.5)))

uint16_t SNReg[MAX_MULTICHIP] = { 0xC0, 0xC0 };
uint16_t SAAReg[MAX_MULTICHIP] = { 0x210, 0x212 };
uint16_t AYReg[MAX_MULTICHIP] = { 0x220, 0x220 };
uint16_t OPL2Reg[MAX_MULTICHIP] = { 0x388, 0x388 };
uint16_t OPL3Reg[MAX_MULTICHIP*2] = { 0x220, 0x222, 0x220, 0x222 };	// Special case: there are two separate ports for the chip
uint16_t MPUReg[MAX_MULTICHIP] = { 0x330, 0x330 };
uint16_t IMFCReg[MAX_MULTICHIP] = { 0x2A20, 0x2A20 };
uint16_t SBReg[MAX_MULTICHIP] = { 0x220, 0x220 };

uint16_t lpt = 0x378;
uint8_t mt32Mode = 0;	// Special mode to prefix any program change with a special command for DreamBlaster S2(P) for MT-32 instruments

void SetTimerCount(uint16_t rate);

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

void InitIMFCAll()
{
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
	
	OutputMIDI(IMFCReg[0], GMReset, _countof(GMReset));
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
	
	OutputMIDI(IMFCReg[0], GMReset, _countof(GMReset));
	
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
	for (j = 0; j < 3; j++)
		delay = inp(OPL3Reg[1]);
	outp(OPL2Reg[1]+1, 1);
	for (j = 0; j < 3; j++)
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
	for (j = 0; j < 3; j++)
		delay = inp(OPL3Reg[1]);
	outp(OPL2Reg[1]+1, fourOp);
	for (j = 0; j < 3; j++)
		delay = inp(OPL3Reg[1]);

	// Enable OPL3 mode
	outp(OPL3Reg[1], 5);
	for (j = 0; j < 3; j++)
		delay = inp(OPL3Reg[1]);
	outp(OPL2Reg[1]+1, opl3);
	for (j = 0; j < 3; j++)
		delay = inp(OPL3Reg[1]);
}

void ResetYMF262(void)
{
	SetYMF262(0, 0);
}

// Waits for numTicks to elapse, where a tick is 1/PIT Frequency (~1193182)
void tickWait(uint32_t numTicks, uint32_t* pCurrentTime)
{
	uint32_t targetTime;

	targetTime = *pCurrentTime - numTicks;
	
	__asm
	{
		mov di, [pCurrentTime]
		mov dx, [di]
		mov bx, [di+2]
			
	pollLoop:
		// PIT command: Channel 0, Latch Counter, Rate Generator, Binary
		mov al, (CHAN0 or AMREAD)
		out CTCMODECMDREG, al
		// Get LSB of timer counter
		in al, CHAN0PORT
		mov cl, al
		// Get MSB of timer counter
		in al, CHAN0PORT
		mov ch, al
			
		// Handle wraparound to 32-bit counter
		cmp dx, cx
		sbb bx, 0
			
		mov dx, cx

		// while (*pCurrentTime > targetTime)
		cmp bx, word ptr [targetTime+2]
		ja pollLoop
		cmp dx, word ptr [targetTime]
		ja pollLoop
		
		mov [di], dx
		mov [di+2], bx
	}
}

// Waits for numTicks to elapse, where a tick is 1/PIT Frequency (~1193182)
void tickWaitC(uint32_t numTicks, uint32_t* pCurrentTime)
{
	uint16_t lastTime;
	uint32_t targetTime;
	
	// Get previous counter value (low 16 bits)
	lastTime = (uint16_t)*pCurrentTime;
	
	targetTime = *pCurrentTime - numTicks;
	
	do
	{
		uint16_t time;
		uint16_t* pCurTimeW = (uint16_t*)pCurrentTime;
		uint8_t* pTime = (uint8_t*)&time;
		
		// PIT command: Channel 0, Latch Counter, Rate Generator, Binary
		outp(CTCMODECMDREG, CHAN0 | AMREAD);
		
		// Get LSB of timer counter
		*pTime = inp(CHAN0PORT);
		
		// Get MSB of timer counter
		*(pTime+1) = inp(CHAN0PORT);
		
		// Handle wraparound
		if (time > lastTime)
			--(*(pCurTimeW+1));
		
		*pCurTimeW = time;
		lastTime = time;
	} while (*pCurrentTime > targetTime);
}

int keypressed(uint8_t* pChar)
{
	int ret;
	union REGPACK regs;
	regs.h.ah = 1;

	intr(0x16, &regs);
	
	ret = !(regs.x.flags & INTR_ZF);     // Check zero-flag
	
	if (ret && (pChar != NULL))
	{
		// Get keystroke
		regs.h.ah = 0;
		intr(0x16, &regs);
		*pChar = regs.h.al;
	}

	return ret;
}

volatile int playing = 1;
uint32_t delayTable[4096];

// Buffer format:
// uint16_t delay;
// uint8_t data_count;
// uint8_t data[data_count]
uint8_t huge* pPreprocessed;
uint8_t huge* pBuf;
uint8_t huge* pEndBuf;

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
		// First port 0 commands
		count = *pBuf++;
			
		while (count--)
		{
			outp(OPL3Reg[i*2], *pBuf++);
			for (j = 0; j < 3; j++)
				delay = inp(OPL3Reg[i*2]);
			outp(OPL3Reg[i*2]+1, *pBuf++);
			for (j = 0; j < 3; j++)
				delay = inp(OPL3Reg[i*2]);
		}

		// Then port 1 commands
		count = *pBuf++;
			
		while (count--)
		{
			outp(OPL3Reg[i*2 + 1], *pBuf++);
			for (j = 0; j < 3; j++)
				delay = inp(OPL3Reg[i*2 + 1]);
			outp(OPL3Reg[i*2 + 1]+1, *pBuf++);
			for (j = 0; j < 3; j++)
				delay = inp(OPL3Reg[i*2 + 1]);
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

void PlayBuffer2()
{
	uint16_t far* pW;
	uint8_t count;
	uint32_t currentTime;
	uint32_t currDelay, nextDelay;
	
	// Disable interrupts
	//_disable();
	
	// Get LSB of timer counter
	currentTime = inp(CHAN0PORT);
	
	// Get MSB of timer counter
	currentTime |= ((uint16_t)inp(CHAN0PORT)) << 8;
	
	// Count down from maximum
	currentTime |= 0xFFFF0000l;
	
	// Find the first delay command
	currDelay = 0;
	
	pW = (uint16_t far*)pBuf;
	nextDelay = *pW++;
	pBuf = (uint8_t far*)pW;
	
	while (pBuf < pEndBuf)
	{
		uint32_t targetTime;

		// Perform waiting
		// max reasonable tickWait time is 50ms, so handle larger values in slices
		if (currDelay == 0)
			currDelay = 65536L;

		//tickWait(currDelay, &currentTime);
		targetTime = currentTime - currDelay;
	
		__asm
		{
			lea di, [currentTime]
			mov dx, [di]
			mov bx, [di+2]
				
		pollLoop:
			// PIT command: Channel 0, Latch Counter, Rate Generator, Binary
			mov al, (CHAN0 or AMREAD)
			out CTCMODECMDREG, al
			// Get LSB of timer counter
			in al, CHAN0PORT
			mov cl, al
			// Get MSB of timer counter
			in al, CHAN0PORT
			mov ch, al
				
			// Handle wraparound to 32-bit counter
			cmp dx, cx
			sbb bx, 0
				
			mov dx, cx

			// while (*pCurrentTime > targetTime)
			cmp bx, word ptr [targetTime+2]
			ja pollLoop
			cmp dx, word ptr [targetTime]
			ja pollLoop
			
			mov [di], dx
			mov [di+2], bx
			
			/*
			push ds
			mov ax, seg pBuf
			mov ds, ax
			lds si, [pBuf]
		
			// Get note count
			lodsb
			test al, al
			jz endHandler
		
			// Play notes
			xor cx, cx
			mov cl, al
		
		noteLoop:
			lodsb
			out 0xC0, al
			
		endHandler:
			mov ax, seg pBuf
			mov ds, ax
			mov word ptr [pBuf], si
			pop ds
			*/
		}
		
		// Loop through all command-data
		count = *pBuf++;
	
		while (count--)
			outp(SNReg[0], *pBuf++);

		// Prefetch next delay
		currDelay = nextDelay;
		pW = (uint16_t far*)pBuf;
		nextDelay = *pW++;
		pBuf = (uint8_t far*)pW;
		
		// handle input
		if (playing == 0)
			break;
	}
	
	// Re-enable interrupts
	//_enable();
}

void PlayBuffer2C()
{
	uint16_t far* pW;
	uint32_t currentTime;
	uint16_t currDelay, nextDelay;
	uint32_t totalDelay;
	
	// Disable interrupts
	//_disable();
	
	// Get LSB of timer counter
	currentTime = inp(CHAN0PORT);
	
	// Get MSB of timer counter
	currentTime |= ((uint16_t)inp(CHAN0PORT)) << 8;
	
	// Count down from maximum
	currentTime |= 0xFFFF0000l;
	
	// Find the first delay command
	currDelay = 0;
	
	pW = (uint16_t far*)pBuf;
	nextDelay = *pW++;
	pBuf = (uint8_t far*)pW;
	
	totalDelay =  0;
	
	while (pBuf < pEndBuf)
	{
		// Perform waiting
		// max reasonable tickWait time is 50ms, so handle larger values in slices
		/*if (currDelay == 0)
			totalDelay += 65536L;
		else
		{
			totalDelay += currDelay;
			tickWaitC(totalDelay, &currentTime);
			totalDelay = 0;
		}*/
		if (currDelay == 0)
			tickWaitC(65536L, &currentTime);
		else
			tickWaitC(currDelay, &currentTime);

		PlayData();

		// Prefetch next delay
		currDelay = nextDelay;
		pW = (uint16_t far*)pBuf;
		nextDelay = *pW++;
		pBuf = (uint8_t far*)pW;
		
		// handle input
		if (playing == 0)
			break;
	}
	
	// Re-enable interrupts
	//_enable();
}

void PlayImmediate(const char* pVGMFile)
{
	FILE* pFile;
	uint32_t delay;
	uint16_t srcDelay;
	VGM_HEADER header;
	uint32_t dataOffset;
	uint32_t currentTime;
	uint8_t i;
	
	pFile = fopen(pVGMFile, "rb");	

	fread(&header, sizeof(header), 1, pFile);
	
	// File appears sane?
	if (header.fccVGM != FCC_VGM)
	{
		printf("Header of %08X does not appear to be a VGM file\n", header.fccVGM);
		
		return;
	}
	
	printf("%s details:\n", pVGMFile);
	printf("EoF Offset: %08X\n", header.lngEOFOffset);
	printf("Version: %08X\n", header.lngVersion);
	printf("GD3 Offset: %08X\n", header.lngGD3Offset);
	printf("Total # samples: %lu\n", header.lngTotalSamples);
	printf("Playback Rate: %08X\n", header.lngRate);
	printf("VGM Data Offset: %08X\n", header.lngDataOffset);

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
	
	printf("Start playing VGM\n");
	
	// Set to rate generator
	outp(CTCMODECMDREG, CHAN0 | AMBOTH | MODE2);
	SetTimerCount(0);
	
	// Get LSB of timer counter
	currentTime = inp(CHAN0PORT);
	
	// Get MSB of timer counter
	currentTime |= ((uint16_t)inp(CHAN0PORT)) << 8;
	
	// Count down from maximum
	currentTime |= 0xFFFF0000l;
	
	while (playing)
	{
		uint8_t value = fgetc(pFile);
		
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
						delay = delayTable[srcDelay];
					else
						delay = GETDELAY(srcDelay);
					
					goto endDelay;
					break;
				}
			case 0x62:	// wait 1/60th second: 735 samples
				delay = GETDELAY(735);
				goto endDelay;
				break;
			case 0x63:	// wait 1/50th second: 882 samples
				delay = GETDELAY(882);
				goto endDelay;
				break;
			case 0x70:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(1);
				goto endDelay;
				break;
			case 0x71:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(2);
				goto endDelay;
				break;
			case 0x72:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(3);
				goto endDelay;
				break;
			case 0x73:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(4);
				goto endDelay;
				break;
			case 0x74:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(5);
				goto endDelay;
				break;
			case 0x75:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(6);
				goto endDelay;
				break;
			case 0x76:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(7);
				goto endDelay;
				break;
			case 0x77:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(8);
				goto endDelay;
				break;
			case 0x78:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(9);
				goto endDelay;
				break;
			case 0x79:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(10);
				goto endDelay;
				break;
			case 0x7A:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(11);
				goto endDelay;
				break;
			case 0x7B:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(12);
				goto endDelay;
				break;
			case 0x7C:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(13);
				goto endDelay;
				break;
			case 0x7D:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(14);
				goto endDelay;
				break;
			case 0x7E:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(15);
				goto endDelay;
				break;
			case 0x7F:	// wait n+1 samples, n can range from 0 to 15.
				delay = GETDELAY(16);
				goto endDelay;
				break;

				// SN76489 commands
			case 0x4F:	// dd : Game Gear PSG stereo, write dd to port 0x06
				// stereo PSG cmd, ignored
				fseek(pFile, 1, SEEK_CUR);
				break;
			case 0x50:	// dd : PSG (SN76489/SN76496) write value dd
				outp(SNReg[0], fgetc(pFile));
				break;
			case 0x5A:	// aa dd : YM3812, write value dd to register aa
				outp(OPL2Reg[0], fgetc(pFile));
				outp(OPL2Reg[0]+1, fgetc(pFile));
				break;
			case 0x5E:	// aa dd : YMF262 port 0, write value dd to register aa
				outp(OPL3Reg[0], fgetc(pFile));
				outp(OPL3Reg[0]+1, fgetc(pFile));
				break;
			case 0x5F:	// aa dd : YMF262 port 1, write value dd to register aa
				outp(OPL3Reg[1], fgetc(pFile));
				outp(OPL3Reg[1]+1, fgetc(pFile));
				break;
			case 0xA0:	// aa dd : AY8910, write value dd to register aa
				// Second chip is indicated by msb in first byte
				value = fgetc(pFile);
				i = (value & 0x80) ? 1 : 0;

				outp(AYReg[i], value & 0x7F);
				outp(AYReg[i]+4, fgetc(pFile));
				break;
				
			case 0xBD:	// aa dd : SAA1099, write value dd to register aa
				// Second chip is indicated by msb in first byte
				value = fgetc(pFile);
				i = (value & 0x80) ? 1 : 0;

				outp(SAAReg[i]+1, value & 0x7F);
				outp(SAAReg[i], fgetc(pFile));
				break;
			
			case 0x51:	// aa dd : YM2413, write value dd to register aa
			case 0x52:	// aa dd : YM2612 port 0, write value dd to register aa
			case 0x53:	// aa dd : YM2612 port 1, write value dd to register aa
			case 0x54:	// aa dd : YM2151, write value dd to register aa
			case 0x55:	// aa dd : YM2203, write value dd to register aa
			case 0x56:	// aa dd : YM2608 port 0, write value dd to register aa
			case 0x57:	// aa dd : YM2608 port 1, write value dd to register aa
			case 0x58:	// aa dd : YM2610 port 0, write value dd to register aa
			case 0x59:	// aa dd : YM2610 port 1, write value dd to register aa
			case 0x5B:	// aa dd : YM3526, write value dd to register aa
			case 0x5C:	// aa dd : Y8950, write value dd to register aa
			case 0x5D:	// aa dd : YMZ280B, write value dd to register aa
				// Skip
				fseek(pFile, 2, SEEK_CUR);
				break;

			default:
				printf("PlayImmediate(): Invalid: %02X\n", value);
				break;
		}
		
		continue;
		
	endDelay:
		// Filter out delays that are too small between commands
		// OPL2 needs 12 cycles for the data delay and 84 cycles for the address delay
		// This is at the base clock of 14.31818 / 4 = 3.579545 MHz
		// The PIT runs at a base clock of 14.31818 / 12 = 1.193182 MHz
		// So there are exactly 3 OPL2 cycles to every PIT cycle. Translating that is:
		// 4 PIT cycles for the data delay and 28 PIT cycles for the address delay
		// That is a total of 32 PIT cycles for every write
		/*if (delay < 32)
		{
			printf("Extremely small delay encountered: %lu. Skipping\n", delay);
			continue;
		}*/
		
		// Perform delay
		tickWaitC(delay, &currentTime);
	}

	fclose(pFile);
	
	// Reset to square wave
	outp(CTCMODECMDREG, CHAN0 | AMBOTH | MODE3);
	SetTimerCount(0);

	printf("Done playing VGM\n");
}

typedef enum
{
	FT_Unknown,
	FT_VGMFile,
	FT_MIDIFile,
	FT_PreFile,
	FT_DROFile
} FileType;

FileType GetFileType(FILE* pFile)
{
	VGM_HEADER* pVGMHeader;
	PreHeader* pPreHeader;
	MIDIHeader* pMIDIHeader;
	DROHeader* pDROHeader;
	
	uint8_t data[16];
	
	// Grab enough data to detect the supported types
	fread(data, sizeof(data), 1, pFile);
	fseek(pFile, 0, SEEK_SET);
	
	// Try VGM
	pVGMHeader = (VGM_HEADER*)data;
	if (pVGMHeader->fccVGM == FCC_VGM)
		return FT_VGMFile;
	
	// Try MIDI
	pMIDIHeader = (MIDIHeader*)data;
	if (memcmp(pMIDIHeader->chunk.chunkType, "MThd", 4) == 0)
		return FT_MIDIFile;
	
	// Try Pre
	pPreHeader = (PreHeader*)data;
	if (memcmp(pPreHeader->marker, "PreV", 4) == 0)
		return FT_PreFile;
	
	// Try DRO
	pDROHeader = (DROHeader*)data;
	if (memcmp(pDROHeader->id, "DBRAWOPL", 8) == 0)
		return FT_DROFile;
	
	return FT_Unknown;
}

void PreProcessVGM(FILE* pFile, const char* pOutFile)
{
	FILE* pOut;
	uint32_t delay = 0, minDelay, length;
	uint16_t srcDelay;
	VGM_HEADER header;
	uint32_t dataOffset, size;
	uint16_t firstDelay;
	uint16_t i;

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
	printf("Playback Rate: %08X\n", header.lngRate);
	printf("VGM Data Offset: %08X\n", header.lngDataOffset);

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
	
	// Save header
	_farfwrite(&preHeader, sizeof(preHeader), 1, pOut);
	
	// Reset all pointers
	ClearCommands();
		
	while (playing)
	{
		uint8_t data[2];
		uint8_t value = fgetc(pFile);
		
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
				AddCommandMulti(1, YMF262PORT0, data[0], data[1], pOut);
				break;
		
			case 0x51:	// aa dd : YM2413, write value dd to register aa
			case 0xA1:	// aa dd : Second YM2413, write value dd to register aa
			case 0x52:	// aa dd : YM2612 port 0, write value dd to register aa
			case 0x53:	// aa dd : YM2612 port 1, write value dd to register aa
			case 0xA2:	// aa dd : Second Second YM2612 port 0, write value dd to register aa
			case 0xA3:	// aa dd : Second YM2612 port 1, write value dd to register aa
			case 0x54:	// aa dd : YM2151, write value dd to register aa
			case 0xA4:	// aa dd : Second YM2151, write value dd to register aa
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
	
	printf("Done preprocessing VGM\n");
}

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

// Speed data
uint32_t tempo = 500000L;		// Microseconds per MIDI quarter-note, 24-bit value, default is 500000, which is 120 bpm
uint16_t division = 0;	// Ticks per quarter-note, 16-bit value
float divisor = 0;		// Precalc this at every tempo-change

// tempo/division = microseconds per MIDI tick
// 1 microsecond == 1/10000000 second

//#define MICROSEC_PER_TICK (tempo/(float)division)
//#define DIVISOR	(MICROSEC_PER_TICK*(PITFREQ/1000000.0f))
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
			divisor = (tempo*(PITFREQ/1000000.0f))/division;
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
	divisor = (tempo*(PITFREQ/1000000.0f))/division;
	
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

	// Free all memory
	for (i = 0; i < nrOfTracks; i++)
		farfree(tracks[i].pData);

	printf("Done preprocessing MIDI\n");
}

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
				delay = totalDelay*(PITFREQ/1000.0);
				
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
	playing = 1;
}

#define FRAMETICKS (262*76)
int ticksLeft = 0;
int tickRate = FRAMETICKS;
int lastTickRate = FRAMETICKS;

uint8_t far* pSampleBuffer;
uint8_t far* pSample;
uint8_t far* pSampleEnd;

// A drop of 2dB will correspond to a ratio of 10-0.1 = 0.79432823 between the current and previous output values.
// So linear output level of volume V is MAX_OUTPUT*(0.79432823^V) with V in (0..15), 0 being the loudest, and 15 silence.
// In this table, we take MAX_OUTPUT = 32767.
int volume_table[16] =
{
	32767, 26028, 20675, 16422, 13045, 10362,  8231,  6568,
	 5193,  4125,  3277,  2603,  2067,  1642,  1304,     0
};

int16_t buf[1024];

// MAX_OUTPUT*(0.79432823^V) = Y
// Solve for V:
// (0.79432823^V) = Y/MAX_OUTPUT
// V = log(Y/MAX_OUTPUT)/log(0.79432823)
void InitSampleSN76489(void)
{
	int i;
	unsigned long len;
	FILE* pFile;
	float iLog;
	
	pFile = fopen("sample.raw", "rb");
	fseek(pFile, 0, SEEK_END);
	len = ftell(pFile)/2L;
	fseek(pFile, 0, SEEK_SET);
		
	pSampleBuffer = (uint8_t far*)farmalloc(len);
	pSample = pSampleBuffer;
	pSampleEnd = pSampleBuffer+len;
	
	iLog = (float)(1.0f/log(0.79432823));
	
	while (len > 0)
	{
		size_t chunk = min(len, (sizeof(buf)/sizeof(buf[0])));
		
		fread(buf, chunk, 2, pFile);

		for (i = 0; i < chunk; i++)
		{
//#define MAX_VALUE 2.0
			//double s = sin((i*M_PI*2.0*16)/len) + 1;
#define MAX_VALUE 65535
			uint16_t s = buf[i] + 32768;

			/*s >>= 8+4;
			s = 15-s;*/
			
			if (s > 0)
				s = log(s/(double)MAX_VALUE)*iLog;
			else
				s = 15;
			
			*pSample++ = min(15, max(s, 0));
		}
		
		len -= chunk;
	}
	
	fclose(pFile);

	pSample = pSampleBuffer;
}

// Resample to 1-bit PWM
void InitSamplePIT(void)
{
	int i;
	unsigned long len;
	FILE* pFile;
	float iLog;
	
	pFile = fopen("sample.raw", "rb");
	fseek(pFile, 0, SEEK_END);
	len = ftell(pFile)/2L;
	fseek(pFile, 0, SEEK_SET);
		
	pSampleBuffer = (uint8_t far*)farmalloc(len);
	pSample = pSampleBuffer;
	pSampleEnd = pSampleBuffer+len;
	
	iLog = (float)(1.0f/log(0.79432823));
	
	while (len > 0)
	{
		size_t chunk = min(len, (sizeof(buf)/sizeof(buf[0])));
		
		fread(buf, chunk, 2, pFile);

		for (i = 0; i < chunk; i++)
		{
			// Make unsigned 16-bit sample
			uint16_t s = buf[i] + 32768;
			
			// Scale to PIT-range
			s >>= 11;

			*pSample++ = s + 1;
		}
		
		len -= chunk;
	}
	
	fclose(pFile);

	pSample = pSampleBuffer;
}

void DeinitSample(void)
{
	if (pSampleBuffer != NULL)
	{
		farfree(pSampleBuffer);
		pSampleBuffer = NULL;
		pSample = NULL;
		pSampleEnd = NULL;
	}
}

void PlayPolled1(void)
{
	//_disable();
	
	//while (pBuf < pEndBuf)
	//{
	
	__asm {
		//push ds
		//push si
		//push ax
		
		cli
		
		les di, [pEndBuf]
		lds si, [pBuf]
		
		// Get delay value from stream
		lodsb
		out CHAN0PORT, al
		lodsb
		out CHAN0PORT, al

	mainLoop:
		
		// Poll for interrupt
		mov dx, PIC1
		mov ah, 0x01

	pollLoop:
		in al, dx
		test al, ah
		je endPoll
		in al, dx
		test al, ah
		je endPoll
		in al, dx
		test al, ah
		je endPoll
		in al, dx
		test al, ah
		je endPoll
		in al, dx
		test al, ah
		je endPoll
		in al, dx
		test al, ah
		je endPoll
		in al, dx
		test al, ah
		je endPoll
		in al, dx
		test al, ah
		jne pollLoop
		
	endPoll:
		
		// Poll to send INTA
		/*mov al, (OCW3 or 0x04)//OCW3_P
		out dx, al
		in al, dx*/

		// Get note count
		lodsb
		test al, al
		jz endHandler
		
		// Play notes
		xor cx, cx
		mov cl, al
		
	noteLoop:
		lodsb
		out 0xC0, al
		
		loop noteLoop
		
	endHandler:
		// Wait for counter to go low
		mov ah, 0x01
		
	pollLoop2:
		in al, dx
		test al, ah
		je pollLoop2
		
		// Get delay value from stream
		//lds si, [pBuf]
		lodsb
		out CHAN0PORT, al
		lodsb
		out CHAN0PORT, al


		cmp si, di
		jb mainLoop
		
		mov ax, seg pBuf
		mov ds, ax
		mov word ptr [pBuf], si
		
		sti

		//pop ax		
		//pop si
		//pop ds
	}
	//}
	
	//_enable();
}

void PlayPolled2(void)
{
	//_disable();
	
	//while (pBuf < pEndBuf)
	//{
	
	__asm {
		//push ds
		//push si
		//push ax
		
		cli
		
		les di, [pEndBuf]
		lds si, [pBuf]
		
		// Get delay value from stream
		lodsb
		out CHAN0PORT, al
		lodsb
		out CHAN0PORT, al

	mainLoop:
		
		// Poll for interrupt
		mov dx, PIC1_COMMAND
		mov ah, 0x80
		
	pollLoop:
		// Poll for interrupt
		mov al, (OCW3 or 0x04)//OCW3_P
		out dx, al
		in al, dx
		test al, ah
		jz pollLoop
		
		// Get note count
		lodsb
		test al, al
		jz endHandler
		
		// Play notes
		xor cx, cx
		mov cl, al
		
	noteLoop:
		lodsb
		out 0xC0, al
		
		loop noteLoop
		
	endHandler:
		// Get delay value from stream
		lodsb
		out CHAN0PORT, al
		lodsb
		out CHAN0PORT, al

		cmp si, di
		jb mainLoop
		
		mov ax, seg pBuf
		mov ds, ax
		mov word ptr [pBuf], si
		
		sti

		//pop ax		
		//pop si
		//pop ds
	}
	//}
	
	//_enable();
}

void interrupt (*OldTimerHandler)(void);
void interrupt (*OldKeyHandler)(void);

void interrupt HandlerC(void)
{
	uint16_t huge* pW;
	
	PlayData();

	// Get delay value from stream
	pW = (uint16_t huge*)pBuf;
	SetTimerCount(*pW++);
	pBuf = (uint8_t huge*)pW;
}

void interrupt KeyHandler()
{
	uint8_t key;
	uint8_t ack;
	
	// Read byte from keyboard
	key = inp(0x60);
	// Acknowledge keyboard
	ack = inp(0x61);
	ack |= 0x80;
	outp(0x61, ack);
	ack &= 0x7F;
	outp(0x61, ack);
	
	if (key == 1)
		playing = 0;
	
	outp(PIC1_COMMAND, OCW2_EOI);
}

void SetTimerRate(uint16_t rate)
{
	// Reset mode to trigger timer immediately
	outp(CTCMODECMDREG, CHAN0 | AMBOTH | MODE2);
	
	outp(0x40, rate);
	outp(0x40, rate >> 8);
}

void SetTimerCount(uint16_t rate)
{
	outp(0x40, rate);
	outp(0x40, rate >> 8);
}

void InitHandler(void)
{
	OldTimerHandler = _dos_getvect(0x0 + 0x8);
	_dos_setvect(0x0 + 0x8, HandlerC);
}

void DeinitHandler(void)
{
	_dos_setvect(0x0 + 0x8, OldTimerHandler);
}

void InitKeyHandler(void)
{
	OldKeyHandler = _dos_getvect(0x1 + 0x8);
	_dos_setvect(0x1 + 0x8, KeyHandler);
}

void DeinitKeyHandler(void)
{
	_dos_setvect(0x1 + 0x8, OldKeyHandler);
}

MachineType machineType;

void PrepareFile(const char* pVGMFile)
{
	FILE* pFile;
	FileType fileType;
	char outFile[512] = "";
	char* pExtension;
	
	// Generate .pre filename
	pExtension = strrchr(pVGMFile, '.');
	if (pExtension == NULL)
		strcpy(outFile, pVGMFile);
	else
		memcpy(outFile, pVGMFile, pExtension-pVGMFile);
	
	strcat(outFile, ".pre");

	printf("Output file: %s\n", outFile);
	
	pFile = fopen(pVGMFile, "rb");
	
	fileType = GetFileType(pFile);
	
	switch (fileType)
	{
		case FT_VGMFile:
			PreProcessVGM(pFile, outFile);
			fclose(pFile);
			LoadPreprocessed(outFile);
			break;
		case FT_MIDIFile:
			PreProcessMIDI(pFile, outFile);
			fclose(pFile);
			LoadPreprocessed(outFile);
			break;
		case FT_PreFile:
			fclose(pFile);
			LoadPreprocessed(pVGMFile);
			break;
		case FT_DROFile:
			PreProcessDRO(pFile, outFile);
			fclose(pFile);
			LoadPreprocessed(outFile);
			break;
		default:
			fclose(pFile);
			printf("Unsupported file!\n");
			break;
	}
}

void PlayPoll1(const char* pVGMFile)
{
	PrepareFile(pVGMFile);
		
	// Set to rate generator
	outp(CTCMODECMDREG, CHAN0 | AMBOTH | MODE2);
	SetTimerCount(0);
	
	// Polling timer-based replay
	PlayBuffer2C();
	
	// Reset to square wave
	outp(CTCMODECMDREG, CHAN0 | AMBOTH | MODE3);
	SetTimerCount(0);
}

void PlayPoll2(const char* pVGMFile)
{
	PrepareFile(pVGMFile);
	
	// Setup auto-EOI
	machineType = GetMachineType();
	
	SetAutoEOI(machineType);
	
	// Set to sqarewave generator
	outp(CTCMODECMDREG, CHAN0 | AMBOTH | MODE3);
	SetTimerCount(0);

	PlayPolled1();
	
	// Reset to square wave
	outp(CTCMODECMDREG, CHAN0 | AMBOTH | MODE3);
	SetTimerCount(0);
	
	RestorePICState(machineType);
}

void PlayPoll3(const char* pVGMFile)
{
	PrepareFile(pVGMFile);
	
	// Setup auto-EOI
	machineType = GetMachineType();
	
	SetAutoEOI(machineType);
	
	// Set to rate generator
	outp(CTCMODECMDREG, CHAN0 | AMBOTH | MODE2);
	SetTimerCount(0);

	PlayPolled2();
	
	// Reset to square wave
	outp(CTCMODECMDREG, CHAN0 | AMBOTH | MODE3);
	SetTimerCount(0);
	
	RestorePICState(machineType);
}
	
void PlayInt(const char* pVGMFile)
{
	uint8_t mask;
	uint16_t far* pW;
	
	PrepareFile(pVGMFile);

	
	// Int-based replay
	// Setup auto-EOI
	machineType = GetMachineType();
	
	SetAutoEOI(machineType);
	
	_disable();
	
	// Set to rate generator
	outp(CTCMODECMDREG, CHAN0 | AMBOTH | MODE2);
	
	// Mask timer interrupts
	mask = inp(PIC1_DATA);
	outp(PIC1_DATA, mask | 1);

	// Have timer restart instantly
	SetTimerCount(1);
	
	// Set first timer value
	pW = (uint16_t far*)pBuf;
	SetTimerCount(*pW++);
	pBuf = (uint8_t far*)pW;
	
	InitHandler();
	
	_enable();
	
	// Unmask timer interrupts
	outp(PIC1_DATA, mask);
	
	while (playing)
	{
		__asm hlt
		
		if (pBuf > pEndBuf)
			playing = 0;
	}
	
	DeinitHandler();
	
	// Reset to square wave
	outp(CTCMODECMDREG, CHAN0 | AMBOTH | MODE3);
	SetTimerCount(0);
	
	RestorePICState(machineType);
}

int main(int argc, char* argv[])
{
	FILE* pFile;
	uint16_t i;
	
	if (argc < 2)
	{
		printf("Usage: VGMPlay <file> <port>\n");
		
		return 0;
	}
	
	// Try to read file
	pFile = fopen(argv[1], "rb");
	if (pFile == NULL)
	{
		printf("File not found: %s!\n", argv[1]);
		
		return 0;
	}
	
	fclose(pFile);
	
	// Parse port
/*	if (argc > 2)
		sscanf(argv[2], "%X", &SNReg[0]);

	printf("Using SN76489 at port %Xh\n", SNReg[0]);*/
	
	if (argc > 2)
		sscanf(argv[2], "%X", &lpt);

	printf("Using LPT at port %Xh\n", lpt);
	
	for (i = 1; i < argc; i++)
	{
		if (stricmp(argv[i], "/mt32") == 0)
		{
			mt32Mode = 1;
			
			printf("DreamBlaster S2(P) MT-32 mode enabled.\n");
		}
	}
	
	
	InitPCjrAudio();
	//SetPCjrAudio(1,440,15);
	//InitPCSpeaker();
#if defined(MPU401)
	InitMPU401();
#elif defined(IMFC)
	InitIMFCAll();
#elif defined(SB)
	InitSB();
#elif defined(DBS2P)
	InitDBS2P(lpt);
#endif

	// Set up channels to play samples, by setting frequency 0
	//SetPCjrAudio(0,0,15);
	//SetPCjrAudio(1,0,15);
	//SetPCjrAudio(2,0,15);
	
	//InitSampleSN76489();
	//InitSamplePIT();
	
	ResetYM3812();
	SetYMF262(0, 0);
	
	// Prepare delay table
	delayTable[0] = 2;
	for (i = 1; i < _countof(delayTable); i++)
		delayTable[i] = GETDELAY(i);
	
	InitKeyHandler();
	
	//PlayPoll1(argv[1]);
	//PlayPoll2(argv[1]);
	//PlayPoll3(argv[1]);
	PlayInt(argv[1]);
	//PlayImmediate(argv[1]);
	
	DeinitKeyHandler();
	
	farfree(pPreprocessed);
	
	//DeinitSample();
 
	{
		int chan;
		for (chan = 0; chan < 3; chan++)
			SetPCjrAudio(chan,440,15);
		
		// Disable noise channel
		SetPCjrAudioVolume(3,15);
	}

	ClosePCjrAudio();
	//ClosePCSpeaker();
#if defined(MPU401)
	CloseMPU401();
#elif defined(IMFC)
	CloseIMFC();
#elif defined(SB)
	CloseSB();
#elif defined(DBS2P)
	CloseDBS2P(lpt);
#endif

	ResetYMF262();
	ResetYM3812();

	return 0;
}
