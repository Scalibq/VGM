#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dos.h>
#include <conio.h>

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
#define SNReg 0xC0
#define SNFreq 3579540
#define SNMplxr 0x61	// MC14529b sound multiplexor chip in the PCjr
#define SampleRate 44100
#define PITfreq 1193182l

void InitPCjrAudio()
{
	uint8_t mplx;
	
	mplx = inp(SNMplxr);
	mplx |= 0x60;	// set bits 6 and 5 to route SN audio through multiplexor
	outp(SNMplxr, mplx);
}

// Sets an SN voice with volume and a desired frequency
// volume is 0-15
void SetPCjrAudio(uint8_t chan, uint16_t freq, uint8_t volume)
{
	uint16_t period;
	uint8_t command;

	period = SNFreq / (32*freq);

	// clamp period so that it doesn't exceed invalid ranges.  This also
	// removes the need to strip out bits that would interfere with the
	// OR'd command bits sent to the register
	if (period > 1023)
		period = 1023;

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
	
    // build LSB
	command = period >> 4;	// isolate upper 6 bits
	//command &= 0x7F;		// clear bit 7 to indicate rest of freq
    outp(SNReg,command);

    // set the volume
	command = chan << 5;	// get voice reg in place
	command |= 0x90;	// tell chip we're selecting a reg for volume
	command |= volume ^ 0xF;	// adjust to attenuation; register expects 0 = full, 15 = quiet
	outp(SNReg, command);
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
	command |= volume ^ 0xF;	// adjust to attenuation; register expects 0 = full, 15 = quiet
	outp(SNReg, command);
}

void ClosePCjrAudio()
{
	uint8_t chan, mplx;

	for (chan = 0; chan < 3; chan++)
		SetPCjrAudio(chan,440,0);

	// Disable noise channel
	SetPCjrAudioVolume(3,0);

	// Reset the multiplexor
	mplx = inp(SNMplxr);
	mplx &= 0x9C;	// clear 6 and 5 to route PC speaker through multiplexor; 1 and 0 turn off timer signal
	outp(SNMplxr, mplx);
}

#define iMC_Chan0 0
#define iMC_LatchCounter 0
#define iMC_OpMode2 0x4
#define iMC_BinaryMode 0

// Waits for numTicks to elapse, where a tick is 1/PIT Frequency (~1193182)
void tickWait(uint16_t numTicks)
{
	uint16_t startTime, time;
	
	// Disable interrupts
	_disable();
	
	// PIT command: Channel 0, Latch Counter, Rate Generator, Binary
	outp(0x43, iMC_Chan0+iMC_LatchCounter+iMC_OpMode2+iMC_BinaryMode);
	
	// Get LSB of timer counter
	startTime = inp(0x40);
	
	// Get MSB of timer counter
	startTime |= ((uint16_t)inp(0x40)) << 8;
	
	// Re-enable interrupts
	_enable();
	
	do
	{
		_disable();

		// PIT command: Channel 0, Latch Counter, Rate Generator, Binary
		outp(0x43, iMC_Chan0+iMC_LatchCounter+iMC_OpMode2+iMC_BinaryMode);
		
		// Get LSB of timer counter
		time = inp(0x40);
		
		// Get MSB of timer counter
		time |= ((uint16_t)inp(0x40)) << 8;
		
		_enable();
	} while ((startTime-time) < numTicks);
}

int keypressed(void)
{
	union REGPACK regs;
	regs.h.ah = 1;

	intr(0x16, &regs);

	return !(regs.x.flags & INTR_ZF);     // Check zero-flag
}

void PlayBuffer(uint8_t* pPos)
{
	int playing = 1;
		
	while (playing)
	{
		switch (*pPos++)
		{
			case 0x4f:
				// stereo PSG cmd, ignored
				pPos++;
				break;
			case 0x50:
				outp(SNReg, *pPos++);
				break;
			case 0x61:
				// wait n samples
				{
					uint16_t* pW;
					uint16_t w;
					
					printf("v\n");	// indicate we're doing a multiframe/variable wait
					
					pW = (uint16_t*)pPos;
					w = *pW++;
					// max reasonable tickWait time is 50ms, so handle larger values in slices
					while (w > (SampleRate / 20))
					{
						tickWait(PITfreq / 20);
						w -= (SampleRate / 20);
					};
					
					tickWait(PITfreq / (SampleRate / w));
					pPos = (uint8_t*)pW;
					break;
				}
			case 0x62:
				// wait 1/60th second
				{
					uint32_t wait = PITfreq / 60;
					
					while (wait > 65535)
					{
						tickWait(65535);
						wait -= 65535;
					}
					
					tickWait(wait);
					break;
				}
			case 0x63:
				// wait 1/50th second
				{
					uint32_t wait = PITfreq / 50;
					
					while (wait > 65535)
					{
						tickWait(65535);
						wait -= 65535;
					}
					
					tickWait(wait);
					break;
				}
			case 0x66:
				// end of VGM data
				playing = 0;
				break;
			default:
				printf("Invalid: %02X\n", *(pPos-1));
				break;
		}

		// handle input
		if (keypressed())
			playing = 0;
	}
}

int playing = 1;
uint8_t* pPos = NULL;

void PlayTick()
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
				outp(SNReg, *pPos++);
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

void PlayBufferTicks(uint8_t* _pPos)
{
	pPos = _pPos;
	
	while (playing)
	{
		PlayTick();
		tickWait(PITfreq / 60);

		// handle input
		if (keypressed())
			playing = 0;
	}
}

int main(int argc, char* argv[])
{
	FILE* pFile;
	long size;
	uint8_t* pVGM;
	uint8_t* pPos;
	VGMHeader* pHeader;
	uint32_t idx;
	
	if (argc != 2)
	{
		printf("Usage: VGMPlay <file>\n");
		
		return 0;
	}
	
	// Try to read file
	pFile = fopen(argv[1], "rb");
	if (pFile == NULL)
	{
		printf("File not found: %s!\n", argv[1]);
		
		return 0;
	}
	
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
		printf("Header of %02X does not appear to be a VGM file\n", pHeader->VGMIdent);
		
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

	// Start playing.  Use polling method as we are not trying to be fancy
	// at this stage, just trying to get something working}

	InitPCjrAudio();
	//SetPCjrAudio(1,440,15);

	// init PIT channel 0, 3=access mode lobyte/hibyte, mode 2, 16-bit binary}
	// We do this so we can get a sensible countdown value from mode 2 instead
	// of the 2xspeedup var from mode 3.  I have no idea why old BIOSes init
	// mode 3; everything 486 and later inits mode 2.  Go figure.  This should
	// not damage anything in DOS or TSRs, in case you were wondering.
	//InitChannel(0,3,2,$0000);
	outp(0x43, 0x34);
	
	pPos = pVGM + idx;

	PlayBufferTicks(pPos);
 
	free(pVGM);

	ClosePCjrAudio();

	return 0;
}
