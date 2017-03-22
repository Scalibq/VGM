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
#include "MIDI.h"

#define M_PI 3.1415926535897932384626433832795

typedef struct
{
	uint32_t VGMIdent, EOFoffset, Version, SN76489clock;
	uint32_t YM2413clock, GD3offset, totalSamples, loopOffset;
	uint32_t loopNumSamples, Rate;
	uint16_t SNFB;
	uint8_t _SNW, _SF;
	uint32_t YM2612clock, YM2151clock, VGMdataoffset, SegaPCMclock, SPCMInterface;
	uint32_t RF5C68clock, YM2203clock, YM2608clock, YM2610Bclock;
	uint32_t YM3812clock, YM3526clock, Y8950clock, YMF262clock;
	uint32_t YMF278Bclock, YMF271clock, YMZ280Bclock, RF5C164clock;
	uint32_t PWMclock, AY8910clock;
	uint8_t _AYT;
	uint8_t AYFlags[2];
    uint8_t _VM, reserved1, _LB, _LM;
    uint32_t GBDMGclock, NESAPUclock, MultiPCMclock, uPD7759clock;
	uint32_t OKIM6258clock;
	uint8_t _OF, _KF, _CF, reserved2;
	uint32_t OKIM6295clock, K051649clock, K054539clock, HuC6280clock, C140clock;
	uint32_t K053260clock, Pokeyclock, QSoundclock, SCSPclock, extraHdroffset;
} VGMHeader;

#define VFileIdent 0x206d6756
//#define SNReg 0xC0
#define SNFreq 3579540
#define SNMplxr 0x61	// MC14529b sound multiplexor chip in the PCjr
#define SampleRate 44100

#define DIVISOR		(55411U)	/* (1193182.0f/44100.0f) * 2048 */
#define	DIVISOR_SHIFT	(11U)

#define GETDELAY(n)	((uint32_t)(n*(uint32_t)DIVISOR) >> DIVISOR_SHIFT)
//#define GETDELAY(n)	((uint32_t)(((n*(1193182.0/44100.0))+0.5)))

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

uint16_t SNReg[MAX_MULTICHIP] = { 0xC0, 0xC0 };
uint16_t SAAReg[MAX_MULTICHIP] = { 0, 0 };
uint16_t AYReg[MAX_MULTICHIP] = { 0, 0 };
uint16_t OPL2Reg[MAX_MULTICHIP] = { 0x388, 0x388 };
uint16_t OPL3Reg[MAX_MULTICHIP*2] = { 0x220, 0x222, 0x220, 0x222 };	// Special case: there are two separate ports for the chip
uint16_t MPUReg[MAX_MULTICHIP] = { 0x330 };

typedef struct _PreHeader
{
	char marker[4];				// = {'P','r','e','V'}; // ("Pre-processed VGM"? No idea, just 4 characters to detect that this is one of ours)
	uint32_t headerLen;			// = sizeof(_PreHeader); // Good MS-custom: always store the size of the header in your file, so you can add extra fields to the end later
	uint32_t size;				// Amount of data after header
	uint8_t version;			// Including a version number may be a good idea
	uint8_t nrOfSN76489;
	uint8_t nrOfSAA1099;
	uint8_t nrOfAY8930;
	uint8_t nrOfYM3812;
	uint8_t nrOfYMF262;
	uint8_t nrOfMIDI;
} PreHeader;

PreHeader preHeader = {
	{'P','r','e','V'},
	sizeof(PreHeader),
	0x01,
	0,	// nrOfSN76489;
	0,	// nrOfSAA1099;
	0,	// nrOfAY8930;
	0,	// nrOfYM3812;
	0,	// nrOfYMF262;
	0,	// nrOfMIDI;
};

