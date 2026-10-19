#include "app_speaker.h"
#include "app_littlefs.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include <math.h>
#include "app_config.h"

static const char *TAG = "app_speaker";

/* ================== TÀI NGUYÊN HỆ THỐNG ================== */
#define DF_SPEAKER_PATH_MAX_LENGTH 256

static i2s_chan_handle_t g_stI2sTxChan         = NULL;
static SemaphoreHandle_t g_stSpeakerSmp        = NULL;
static TaskHandle_t      g_stSpeakerTaskHandle = NULL;
static bool              g_bSpeakerRunning     = false;
static bool              g_bSpeakerInitialized = false;

static float             g_fGlobalVolumePercent = 100.0f;
static float             g_fCurrentOutputGain   = 1.0f;
static char              g_acCurrentPlayPath[DF_SPEAKER_PATH_MAX_LENGTH];

/* Cấu trúc trạng thái DSP */
typedef struct {
    float fPrevX;
    float fPrevY;
    float fEnvelope;
    float fGateEnvelope;
    float fGateGain;
} st_speaker_dsp_state_t;

static st_speaker_dsp_state_t g_stDspState;

/* Hằng số DSP */
static const float g_fAudioDriveGain       = 8.0f;
static const float g_fAudioMaxCeiling      = 21000.0f;
static const float g_fAudioSoftClipThresh  = 0.7f;
static const float g_fAudioHpfAlpha        = 0.7f;
static const float g_fAudioCompThresh      = 0.7f;
static const float g_fAudioCompRatio       = 70.0f;
static const float g_fAudioCompAttack      = 1.0f;
static const float g_fAudioCompRelease     = 0.0002f;
static float       g_fAudioNoiseGateThresh = 0.15f;
static const float g_fAudioNoiseGateAtten  = 0.05f;

//====================mảng ánh xạ tên file âm thanh ==============
static const char *g_apcSoundFiles[SPEAKER_SOUND_MAX] = {
    [SPEAKER_SOUND_WAKE]           = "beep.wav",
    [SPEAKER_SOUND_STARTING_UP]    = "starting_up.wav",
    [SPEAKER_SOUND_STARTUP_SUCCESS]= "startup_successful.wav",
    [SPEAKER_SOUND_AUTO_MODE]      = "auto_mode.wav",
    [SPEAKER_SOUND_MANUAL_MODE]    = "manual_mode.wav",
    [SPEAKER_SOUND_OTA_RUNNING]    = "update_ota_running.wav",
    [SPEAKER_SOUND_OTA_SUCCESS]    = "update_ota_success.wav",
    [SPEAKER_SOUND_OTA_FAIL]       = "update_ota_fail.wav",
};

/* ================== THUẬT TOÁN DSP ÂM THANH ================== */

static inline float app_speaker_ClampF32(float fVal, float fMin, float fMax)
{
    if (fVal < fMin) return fMin;
    if (fVal > fMax) return fMax;
    return fVal;
}

static void app_speaker_ResetDspState(void)
{
    g_stDspState.fPrevX        = 0.0f;
    g_stDspState.fPrevY        = 0.0f;
    g_stDspState.fEnvelope     = 0.0f;
    g_stDspState.fGateEnvelope = 0.0f;
    g_stDspState.fGateGain     = 1.0f;
}

static void app_speaker_ApplyDspVolume(float fPercent)
{
    fPercent = app_speaker_ClampF32(fPercent, 0.0f, 100.0f);
    g_fCurrentOutputGain = fPercent / 100.0f;
}

