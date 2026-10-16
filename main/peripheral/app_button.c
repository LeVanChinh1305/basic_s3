#include "app_button.h"
#include "app_config.h"
#include "app_speaker.h"

#if DF_ENABLE_BLUFI
#include "app_blufi.h"
#endif
#if DF_ENABLE_WIFI
#include "app_wifi.h"
#endif

#include "driver/gpio.h"
#include "nvs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_button";

/* ================== TRẠNG THÁI ================== */
static bool         g_bInited        = false;
static bool         g_bPressed       = false;
static bool         g_bRawLevel      = false;
static uint32_t     g_u32DebounceCnt = 0;
static uint32_t     g_u32HoldMs      = 0;
static bool         g_bHold3sFired   = false;
static bool         g_bHold10sFired  = false;
static TaskHandle_t g_hTask          = NULL;

/* ================== XỬ LÝ SỰ KIỆN (nội bộ) ================== */

static void app_button_OnHold3s(void)
{
    LREP(TAG, "HOLD >= 3s → BluFi provisioning");

    /* Beep phản hồi */
    app_speaker_PlayBeep(1000, 150, 80);
    app_common_DelayMs(200);
    app_speaker_PlayBeep(1500, 150, 80);

#if DF_ENABLE_BLUFI
    if (app_blufi_IsRunning()) {
        LREP_WARNING(TAG, "BluFi dang chay, bo qua");
        return;
    }
    int32_t ret = app_blufi_Start();
    if (ret != ERROR_CODE_OK) {
        LREP_ERROR(TAG, "app_blufi_Start failed: %ld", (long)ret);
        app_speaker_PlayBeep(400, 300, 80);
    }
#else
    LREP_WARNING(TAG, "DF_ENABLE_BLUFI = 0, khong the start BluFi");
#endif
}

static void app_button_OnHold10s(void)
{
    LREP(TAG, "HOLD >= 10s → Factory reset (xoa Wi-Fi config)");

    app_speaker_PlayBeep(500, 100, 80);
    app_common_DelayMs(100);
    app_speaker_PlayBeep(500, 100, 80);
    app_common_DelayMs(100);
    app_speaker_PlayBeep(500, 100, 80);

#if DF_ENABLE_WIFI
    app_wifi_Disconnect();
#endif

    /* Xóa cấu hình Wi-Fi trong NVS */
    nvs_handle_t h;
    if (nvs_open("nvs.net80211", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
        LREP(TAG, "Da xoa Wi-Fi NVS");
    }

    app_common_DelayMs(500);
    esp_restart();
}

/* ================== TASK QUÉT ================== */

static inline bool app_button_ReadRaw(void)
{
    return (gpio_get_level(DF_BUTTON_PIN) == DF_BUTTON_ACTIVE_LEVEL);
}

static void app_button_Task(void *pvParam)
{
    (void)pvParam;
    const uint32_t u32PeriodMs       = DF_BUTTON_SCAN_PERIOD_MS;
    const uint32_t u32DebounceTicks  = (DF_BUTTON_DEBOUNCE_MS + u32PeriodMs - 1) / u32PeriodMs;

    LREP(TAG, "Button task started (GPIO %d, period %ums)",
         DF_BUTTON_PIN, (unsigned)u32PeriodMs);

    while (1) {
        bool bRaw = app_button_ReadRaw();

        /* Debounce */
        if (bRaw != g_bRawLevel) {
            g_bRawLevel      = bRaw;
            g_u32DebounceCnt = 0;
        } else if (g_u32DebounceCnt < u32DebounceTicks) {
            g_u32DebounceCnt++;
        }

        bool bStable = g_bPressed;
        if (g_u32DebounceCnt >= u32DebounceTicks) {
            bStable = g_bRawLevel;
        }

        /* Cạnh xuống — nhấn */
        if (bStable && !g_bPressed) {
            g_bPressed      = true;
            g_u32HoldMs     = 0;
            g_bHold3sFired  = false;
            g_bHold10sFired = false;
            LREP_DEBUG(TAG, "PRESS");
        }
        /* Cạnh lên — nhả */
        else if (!bStable && g_bPressed) {
            g_bPressed = false;
            LREP_DEBUG(TAG, "RELEASE (hold %u ms)", (unsigned)g_u32HoldMs);
            g_u32HoldMs = 0;
        }
        /* Đang giữ */
        else if (g_bPressed) {
            g_u32HoldMs += u32PeriodMs;

            if (!g_bHold3sFired && g_u32HoldMs >= DF_BUTTON_HOLD_3S_MS) {
                g_bHold3sFired = true;
                app_button_OnHold3s();
            }
            if (!g_bHold10sFired && g_u32HoldMs >= DF_BUTTON_HOLD_10S_MS) {
                g_bHold10sFired = true;
                app_button_OnHold10s();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(u32PeriodMs));
    }
}

/* ================== API ================== */

int32_t app_button_Init(void)
{
    if (g_bInited) {
        return ERROR_CODE_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << DF_BUTTON_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        LREP_ERROR(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return ERROR_CODE_FAIL;
    }

    g_bRawLevel      = app_button_ReadRaw();
    g_bPressed       = g_bRawLevel;
    g_u32DebounceCnt = 0;
    g_u32HoldMs      = 0;
    g_bHold3sFired   = false;
    g_bHold10sFired  = false;

    BaseType_t ret = xTaskCreate(app_button_Task, "app_btn", 3072, NULL, 6, &g_hTask);
    if (ret != pdPASS) {
        LREP_ERROR(TAG, "Create button task failed");
        return ERROR_CODE_NO_MEMORY;
    }

    g_bInited = true;
    LREP(TAG, "Button init OK (GPIO %d)", DF_BUTTON_PIN);
    return ERROR_CODE_OK;
}

bool app_button_IsPressed(void)
{
    return g_bPressed;
}
