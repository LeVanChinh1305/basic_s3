#ifndef APP_SPEAKER_H_
#define APP_SPEAKER_H_

#include "app_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum{
    SPEAKER_SOUND_WAKE = 0,               /* beep.wav */
    SPEAKER_SOUND_STARTING_UP,            /* starting_up.wav */
    SPEAKER_SOUND_STARTUP_SUCCESS,        /* startup_successful.wav */
    SPEAKER_SOUND_AUTO_MODE,              /* auto_mode.wav */
    SPEAKER_SOUND_MANUAL_MODE,            /* manual_mode.wav */
    SPEAKER_SOUND_OTA_RUNNING,            /* update_ota_running.wav */
    SPEAKER_SOUND_OTA_SUCCESS,            /* update_ota_success.wav */
    SPEAKER_SOUND_OTA_FAIL,               /* update_ota_fail.wav */
    SPEAKER_SOUND_MAX
} app_speaker_sound_t;



/* Prototypes API xuất */
int32_t app_speaker_Init(void);
void    app_speaker_EnableAmp(bool bEnable);
int32_t app_speaker_SetVolume(float fPercent);
float   app_speaker_GetVolume(void);
int32_t app_speaker_PlayByName(const char *pcFilename);
void    app_speaker_PlayBeep(int32_t s32FreqHz, int32_t s32DurationMs, int32_t s32VolumePercent);
int32_t app_speaker_PlayByID(app_speaker_sound_t eSound);

#ifdef __cplusplus
}
#endif

#endif /* APP_SPEAKER_H_ */