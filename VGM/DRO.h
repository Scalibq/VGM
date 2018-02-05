#ifndef _DRO_H_
#define _DRO_H_

typedef struct
{
	uint8_t id[8];				/* 0x00, "DBRAWOPL" */
	uint16_t versionHigh;			/* 0x08, size of the data following the m */
	uint16_t versionLow;			/* 0x0a, size of the data following the m */
	uint32_t commands;			/* 0x0c, Bit32u amount of command/data pairs */
	uint32_t milliseconds;		/* 0x10, Bit32u Total milliseconds of data in this chunk */
	uint8_t hardware;				/* 0x14, Bit8u Hardware Type 0=opl2,1=dual-opl2,2=opl3 */
	uint8_t format;				/* 0x15, Bit8u Format 0=cmd/data interleaved, 1 maybe all cdms, followed by all data */
	uint8_t compression;			/* 0x16, Bit8u Compression Type, 0 = No Compression */
	uint8_t delay256;				/* 0x17, Bit8u Delay 1-256 msec command */
	uint8_t delayShift8;			/* 0x18, Bit8u (delay + 1)*256 */			
	uint8_t conversionTableSize;	/* 0x191, Bit8u Raw Conversion Table size */
} DROHeader;

#endif /* _DRO_H_ */
