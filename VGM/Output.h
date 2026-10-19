#ifndef _OUTPUT_H_
#define _OUTPUT_H_

#include "MPU401.h"
#include "IMFC.h"
#include "SB.h"
#include "DBS2P.h"
#include "OPL2LPT.h"
#include "OPL3LPT.h"
#include "TNDLPT.h"
#include "CMSLPT.h"
#include "PreProcess.h"
#include "32bit.h"

#define SNFreq 3579540

typedef struct _OutputDevice
{
	void (*Init)(struct _OutputDevice*);
	void (*DeInit)(struct _OutputDevice*);
	uint8_t huge* (*Play)(struct _OutputDevice*, uint8_t huge*);
} OutputDevice;

typedef struct
{
	OutputDevice device;
	uint16_t command;
} SN76489Dev;

typedef struct
{
	OutputDevice device;
	uint16_t base;
} TNDLPTDev;

typedef struct
{
	OutputDevice device;
	uint16_t data;
	uint16_t command;
} SAA1099Dev;

typedef struct
{
	OutputDevice device;
	uint16_t base;
} CMSLPTDev;

typedef struct
{
	OutputDevice device;
	uint16_t command;
	uint16_t data;
} AY8930Dev;

typedef struct
{
	OutputDevice device;
	uint16_t command;
	uint16_t data;
} OPL2Dev;

typedef struct
{
	OutputDevice device;
	uint16_t base;
} OPL2LPTDev;

typedef struct
{
	OutputDevice device;
	struct
	{
		uint16_t command;
		uint16_t data;
	} port0;
	struct
	{
		uint16_t command;
		uint16_t data;
	} port1;
} OPL3Dev;

typedef struct
{
	OutputDevice device;
	uint16_t base;
} OPL3LPTDev;

typedef struct
{
	OutputDevice device;
	uint16_t command;
	uint16_t data;
} MPUDev;

typedef struct
{
	OutputDevice device;
	uint16_t command;
} IMFCDev;

typedef struct
{
	OutputDevice device;
	uint16_t command;
	uint16_t data;
} YM2151Dev;

typedef struct
{
	OutputDevice device;
	uint16_t command;
	uint16_t data;
} YM2203Dev;

typedef struct
{
	OutputDevice device;
	struct
	{
		uint16_t command;
		uint16_t data;
	} port0;
	struct
	{
		uint16_t command;
		uint16_t data;
	} port1;
} YM2608Dev;

typedef struct
{
	OutputDevice device;
	uint16_t base;
} SBMIDIDev;

extern SN76489Dev SNReg[MAX_MULTICHIP];
extern SAA1099Dev SAAReg[MAX_MULTICHIP];
extern AY8930Dev AYReg[MAX_MULTICHIP];
extern OPL2Dev OPL2Reg[MAX_MULTICHIP];
extern OPL3Dev OPL3Reg[MAX_MULTICHIP];
extern MPUDev MPUReg[MAX_MULTICHIP];
extern IMFCDev IMFCReg[MAX_MULTICHIP];
extern YM2151Dev YMReg[MAX_MULTICHIP];
extern YM2203Dev YM03Reg[MAX_MULTICHIP];
extern YM2608Dev YM08Reg[MAX_MULTICHIP];
extern SBMIDIDev SBReg[MAX_MULTICHIP];

#define SNMplxr 0x61	// MC14529b sound multiplexor chip in the PCjr

extern uint16_t lpt;
extern bool opl322;

void OutputMIDI(uint16_t base, uint8_t huge* pBuf, uint16_t len);

void SetOPL3(OutputDevice* pDev, uint16_t base);

void InitMPU401(OutputDevice* pDev);
void CloseMPU401(OutputDevice* pDev);
void InitIMFCAll(OutputDevice* pDev);
void CloseIMFC(OutputDevice* pDev);
void InitSB(OutputDevice* pDev);
void CloseSB(OutputDevice* pDev);
void InitDBS2P(uint16_t base);
void CloseDBS2P(uint16_t base);
void InitPCjrAudio(OutputDevice* pDev);
void ClosePCjrAudio(OutputDevice* pDev);
void SetPCjrAudioPeriod(OutputDevice* pDev, uint8_t chan, uint16_t period);
void SetPCjrAudioVolume(OutputDevice* pDev, uint8_t chan, uint8_t volume);
void SetPCjrAudio(OutputDevice* pDev, uint8_t chan, uint16_t freq, uint8_t volume);
void InitPCSpeaker(void);
void ClosePCSpeaker(void);
void ResetYM3812(OutputDevice* pDev);
void SetYMF262(OutputDevice* pDev, uint8_t opl3, uint8_t fourOp);
void ResetYMF262(OutputDevice* pDev);

#endif /* _OUTPUT_H_ */
