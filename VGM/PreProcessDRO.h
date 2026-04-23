#ifndef _PREPROCESSDRO_H_
#define _PREPROCESSDRO_H_

#include <stdio.h>
#include <stdint.h>

void SetPITFreqDRO(uint32_t pitFreq);
void PreProcessDRO(FILE* pFile, const char* pOutFile);

#endif /* _PREPROCESSDRO_H_ */
