#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dos.h>
#include <conio.h>
#include <math.h>
#include <malloc.h>
#include "8253.h"
#include "8259A.h"

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
	uint32_t K053260clock, Pokeyclock, QSoundclock, reserved3, reserved4;
} VGMHeader;

#define VFileIdent 0x206d6756
//#define SNReg 0xC0
#define SNFreq 3579540
#define SNMplxr 0x61	// MC14529b sound multiplexor chip in the PCjr
#define SampleRate 44100

uint16_t SNReg = 0xC0;

void InitPCjrAudio(void)
{
	uint8_t mplx;
	
	mplx = inp(SNMplxr);
	mplx |= 0x60;	// set bits 6 and 5 to route SN audio through multiplexor
	outp(SNMplxr, mplx);
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
	outp(SNReg,command);
	//__asm int 0xC0
	
    // build LSB
	command = period >> 4;	// isolate upper 6 bits
	//command &= 0x7F;		// clear bit 7 to indicate rest of freq
    outp(SNReg,command);
	//__asm int 0xC0
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
	outp(SNReg, command);
	//__asm int 0xC0
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
	uint8_t chan, mplx;

	for (chan = 0; chan < 3; chan++)
		SetPCjrAudio(chan,440,15);

	// Disable noise channel
	SetPCjrAudioVolume(3,15);

	// Reset the multiplexor
	mplx = inp(SNMplxr);
	mplx &= 0x9C;	// clear 6 and 5 to route PC speaker through multiplexor; 1 and 0 turn off timer signal
	outp(SNMplxr, mplx);
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
typedef union _WordVal
{
	struct {
		uint8_t Byte0;
		uint8_t Byte1;
	};
	uint16_t WordPart;
} WordVal;

typedef union _TimeVal
{
	struct {
		WordVal LowPart;
		WordVal HighPart;
	};
	uint16_t LongPart;	
} TimeVal;

uint32_t tickWait(uint32_t numTicks, uint32_t currentTime)
{
	uint16_t lastTime;
	uint32_t targetTime;
	
	lastTime = currentTime;
	
	targetTime = currentTime - numTicks;
	
	do
	{
		uint16_t time;
		
		_disable();

		// PIT command: Channel 0, Latch Counter, Rate Generator, Binary
		outp(CTCMODECMDREG, CHAN0 | AMREAD);
		
		// Get LSB of timer counter
		time = inp(CHAN0PORT);
		
		// Get MSB of timer counter
		time |= ((uint16_t)inp(CHAN0PORT)) << 8;
		
		_enable();
		
		// Handle wraparound
		if (time > lastTime)
		{
			currentTime -= 0x10000l;
		}
		
		currentTime &= 0xFFFF0000l;
		currentTime |= time;
		lastTime = time;
	} while (currentTime > targetTime);
	
	return currentTime;
}

// Waits for numTicks to elapse, where a tick is 1/PIT Frequency (~1193182)
void tickWait2(uint32_t numTicks)
{
	WordVal lastTime;
	TimeVal currentTime, targetTime;
	
	// Disable interrupts
	_disable();
	
	// PIT command: Channel 0, Latch Counter, Rate Generator, Binary
	outp(CTCMODECMDREG, CHAN0 | AMREAD);
	
	// Get LSB of timer counter
	currentTime.LowPart.Byte0 = inp(CHAN0PORT);
	
	// Get MSB of timer counter
	currentTime.LowPart.Byte1 = inp(CHAN0PORT);
	
	// Re-enable interrupts
	_enable();
	
	lastTime = currentTime.LowPart;
	
	// Count down from maximum
	currentTime.HighPart.WordPart = 0xFFFF;
	
	targetTime.LongPart = currentTime.LongPart - numTicks;
	
	do
	{
		WordVal time;
		
		_disable();

		// PIT command: Channel 0, Latch Counter, Rate Generator, Binary
		outp(CTCMODECMDREG, CHAN0 | AMREAD);
		
		// Get LSB of timer counter
		time.Byte0 = inp(CHAN0PORT);
		
		// Get MSB of timer counter
		time.Byte1 = inp(CHAN0PORT);
		
		_enable();
		
		// Handle wraparound
		if (time.WordPart > lastTime.WordPart)
		{
			currentTime.HighPart.WordPart--;
		}
		
		currentTime.LowPart = time;
		lastTime = time;
	} while (currentTime.LongPart > targetTime.LongPart);
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

int playing = 1;
uint8_t far* pPos = NULL;
uint32_t currentTime;

void PlayBuffer()
{
	// Disable interrupts
	_disable();
	
	// PIT command: Channel 0, Latch Counter, Rate Generator, Binary
	outp(CTCMODECMDREG, CHAN0 | AMREAD);
	
	// Get LSB of timer counter
	currentTime = inp(CHAN0PORT);
	
	// Get MSB of timer counter
	currentTime |= ((uint16_t)inp(CHAN0PORT)) << 8;
	
	// Re-enable interrupts
	_enable();
	
	// Count down from maximum
	currentTime |= 0xFFFF0000l;

	while (playing)
	{
		switch (*pPos++)
		{
			case 0x4F:	// dd : Game Gear PSG stereo, write dd to port 0x06
				// stereo PSG cmd, ignored
				pPos++;
				break;
			case 0x50:	// dd : PSG (SN76489/SN76496) write value dd
				outp(SNReg, *pPos++);
				/*{
					byte s = *pPos++;
					__asm{
						mov al, [s]
						int 0xC0
					}
				}*/
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
			case 0x5A:	// aa dd : YM3812, write value dd to register aa
			case 0x5B:	// aa dd : YM3526, write value dd to register aa
			case 0x5C:	// aa dd : Y8950, write value dd to register aa
			case 0x5D:	// aa dd : YMZ280B, write value dd to register aa
			case 0x5E:	// aa dd : YMF262 port 0, write value dd to register aa
			case 0x5F:	// aa dd : YMF262 port 1, write value dd to register aa
				// Skip
				pPos += 2;
				break;
			case 0x61:
				// wait n samples
				{
					uint16_t* pW;
					uint16_t w;
					
					pW = (uint16_t*)pPos;
					w = *pW++;
					// max reasonable tickWait time is 50ms, so handle larger values in slices
					while (w > (SampleRate / 20))
					{
						currentTime = tickWait(PITFREQ / 20, currentTime);
						w -= (SampleRate / 20);
					};
					
					currentTime = tickWait(PITFREQ / (SampleRate / w), currentTime);
					pPos = (uint8_t*)pW;
					break;
				}
			case 0x62:
				// wait 1/60th second
				{
					uint32_t wait = PITFREQ / 60L;
					
					currentTime = tickWait(wait, currentTime);
					break;
				}
			case 0x63:
				// wait 1/50th second
				{
					uint32_t wait = PITFREQ / 50L;
				
					currentTime = tickWait(wait, currentTime);
					break;
				}
			case 0x66:
				// end of VGM data
				playing = 0;
				break;
			case 0x70:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate), currentTime);
				break;
			case 0x71:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 2), currentTime);
				break;
			case 0x72:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 3), currentTime);
				break;
			case 0x73:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 4), currentTime);
				break;
			case 0x74:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 5), currentTime);
				break;
			case 0x75:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 6), currentTime);
				break;
			case 0x76:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 7), currentTime);
				break;
			case 0x77:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 8), currentTime);
				break;
			case 0x78:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 9), currentTime);
				break;
			case 0x79:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 10), currentTime);
				break;
			case 0x7A:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 11), currentTime);
				break;
			case 0x7B:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 12), currentTime);
				break;
			case 0x7C:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 13), currentTime);
				break;
			case 0x7D:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 14), currentTime);
				break;
			case 0x7E:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 15), currentTime);
				break;
			case 0x7F:	// wait n+1 samples, n can range from 0 to 15.
				currentTime = tickWait(PITFREQ / (SampleRate / 16), currentTime);
				break;
			default:
				printf("Invalid: %02X\n", *(pPos-1));
				break;
		}

		// handle input
		if (keypressed(NULL))
			playing = 0;
	}
}