static void app_speaker_ProcessAudioPipeline(int16_t *ps16Src, int32_t s32Samples)
{
    if (ps16Src == NULL || s32Samples <= 0) return;

    if (g_fCurrentOutputGain <= 0.0f) {
        memset(ps16Src, 0, s32Samples * sizeof(int16_t));
        return;
    }

    for (int32_t i = 0; i < s32Samples; i++) {
        float fX = (float)ps16Src[i] * g_fAudioDriveGain;

        /* 1. HPF Filter - Cắt bass thừa */
        float fY = fX - g_stDspState.fPrevX + g_fAudioHpfAlpha * g_stDspState.fPrevY;
        g_stDspState.fPrevX = fX;
        g_stDspState.fPrevY = fY;
        fX = fY / g_fAudioMaxCeiling;

        /* 2. Noise Gate - Cắt nhiễu nền */
        float fAbsXGate = fabsf(fX);
        if (fAbsXGate > g_stDspState.fGateEnvelope) {
            g_stDspState.fGateEnvelope = 0.1f * fAbsXGate + 0.9f * g_stDspState.fGateEnvelope;
        } else {
            g_stDspState.fGateEnvelope = 0.001f * fAbsXGate + 0.999f * g_stDspState.fGateEnvelope;
        }

        /* Lọc Noise Gate làm mịn chống tiếng bụp đầu */
        float fTargetGain = (g_stDspState.fGateEnvelope > g_fAudioNoiseGateThresh) ? 1.0f : g_fAudioNoiseGateAtten;
        if (fTargetGain > g_stDspState.fGateGain) {
            /* Đổi từ 0.1f xuống 0.01f để mở từ từ mượt mà hơn */
            g_stDspState.fGateGain += 0.01f * (fTargetGain - g_stDspState.fGateGain); 
        } else {
            g_stDspState.fGateGain += 0.002f * (fTargetGain - g_stDspState.fGateGain);
        }
        fX = fX * g_stDspState.fGateGain;

        /* 3. Dynamic Compressor - Hãm tiếng quá to */
        float fAbsX = fabsf(fX);
        if (fAbsX > g_stDspState.fEnvelope) {
            g_stDspState.fEnvelope = g_fAudioCompAttack * fAbsX + (1.0f - g_fAudioCompAttack) * g_stDspState.fEnvelope;
        } else {
            g_stDspState.fEnvelope = g_fAudioCompRelease * fAbsX + (1.0f - g_fAudioCompRelease) * g_stDspState.fEnvelope;
        }

        if (g_stDspState.fEnvelope > g_fAudioCompThresh) {
            float fOver = g_stDspState.fEnvelope - g_fAudioCompThresh;
            float fCompressedOver = fOver / g_fAudioCompRatio;
            float fGainReduction = (g_fAudioCompThresh + fCompressedOver) / g_stDspState.fEnvelope;
            fX = fX * fGainReduction;
        }

        /* 4. Limiter - Anti Clipping */
        float fAbsXClip = fabsf(fX);
        if (fAbsXClip > 1.0f) {
            fX = (fX > 0) ? 1.0f : -1.0f;
        } else if (fAbsXClip > g_fAudioSoftClipThresh) {
            float fOver = fAbsXClip - g_fAudioSoftClipThresh;
            float fSmooth = g_fAudioSoftClipThresh + (fOver - (fOver * fOver) / (2.0f * (1.0f - g_fAudioSoftClipThresh)));
            fX = (fX > 0) ? fSmooth : -fSmooth;
        }

        ps16Src[i] = (int16_t)(fX * g_fAudioMaxCeiling * g_fCurrentOutputGain);
    }
}

/* ================== QUẢN LÝ THAO TÁC ÂM THANH ================== */

void app_speaker_EnableAmp(bool bEnable)
{
    gpio_set_level(DF_SPK_EN_PIN, bEnable ? 1 : 0);
}

