#include "app_common.h"
#include "app_console.h"
#include "app_littlefs.h"
#include "app_speaker.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "app_config.h"

#if DF_ENABLE_BLE_MESH
#include "app_blemesh.h"
#include "app_blemesh_prov.h"
#endif
#if DF_ENABLE_BUTTON
#include "app_button.h"
#endif
#if DF_ENABLE_WIFI
#include "app_wifi.h"
#endif
#if DF_ENABLE_BLUFI
#include "app_blufi.h"
#endif

static const char *TAG = "APP_MAIN";

static void app_main_InitNvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    LREP(TAG, "========================================");
    LREP(TAG, "  %s  v%s", DF_FIRMWARE_NAME, app_common_GetVersion());
    LREP(TAG, "  Target : %s | HW: %s", CONFIG_IDF_TARGET, DF_HARDWARE_VERSION);
    LREP(TAG, "========================================");

    app_console_Init();
    app_main_InitNvs();
    app_speaker_Init();

#if DF_ENABLE_LITTLEFS
    app_littlefs_Init();
#endif
#if DF_ENABLE_WIFI
    if (app_wifi_Init() != ERROR_CODE_OK)
        LREP_ERROR(TAG, "Wi-Fi init THAT BAI!");
#endif
#if DF_ENABLE_BLUFI
    app_blufi_Init();
#endif
#if DF_ENABLE_BUTTON
    if (app_button_Init() != ERROR_CODE_OK)
        LREP_ERROR(TAG, "Button init THAT BAI!");
#endif
#if DF_ENABLE_BLE_MESH
    if (app_mesh_prov_Init() != ERROR_CODE_OK)
        LREP_ERROR(TAG, "BLE Mesh init THAT BAI!");
#endif

    app_speaker_PlayByID(SPEAKER_SOUND_STARTUP_SUCCESS);
    LREP(TAG, "San sang. Nhan giu nut 3s de them thiet bi (BluFi).");

    uint32_t u32Count100ms = 0;
    while (1) {
        u32Count100ms++;
        if (u32Count100ms % 100 == 0) {
            size_t sram_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            LREP("MAIN", "Free SRAM: %u KB | PSRAM: %u KB | BluFi=%d WiFi=%d",
                 (unsigned)(sram_free / 1024), (unsigned)(psram_free / 1024),
#if DF_ENABLE_BLUFI
                 app_blufi_IsRunning() ? 1 : 0,
#else
                 0,
#endif
#if DF_ENABLE_WIFI
                 app_wifi_IsConnected() ? 1 : 0
#else
                 0
#endif
            );
        }
        app_common_DelayMs(100);
    }
}