void PlayTick(void)
{
	while (1)
	{
		switch (*pPos++)
		{
			case 0x4f:
				// stereo PSG cmd, ignored
				pPos++;
				break;
			case 0x50:
				// Filter out channel 2
				//if ((*pPos & 0x60) != 0x40)
					outp(SNReg, *pPos);
					/*{
						byte s = *pPos;
						__asm{
							mov al, [s]
							int 0xC0
						}
					}*/
				//else if ((*pPos & 0x10) == 0)
				//	pPos += 2;
				
				pPos++;
				break;
			case 0x66:
				// end of VGM data
				playing = 0;
			case 0x62:
				// wait 1/60th second
			case 0x63:
				// wait 1/50th second
				goto endTick;
				break;
			default:
				printf("Invalid: %02X\n", *(pPos-1));
				break;
		}
	}
endTick:;
}

void PlayBufferTicks()
{
	while (playing)
	{
		PlayTick();
		
		currentTime = tickWait(PITFREQ / 60, currentTime);

		// handle input
		if (keypressed(NULL))
			playing = 0;
	}
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
	size_t len;
	FILE* pFile;
	float iLog;
	
	pFile = fopen("sample.raw", "rb");
	fseek(pFile, 0, SEEK_END);
	len = ftell(pFile)/2L;
	fseek(pFile, 0, SEEK_SET);
		
	pSampleBuffer = (uint8_t far*)_fmalloc(len);
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
	size_t len;
	FILE* pFile;
	float iLog;
	
	pFile = fopen("sample.raw", "rb");
	fseek(pFile, 0, SEEK_END);
	len = ftell(pFile)/2L;
	fseek(pFile, 0, SEEK_SET);
		
	pSampleBuffer = (uint8_t far*)_fmalloc(len);
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
		_ffree(pSampleBuffer);
		pSampleBuffer = NULL;
		pSample = NULL;
		pSampleEnd = NULL;
	}
}

