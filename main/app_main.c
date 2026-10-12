#include "app_common.h"
#include "app_console.h"
#include "app_littlefs.h"
#include "app_speaker.h"

static const char *TAG = "APP_MAIN";

void app_main(void)
{

    LREP("MAIN", "========================================");
    LREP("MAIN", "  %s  v%s", DF_FIRMWARE_NAME, app_common_GetVersion());
    LREP("MAIN", "  Target : %s | HW: %s", CONFIG_IDF_TARGET, DF_HARDWARE_VERSION);
    LREP("MAIN", "========================================");

    /* Khởi động LittleFS */
    if (app_littlefs_Init() == ERROR_CODE_OK) {
        
        /* Ghi thử file cấu hình JSON */
        const char *pcConfigSample = "{\"device\":\"DOOR_GATE_V2\",\"pulse_time_ms\":500}";
        app_littlefs_WriteTextFile("/storage/config.json", pcConfigSample);

        /* Đọc lại file cấu hình */
        char *pcReadJson = NULL;
        if (app_littlefs_ReadTextFile("/storage/config.json", &pcReadJson) == ERROR_CODE_OK) {
            LREP(TAG, "Noi dung doc duoc: %s", pcReadJson);
            
            /* Giải phóng bộ nhớ an toàn */
            FREE_SAFE(pcReadJson);
        }

        app_speaker_Init();
    }

    /* Khởi động console sau khi các dịch vụ nền đã sẵn sàng */
    app_console_Init();

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