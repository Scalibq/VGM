#ifndef _PREPLAYER_H_
#define _PREPLAYER_H_

extern uint8_t huge* pPreprocessed;
extern uint8_t huge* pBuf;
extern uint8_t huge* pEndBuf;

void LoadPreprocessed(const char* pFileName);
void PlayData(void);
void OutputMIDI(uint16_t base, uint8_t huge* pBuf, uint16_t len);

void InitMPU401(void);
void CloseMPU401(void);
void InitIMFCAll(void);
void CloseIMFC(void);
void InitSB(void);
void CloseSB(void);
void InitDBS2P(uint16_t base);
void CloseDBS2P(uint16_t base);
void InitPCjrAudio(void);
void ClosePCjrAudio(void);
void SetPCjrAudioPeriod(uint8_t chan, uint16_t period);
void SetPCjrAudioVolume(uint8_t chan, uint8_t volume);
void SetPCjrAudio(uint8_t chan, uint16_t freq, uint8_t volume);
void InitPCSpeaker(void);
void ClosePCSpeaker(void);
void ResetYM3812(void);
void SetYMF262(uint8_t opl3, uint8_t fourOp);
void ResetYMF262(void);

#endif /* _PREPLAYER_H_ */