void interrupt Handler(void)
{
	PlayTick();

	// Acknowledge timer
	outp(0x20, 0x20);

	return;
	
	// Play 1 sample
	//SetPCjrAudioVolume(2, *pSample);
	outp(0x42, *pSample);
	if (++pSample >= pSampleEnd)
		pSample = pSampleBuffer;
	
	ticksLeft -= lastTickRate;

	if (ticksLeft < 0)
	{
		ticksLeft += FRAMETICKS;
		
		lastTickRate = tickRate;
		
		// Acknowledge timer
		outp(0x20, 0x20);

		//PlayTick();
	}
	else
	{
		lastTickRate = tickRate;
		
		// Acknowledge timer
		outp(0x20, 0x20);
	}
}

void interrupt (*Old1C)(void);

void SetTimerRate(uint16_t rate)
{
	_disable();
	
	// Reset mode to trigger timer immediately
	outp(0x43, 0x34);
	
	outp(0x40, rate);
	outp(0x40, rate >> 8);
	
	_enable();
}

void SetTimerCount(uint16_t rate)
{
	_disable();
	
	outp(0x40, rate);
	outp(0x40, rate >> 8);
	
	_enable();
}

void InitHandler(void)
{
	//tickRate = PITfreq/8000;
	//lastTickRate = tickRate;
	tickRate = 19912;
	
	SetTimerRate(tickRate);	// Play at 60 Hz

	Old1C = _dos_getvect(0x8);
	_dos_setvect(0x8, Handler);
}

void DeinitHandler(void)
{
	_disable();
	
	// Return timer to default 18.2 Hz
	outp(0x43, 0x36);
	
	outp(0x40, 0);
	outp(0x40, 0);
	
	_enable();

	_dos_setvect(0x8, Old1C);
}

MachineType machineType;