void SetTimerCount(uint16_t rate);

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
	uint8_t count;
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

		// Loop through all command-data
		count = *pBuf++;
	
		while (count--)
		{
			//outp(SNReg, *pBuf++);
			outp(0x388, *pBuf++);
			outp(0x389, *pBuf++);
		}

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
	VGMHeader header;
	uint32_t idx;
	uint32_t currentTime;
	
	pFile = fopen(pVGMFile, "rb");	

	fread(&header, sizeof(header), 1, pFile);
	
	// File appears sane?
	if (header.VGMIdent != VFileIdent)
	{
		printf("Header of %08X does not appear to be a VGM file\n", header.VGMIdent);
		
		return;
	}
	
	printf("%s details:\n", pVGMFile);
	printf("EoF Offset: %08X\n", header.EOFoffset);
	printf("Version: %08X\n", header.Version);
	printf("GD3 Offset: %08X\n", header.GD3offset);
	printf("Total # samples: %lu\n", header.totalSamples);
	printf("Playback Rate: %08X\n", header.Rate);
	printf("VGM Data Offset: %08X\n", header.VGMdataoffset);

    if (header.VGMdataoffset == 0)
		idx = 0x40;
	else
		idx = header.VGMdataoffset + 0x34;
	
	printf("VGM Data starts at %08X\n", idx);
	
    // Can we play this on PCjr hardware?
    /*if (header.SN76489clock != 0)
		printf("SN76489 Clock: %lu Hz\n", header.SN76489clock);
	else
	{
		printf("File does not contain data for our hardware\n");
		
		return;
	}*/
	
	// Seek to VGM data
	fseek(pFile, idx, SEEK_SET);
	
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

void SavePreprocessed(const char* pFileName);

uint8_t commands[MAX_MULTICHIP][NUM_CHIPS][256];
uint8_t* pCommands[MAX_MULTICHIP][NUM_CHIPS];

void OutputCommands(FILE* pOut)
{
	uint16_t count, length, i;

	for (i = 0; i < preHeader.nrOfSN76489; i++)
	{
		length = pCommands[i][SN76489] - commands[i][SN76489];
		count = (length - 1);

		if (count > 255)
			printf("Too many SN76489 commands: %u!\n", count);
		commands[i][SN76489][0] = count;
		fwrite(commands[i][SN76489], length, 1, pOut);
	}
		
	for (i = 0; i < preHeader.nrOfSAA1099; i++)
	{
		// TODO
	}
				
	for (i = 0; i < preHeader.nrOfAY8930; i++)
	{
		// TODO
	}

	for (i = 0; i < preHeader.nrOfYM3812; i++)
	{
		length = pCommands[i][YM3812] - commands[i][YM3812];
		count = (length - 1) / 2;

		if (count > 255)
			printf("Too many YM3812 commands: %u!\n", count);
		commands[i][YM3812][0] = count;
		fwrite(commands[i][YM3812], length, 1, pOut);
	}

	for (i = 0; i < preHeader.nrOfYMF262; i++)
	{
		// Port 0 first
		length = pCommands[i][YMF262PORT0] - commands[i][YMF262PORT0];
		count = (length - 1) / 2;

		if (count > 255)
			printf("Too many YMF262 port 0 commands: %u!\n", count);
		commands[i][YMF262PORT0][0] = count;
		fwrite(commands[i][YMF262PORT0], length, 1, pOut);

		// Port 1 second
		length = pCommands[i][YMF262PORT1] - commands[i][YMF262PORT1];
		count = (length - 1) / 2;

		if (count > 255)
			printf("Too many YMF262 port 1 commands: %u!\n", count);
		commands[i][YMF262PORT1][0] = count;
		fwrite(commands[i][YMF262PORT1], length, 1, pOut);
	}
}

typedef enum
{
	FT_Unknown,
	FT_VGMFile,
	FT_MIDIFile,
	FT_PreFile
} FileType;

FileType GetFileType(FILE* pFile)
{
	VGMHeader* pVGMHeader;
	PreHeader* pPreHeader;
	MIDIHeader* pMIDIHeader;
	
	uint8_t data[16];
	
	// Grab enough data to detect the supported types
	fread(data, sizeof(data), 1, pFile);
	fseek(pFile, 0, SEEK_SET);
	
	// Try VGM
	pVGMHeader = (VGMHeader*)data;
	if (pVGMHeader->VGMIdent == VFileIdent)
		return FT_VGMFile;
	
	// Try MIDI
	pMIDIHeader = (MIDIHeader*)data;
	if (memcmp(pMIDIHeader->chunk.chunkType, "MThd", 4) == 0)
		return FT_MIDIFile;
	
	// Try Pre
	pPreHeader = (PreHeader*)data;
	if (memcmp(pPreHeader->marker, "PreV", 4) == 0)
		return FT_PreFile;
	
	return FT_Unknown;
}

