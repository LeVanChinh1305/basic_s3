#include "app_common.h"
#include "app_console.h"
#include "app_littlefs.h"
#include "app_speaker.h"
#include "nvs_flash.h"
#include "app_blemesh.h"

static const char *TAG = "APP_MAIN";

void app_main_InitNvs() {
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
    app_littlefs_Init();
#if DF_ENABLE_BLE_MESH
    /* Khoi dong BLE Mesh sau LittleFS (cần đọc /storage/mesh_node.json) */
    if (app_mesh_Init() != ERROR_CODE_OK) {
        LREP_ERROR(TAG, "Khoi dong BLE Mesh THAT BAI!");
    }
#endif

    uint32_t u32Count100ms = 0;

    /* app_main có thể tiếp tục làm việc khác hoặc kết thúc */
    while (1) {
        /* Mỗi 100ms tăng biến đếm */
        u32Count100ms++;

        /* Mỗi 10 giây (100 * 100ms) in thông tin heap */
        if (u32Count100ms % 100 == 0) {
            size_t sram_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

            LREP("MAIN", "Free SRAM: %u KB | Free PSRAM: %u KB",
                (unsigned int)(sram_free / 1024),
                (unsigned int)(psram_free / 1024));
        }

        app_common_DelayMs(100);
    }
}