int main(int argc, char* argv[])
{
	FILE* pFile;
	long size;
	uint8_t* pVGM;
	VGMHeader* pHeader;
	uint32_t idx;
	
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
	
	// Parse port
	if (argc > 2)
		sscanf(argv[2], "%X", &SNReg);

	printf("Using SN76489 at port %Xh\n", SNReg);
	
	fseek(pFile, 0, SEEK_END);
	size = ftell(pFile);
	fseek(pFile, 0, SEEK_SET);
	
	pVGM = malloc(size);
	fread(pVGM, size, 1, pFile);
	fclose(pFile);
	
	// File appears sane?
	pHeader = (VGMHeader*)pVGM;
	
	if (pHeader->VGMIdent != VFileIdent)
	{
		printf("Header of %08X does not appear to be a VGM file\n", pHeader->VGMIdent);
		
		return 0;
	}
	
	printf("%s details:\n", argv[1]);
	printf("EoF Offset: %08X\n", pHeader->EOFoffset);
	printf("Version: %08X\n", pHeader->Version);
	printf("GD3 Offset: %08X\n", pHeader->GD3offset);
	printf("Total # samples: %lu\n", pHeader->totalSamples);
	printf("Playback Rate: %08X\n", pHeader->Rate);
	printf("VGM Data Offset: %08X\n", pHeader->VGMdataoffset);

    if (pHeader->VGMdataoffset == 0)
		idx = 0x40;
	else
		idx = pHeader->VGMdataoffset + 0x34;
	
	printf("VGM Data starts at %08X\n", idx);
	
    // Can we play this on PCjr hardware?
    if (pHeader->SN76489clock != 0)
		printf("SN76489 Clock: %lu Hz\n", pHeader->SN76489clock);
	else
	{
		printf("File does not contain data for our hardware\n");
		
		return 3;
	}

    // Print GD3 tag to be nice
    /*writeln(#13#10'GD3 Tag Information:');
    for w:=GD3Offset+$18 to EOFoffset-$4 do begin
      c:=char(ba^[w]);
      case c of
        #32..#127:write(c);
        #0:if ba^[w+1]=0 then writeln; {see goofy GD3 spec for details}
      end;
    end;
    writeln;
  end;*/

	// Setup auto-EOI
	//machineType = GetMachineType();
	
	//SetAutoEOI(machineType);
  

	// Start playing.  Use polling method as we are not trying to be fancy
	// at this stage, just trying to get something working}

	//InitPCjrAudio();
	//SetPCjrAudio(1,440,15);
	//InitPCSpeaker();

	// init PIT channel 0, 3=access mode lobyte/hibyte, mode 2, 16-bit binary}
	// We do this so we can get a sensible countdown value from mode 2 instead
	// of the 2xspeedup var from mode 3.  I have no idea why old BIOSes init
	// mode 3; everything 486 and later inits mode 2.  Go figure.  This should
	// not damage anything in DOS or TSRs, in case you were wondering.
	//InitChannel(0,3,2,$0000);
	outp(0x43, 0x34);
	
	pPos = pVGM + idx;
	
	// Set up channels to play samples, by setting frequency 0
	//SetPCjrAudio(0,0,15);
	//SetPCjrAudio(1,0,15);
	//SetPCjrAudio(2,0,15);
	
	//InitSampleSN76489();
	//InitSamplePIT();

	// Polling timer-based replay
	SetTimerRate(0);
	PlayBuffer();
	_disable();
	
	// Return timer to default 18.2 Hz
	outp(0x43, 0x36);
	
	outp(0x40, 0);
	outp(0x40, 0);
	
	_enable();
	
	// Int-based replay
	/*
	InitHandler();
	
	while (playing)
	{
		uint8_t key;
		
		if (keypressed(&key))
		{
			switch(key)
			{
				case '1':
					tickRate += 100;
					break;
				case '2':
					tickRate -= 100;
					break;
				case '3':
					playing = 0;
					break;
				case '4':
					tickRate = 19912;
					break;
				case '5':
					tickRate = 2000;
					break;
			}
			
			SetTimerCount(tickRate);
			printf("Tickrate: %d\n", tickRate);
		}
	}
	
	DeinitHandler();
	*/
	
	//RestorePICState(machineType);
	
	//DeinitSample();
 
	free(pVGM);
	
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
