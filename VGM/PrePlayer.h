#ifndef _PREPLAYER_H_
#define _PREPLAYER_H_

#include "32bit.h"

extern uint8_t huge* pPreprocessed;
extern uint8_t huge* pBuf;
extern uint8_t huge* pEndBuf;
extern uint8_t huge* pLoopStart;
extern uint8_t huge* pLoopEnd;

void GetDevices(void);
void InitDevices(void);
void DeInitDevices(void);
void LoadPreprocessed(const char* pFileName);
void PlayData(void);

#endif /* _PREPLAYER_H_ */
