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
#include "PreProcessVGM.h"
#include "PreProcessMIDI.h"
#include "PreProcessDRO.h"
#include "PrePlayer.h"

uint16_t lpt = 0x378;

void SetTimerCount(uint16_t rate);

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

uint32_t playTime = 0;

void interrupt HandlerC(void)
{
	uint16_t huge* pW;
	uint16_t delay;

	PlayData();
	
	// Handle looping
	if (pBuf >= pLoopEnd)
	{
		pBuf = pLoopStart;
	}

	// Get delay value from stream
	pW = (uint16_t huge*)pBuf;
	delay = *pW++;
	SetTimerCount(delay);
	pBuf = (uint8_t huge*)pW;

	if (delay == 0)
		playTime += 65536L;
	else
		playTime += delay;
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
	
	outp(CHAN0PORT, rate);
	outp(CHAN0PORT, rate >> 8);
}

void SetTimerCount(uint16_t rate)
{
	outp(CHAN0PORT, rate);
	outp(CHAN0PORT, rate >> 8);
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
		uint16_t minutes, seconds, ms;

		//__asm hlt
		
		SplitTime(playTime / (PITFREQ/1000L), &minutes, &seconds, &ms);

		printf("\rTime: %u:%02u.%03u", minutes, seconds, ms);

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
		else if (stricmp(argv[i], "/opl3") == 0)
		{
			if (argc >= (i+1))
			{
				uint16_t opl3;
				sscanf(argv[i+1], "%X", &opl3);
				
				SetOPL3(opl3);
				
				printf("OPL3 address set to: %Xh\n", opl3);
			}
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
	SetYMF262(1, 0);
	
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
