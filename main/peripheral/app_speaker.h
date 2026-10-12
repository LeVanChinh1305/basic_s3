#ifndef APP_SPEAKER_H_
#define APP_SPEAKER_H_

#include "app_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Prototypes API xuất */
int32_t app_speaker_Init(void);
void    app_speaker_EnableAmp(bool bEnable);
int32_t app_speaker_SetVolume(float fPercent);
float   app_speaker_GetVolume(void);
int32_t app_speaker_PlayByName(const char *pcFilename);
void    app_speaker_PlayBeep(int32_t s32FreqHz, int32_t s32DurationMs, int32_t s32VolumePercent);

#ifdef __cplusplus
}
#endif

#endif /* APP_SPEAKER_H_ */