static int32_t app_speaker_PlayFileInternal(const char *pcFilePath, float fEffectiveVol)
{
    FILE *pFile = fopen(pcFilePath, "rb");
    if (pFile == NULL) {
        LREP_ERROR(TAG, "File %s khong ton tai trong LittleFS!", pcFilePath);
        return ERROR_CODE_NOT_FOUND;
    }

    /* Bỏ qua 44 bytes WAV header */
    fseek(pFile, 44, SEEK_SET);

    app_speaker_ResetDspState();
    app_speaker_ApplyDspVolume(fEffectiveVol);

    uint8_t *pu8PlayBuf = (uint8_t *)MALLOC_PSRAM(DF_SPK_PLAY_BUF_SIZE);
    if (pu8PlayBuf == NULL) {
        LREP_ERROR(TAG, "Khong du PSRAM de cap phat Play Buffer!");
        fclose(pFile);
        return ERROR_CODE_NO_MEMORY;
    }

    size_t u32BytesWritten = 0;

    /* STEP 1: Bật I2S trước */
    i2s_channel_enable(g_stI2sTxChan);

    /* STEP 2: Ghi sẵn 512 byte mẫu câm (0x00) vào DMA để ổn định đường bus I2S */
    memset(pu8PlayBuf, 0, 512);
    i2s_channel_write(g_stI2sTxChan, pu8PlayBuf, 512, &u32BytesWritten, pdMS_TO_TICKS(50));

    /* STEP 3: Mới kích hoạt chân Enable Amplifier MAX98357A */
    app_speaker_EnableAmp(true);
    app_common_DelayMs(30); // Trễ 30ms cho tụ lọc nguồn Amp ổn định

    size_t  u32BytesRead = 0;
    int32_t s32FadeInSamples = 16000 * 0.04f; // 40ms Fade-In ở 16kHz
    int32_t s32ProcessedSamples = 0;

    /* Vòng lặp đọc block PCM -> Lọc DSP -> Đẩy ra I2S */
    while ((u32BytesRead = fread(pu8PlayBuf, 1, DF_SPK_PLAY_BUF_SIZE, pFile)) > 0) {
        int16_t *ps16Pcm = (int16_t *)pu8PlayBuf;
        int32_t s32Samples = u32BytesRead / sizeof(int16_t);

        /* 1. Lọc DSP */
        app_speaker_ProcessAudioPipeline(ps16Pcm, s32Samples);

        /* 2. Soft Fade-In mượt mà cho 40ms đầu tiên */
        for (int32_t i = 0; i < s32Samples; i++) {
            if (s32ProcessedSamples < s32FadeInSamples) {
                float fFadeMultiplier = 0.5f * (1.0f - cosf(3.14159265f * (float)s32ProcessedSamples / (float)s32FadeInSamples));
                ps16Pcm[i] = (int16_t)((float)ps16Pcm[i] * fFadeMultiplier);
                s32ProcessedSamples++;
            }
        }

        i2s_channel_write(g_stI2sTxChan, ps16Pcm, u32BytesRead, &u32BytesWritten, pdMS_TO_TICKS(1000));
    }

    /* Đẩy 1024 mẫu câm xả cạn DMA trước khi tắt Amp */
    memset(pu8PlayBuf, 0, 1024 * sizeof(int16_t));
    i2s_channel_write(g_stI2sTxChan, pu8PlayBuf, 1024 * sizeof(int16_t), &u32BytesWritten, pdMS_TO_TICKS(100));

    /* Tắt Amp trước, tắt I2S sau */
    app_speaker_EnableAmp(false);
    app_common_DelayMs(10);
    i2s_channel_disable(g_stI2sTxChan);

    FREE_SAFE(pu8PlayBuf);
    fclose(pFile);

    return ERROR_CODE_OK;
}

static void app_speaker_Task(void *pvParam)
{
    while (1) {
        if (xSemaphoreTake(g_stSpeakerSmp, portMAX_DELAY) == pdTRUE) {
            char acFilePath[DF_SPEAKER_PATH_MAX_LENGTH];
            snprintf(acFilePath, sizeof(acFilePath), "%s", g_acCurrentPlayPath);
            app_speaker_PlayFileInternal(acFilePath, g_fGlobalVolumePercent);
            g_bSpeakerRunning = false;
        }
    }
}

/* ================== PUBLIC APIS ================== */

int32_t app_speaker_Init(void)
{
    if (g_bSpeakerInitialized) return ERROR_CODE_OK;

    gpio_config_t stGpioConf = {
        .pin_bit_mask = (1ULL << DF_SPK_EN_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&stGpioConf);
    gpio_set_level(DF_SPK_EN_PIN, 0);

    i2s_chan_config_t stChanCfg = I2S_CHANNEL_DEFAULT_CONFIG(DF_SPK_I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&stChanCfg, &g_stI2sTxChan, NULL));

    i2s_std_config_t stStdCfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(DF_SPK_SAMPLE_RATE_DEFAULT),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = DF_I2S_BCLK_PIN,
            .ws   = DF_I2S_WS_PIN,
            .dout = DF_I2S_DOUT_PIN,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(g_stI2sTxChan, &stStdCfg));

    g_stSpeakerSmp = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(app_speaker_Task, "app_spk_task", 4096, NULL, 10, &g_stSpeakerTaskHandle, 1);

    g_bSpeakerInitialized = true;
    LREP(TAG, "Khoi tao Speaker Driver thanh cong!");
    return ERROR_CODE_OK;
}

