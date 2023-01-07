#ifndef _PREPROCESSMIDI_H_
#define _PREPROCESSMIDI_H_

#include <stdint.h>
#include "MIDI.h"
#include "PreProcess.h"

extern uint8_t mt32Mode;	// Special mode to prefix any program change with a special command for DreamBlaster S2(P) for MT-32 instruments

void SetPITFreqMIDI(uint32_t pitFreq);
void PreProcessMIDI(FILE* pFile, const char* pOutFile);

#endif /* _PREPROCESSMIDI_H_ */