void PreProcessVGM(FILE* pFile, const char* pOutFile)
{
	FILE* pOut;
	uint32_t delay;
	uint16_t srcDelay;
	VGMHeader header;
	uint32_t idx, size;
	uint16_t firstDelay;
	uint16_t i, j;

	fread(&header, sizeof(header), 1, pFile);
	
	// File appears sane?
	if (header.VGMIdent != VFileIdent)
	{
		printf("Header of %08X does not appear to be a VGM file\n", header.VGMIdent);
		
		return;
	}
	
	printf("VGM file details:\n");
	printf("EoF Offset: %08X\n", header.EOFoffset);
	printf("Version: %08X\n", header.Version);
	printf("GD3 Offset: %08X\n", header.GD3offset);
	printf("Total # samples: %lu\n", header.totalSamples);
	printf("Playback Rate: %08X\n", header.Rate);
	printf("VGM Data Offset: %08X\n", header.VGMdataoffset);

    if (header.VGMdataoffset == 0)
		idx = 0x40;
	else
		idx = header.VGMdataoffset + 0x34;
	
	printf("VGM Data starts at %08X\n", idx);
	
    // Can we play this on PCjr hardware?
    /*if (header.SN76489clock != 0)
		printf("SN76489 Clock: %lu Hz\n", header.SN76489clock);
	else
	{
		printf("File does not contain data for our hardware\n");
		
		return;
	}*/
	
	// Seek to VGM data
	fseek(pFile, idx, SEEK_SET);
	
	printf("Start preprocessing VGM\n");
	
	pOut = fopen(pOutFile, "wb");
	
	// Detect used chips
	preHeader.nrOfSN76489 = (header.SN76489clock != 0) + ((header.SN76489clock & 0x80000000l) != 0);
	//preHeader.nrOfSAA1099 = (header.SAA1099clock != 0) + ((header.SAA1099clock & 0x80000000l) != 0);
	//preHeader.nrOfAY8930 = (header.AY8930clock != 0) + ((header.AY8930clock & 0x80000000l) != 0);
	preHeader.nrOfYM3812 = (header.YM3812clock != 0) + ((header.YM3812clock & 0x80000000l) != 0);
	preHeader.nrOfYMF262 = (header.YMF262clock != 0) + ((header.YMF262clock & 0x80000000l) != 0);
	preHeader.nrOfMIDI = 0;
	
	printf("# SN76479: %u\n", preHeader.nrOfSN76489);
	printf("# SAA1099: %u\n", preHeader.nrOfSAA1099);
	printf("# AY8930: %u\n", preHeader.nrOfAY8930);
	printf("# YM3812: %u\n", preHeader.nrOfYM3812);
	printf("# YMF262: %u\n", preHeader.nrOfYMF262);
	printf("# MIDI: %u\n", preHeader.nrOfMIDI);

	
	// Save header
	_farfwrite(&preHeader, sizeof(preHeader), 1, pOut);
	
	// Reset all pointers
	for (i = 0; i < MAX_MULTICHIP; i++)
		for (j = 0; j < NUM_CHIPS; j++)
			pCommands[i][j] = commands[i][j] + 1;
	
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
			case 0x3F: 	// dd : Second Game Gear PSG stereo, write dd to port 0x06
				// stereo PSG cmd, ignored
				fseek(pFile, 1, SEEK_CUR);
				break;
			case 0x50:	// dd : PSG (SN76489/SN76496) write value dd
				*pCommands[0][SN76489]++ = fgetc(pFile);
				break;
			case 0x30:	// dd : Second PSG (SN76489/SN76496) write value dd
				*pCommands[1][SN76489]++ = fgetc(pFile);
				break;
			case 0x5A:	// aa dd : YM3812, write value dd to register aa
				*pCommands[0][YM3812]++ = fgetc(pFile);
				*pCommands[0][YM3812]++ = fgetc(pFile);
				break;
			case 0xAA:	// aa dd : Second YM3812, write value dd to register aa
				*pCommands[1][YM3812]++ = fgetc(pFile);
				*pCommands[1][YM3812]++ = fgetc(pFile);
				break;
			case 0x5E:	// aa dd : YMF262 port 0, write value dd to register aa
				*pCommands[0][YMF262PORT0]++ = fgetc(pFile);
				*pCommands[0][YMF262PORT0]++ = fgetc(pFile);
				break;
			case 0x5F:	// aa dd : YMF262 port 1, write value dd to register aa
				*pCommands[0][YMF262PORT1]++ = fgetc(pFile);
				*pCommands[0][YMF262PORT1]++ = fgetc(pFile);
				break;
				
			case 0xBD:	// aa dd : SAA1099, write value dd to register aa
				// Second chip is indicated by msb in first byte
				value = fgetc(pFile);
				i = (value & 0x80) ? 1 : 0;

				*pCommands[i][SAA1099]++ = value;
				*pCommands[i][SAA1099]++ = fgetc(pFile);
				break;
			
			case 0xAE:	// aa dd : Second YMF262 port 0, write value dd to register aa
			case 0xAF:	// aa dd : Second YMF262 port 1, write value dd to register aa
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
		/*if (delay < 32)
		{
			printf("Extremely small delay encountered: %lu. Skipping\n", delay);
			continue;
		}*/
		
		// Break up into multiple delays with no notes
		while (delay > 1)
		{
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
			for (i = 0; i < MAX_MULTICHIP; i++)
				for (j = 0; j < NUM_CHIPS; j++)
					pCommands[i][j] = commands[i][j] + 1;
		}
	}
	
	// Output last delay of 0
	firstDelay = 0;

	// Write to disk
	fwrite(&firstDelay, sizeof(firstDelay), 1, pOut);
	
	// Output last set of commands
	OutputCommands(pOut);
	
	// And a final delay of 0, which would get fetched by the last int handler
	fwrite(&firstDelay, sizeof(firstDelay), 1, pOut);
	
	// Update size field
	size = ftell(pOut);
	size -= sizeof(preHeader);
	fseek(pOut, 8, SEEK_SET);
	fwrite(&size, sizeof(size), 1, pOut);

	fclose(pOut);
	
	printf("Done preprocessing VGM\n");
}