int32_t app_speaker_PlayByName(const char *pcFilename)
{
    if (pcFilename == NULL || pcFilename[0] == '\0') {
        return ERROR_CODE_INVALID_PARAM;
    }
    if (!g_bSpeakerInitialized) {
        LREP_ERROR(TAG, "Speaker chua duoc khoi tao!");
        return ERROR_CODE_INVALID_PARAM;
    }
    if (g_bSpeakerRunning) {
        LREP_WARNING(TAG, "Speaker dang phat am thanh khác, vui long cho!");
        return ERROR_CODE_BUSY;
    }

    int32_t s32PathLength;
    if (pcFilename[0] == '/') {
        s32PathLength = snprintf(g_acCurrentPlayPath, sizeof(g_acCurrentPlayPath), "%s", pcFilename);
    } else {
        s32PathLength = snprintf(g_acCurrentPlayPath, sizeof(g_acCurrentPlayPath), "%s/%s",
                                 DF_LITTLEFS_MOUNT_POINT, pcFilename);
    }
    if (s32PathLength < 0 || (size_t)s32PathLength >= sizeof(g_acCurrentPlayPath)) {
        return ERROR_CODE_INVALID_PARAM;
    }

    g_bSpeakerRunning = true;
    xSemaphoreGive(g_stSpeakerSmp);
    return ERROR_CODE_OK;
}

int32_t app_speaker_SetVolume(float fPercent)
{
    g_fGlobalVolumePercent = app_speaker_ClampF32(fPercent, 0.0f, 100.0f);
    app_speaker_ApplyDspVolume(g_fGlobalVolumePercent);
    return ERROR_CODE_OK;
}

float app_speaker_GetVolume(void)
{
    return g_fGlobalVolumePercent;
}

void app_speaker_PlayBeep(int32_t s32FreqHz, int32_t s32DurationMs, int32_t s32VolumePercent)
{
    if (s32FreqHz <= 0 || s32DurationMs <= 0) return;

    int32_t s32EffectiveVol = (s32VolumePercent * (int32_t)g_fGlobalVolumePercent) / 100;
    if (s32EffectiveVol <= 0) return;

    int32_t s32Amplitude = (12000 * s32EffectiveVol) / 100;
    int32_t s32TotalSamples = (DF_SPK_SAMPLE_RATE_DEFAULT * s32DurationMs) / 1000;

    int16_t as16PcmChunk[160];
    double fPhase = 0.0;
    double fPhaseStep = (2.0 * 3.141592653589793 * (double)s32FreqHz) / (double)DF_SPK_SAMPLE_RATE_DEFAULT;

    int32_t s32Generated = 0;
    app_speaker_EnableAmp(true);
    i2s_channel_enable(g_stI2sTxChan);

    while (s32Generated < s32TotalSamples) {
        int32_t s32ChunkSamples = s32TotalSamples - s32Generated;
        if (s32ChunkSamples > 160) s32ChunkSamples = 160;

        for (int32_t i = 0; i < s32ChunkSamples; i++) {
            int32_t s32SampleIdx = s32Generated + i;
            int32_t s32Env = 1000;
            int32_t s32FadeSamples = DF_SPK_SAMPLE_RATE_DEFAULT * 5 / 1000;

            if (s32SampleIdx < s32FadeSamples) {
                s32Env = (s32SampleIdx * 1000) / s32FadeSamples;
            } else if ((s32TotalSamples - s32SampleIdx) < s32FadeSamples) {
                s32Env = ((s32TotalSamples - s32SampleIdx) * 1000) / s32FadeSamples;
            }

            double fS = sin(fPhase);
            int32_t s32Val = (int32_t)(fS * (double)s32Amplitude);
            s32Val = (s32Val * s32Env) / 1000;

            if (s32Val > 32767) s32Val = 32767;
            if (s32Val < -32768) s32Val = -32768;

            as16PcmChunk[i] = (int16_t)s32Val;

            fPhase += fPhaseStep;
            if (fPhase >= (2.0 * 3.141592653589793)) {
                fPhase -= (2.0 * 3.141592653589793);
            }
        }

        size_t u32Written = 0;
        i2s_channel_write(g_stI2sTxChan, as16PcmChunk, s32ChunkSamples * sizeof(int16_t), &u32Written, pdMS_TO_TICKS(100));
        s32Generated += s32ChunkSamples;
    }

    i2s_channel_disable(g_stI2sTxChan);
    app_speaker_EnableAmp(false);
}

int32_t app_speaker_PlayByID(app_speaker_sound_t eSound)
{
    if (eSound < 0 || eSound >= SPEAKER_SOUND_MAX) {
        return ERROR_CODE_INVALID_PARAM;
    }
    return app_speaker_PlayByName(g_apcSoundFiles[eSound]);
}