void SavePreprocessed(const char* pFileName)
{
	FILE* pFile = fopen(pFileName, "wb");

	preHeader.size = ((uint32_t)pEndBuf-(uint32_t)pPreprocessed);
	
	printf("Preprocessed size: %lu\n", preHeader.size);
	
	// Save to file
	_farfwrite(&preHeader, sizeof(preHeader), 1, pFile);	
	_farfwrite(pPreprocessed, preHeader.size, 1, pFile);
	fclose(pFile);
}

void LoadPreprocessed(const char* pFileName)
{
	FILE* pFile = fopen(pFileName, "rb");
	
	// Load from file
	_farfread(&preHeader, sizeof(preHeader), 1, pFile);
	
	pPreprocessed = farmalloc(preHeader.size);
	
	printf("Preprocessed size: %lu\n", preHeader.size);
	
	printf("# SN76479: %u\n", preHeader.nrOfSN76489);
	printf("# SAA1099: %u\n", preHeader.nrOfSAA1099);
	printf("# AY8930: %u\n", preHeader.nrOfAY8930);
	printf("# YM3812: %u\n", preHeader.nrOfYM3812);
	printf("# YMF262: %u\n", preHeader.nrOfYMF262);
	printf("# MIDI: %u\n", preHeader.nrOfMIDI);

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
	uint8_t count;
	uint16_t huge* pW;
	uint16_t i;
	
	// Get note data
	for (i = 0; i < preHeader.nrOfSN76489; i++)
	{
		count = *pBuf++;
		while (count--)
			outp(SNReg[i], *pBuf++);
	}
		
	for (i = 0; i < preHeader.nrOfSAA1099; i++)
	{
		// TODO
	}
				
	for (i = 0; i < preHeader.nrOfAY8930; i++)
	{
		// TODO
	}
		
	for (i = 0; i < preHeader.nrOfYM3812; i++)
	{
		count = *pBuf++;
	
		while (count--)
		{
			outp(OPL2Reg[i], *pBuf++);
			outp(OPL2Reg[i]+1, *pBuf++);
		}
	}
		
	for (i = 0; i < preHeader.nrOfYMF262; i++)
	{
		// First port 0
		count = *pBuf++;
			
		while (count--)
		{
			outp(OPL3Reg[i*2], *pBuf++);
			outp(OPL3Reg[i*2]+1, *pBuf++);
		}

		// Second port 1
		count = *pBuf++;
			
		while (count--)
		{
			outp(OPL3Reg[i*2 + 1], *pBuf++);
			outp(OPL3Reg[i*2 + 1]+1, *pBuf++);
		}
	}

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

void PlayPoll1(const char* pVGMFile)
{
	FILE* pFile;
	FileType fileType;
	
	pFile = fopen(pVGMFile, "rb");
	
	fileType = GetFileType(pFile);
	
	switch (fileType)
	{
		case FT_VGMFile:
			PreProcessVGM(pFile, "out.pre");
			fclose(pFile);
			LoadPreprocessed("out.pre");
			break;
		case FT_MIDIFile:
			// TODO
			fclose(pFile);
			break;
		case FT_PreFile:
			fclose(pFile);
			LoadPreprocessed(pVGMFile);
			break;
		default:
			fclose(pFile);
			printf("Unsupported file!\n");
			break;
	}
		
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
	FILE* pFile;
	FileType fileType;
	
	pFile = fopen(pVGMFile, "rb");
	
	fileType = GetFileType(pFile);
	
	switch (fileType)
	{
		case FT_VGMFile:
			PreProcessVGM(pFile, "out.pre");
			fclose(pFile);
			LoadPreprocessed("out.pre");
			break;
		case FT_MIDIFile:
			// TODO
			fclose(pFile);
			break;
		case FT_PreFile:
			fclose(pFile);
			LoadPreprocessed(pVGMFile);
			break;
		default:
			fclose(pFile);
			printf("Unsupported file!\n");
			break;
	}
	
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
	FILE* pFile;
	FileType fileType;
	
	pFile = fopen(pVGMFile, "rb");	

	fileType = GetFileType(pFile);
	
	switch (fileType)
	{
		case FT_VGMFile:
			PreProcessVGM(pFile, "out.pre");
			fclose(pFile);
			LoadPreprocessed("out.pre");
			break;
		case FT_MIDIFile:
			// TODO
			fclose(pFile);
			break;
		case FT_PreFile:
			fclose(pFile);
			LoadPreprocessed(pVGMFile);
			break;
		default:
			fclose(pFile);
			printf("Unsupported file!\n");
			break;
	}
	
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
	FILE* pFile;
	FileType fileType;
	uint8_t mask;
	uint16_t far* pW;
	
	// Int-based replay
	pFile = fopen(pVGMFile, "rb");	

	fileType = GetFileType(pFile);
	
	switch (fileType)
	{
		case FT_VGMFile:
			PreProcessVGM(pFile, "out.pre");
			fclose(pFile);
			LoadPreprocessed("out.pre");
			break;
		case FT_MIDIFile:
			// TODO
			fclose(pFile);
			break;
		case FT_PreFile:
			fclose(pFile);
			LoadPreprocessed(pVGMFile);
			break;
		default:
			fclose(pFile);
			printf("Unsupported file!\n");
			break;
	}
	
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
	if (argc > 2)
		sscanf(argv[2], "%X", &SNReg[0]);

	printf("Using SN76489 at port %Xh\n", SNReg[0]);
	
	InitPCjrAudio();
	//SetPCjrAudio(1,440,15);
	//InitPCSpeaker();

	// Set up channels to play samples, by setting frequency 0
	//SetPCjrAudio(0,0,15);
	//SetPCjrAudio(1,0,15);
	//SetPCjrAudio(2,0,15);
	
	//InitSampleSN76489();
	//InitSamplePIT();
	
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

	return 0;
}
