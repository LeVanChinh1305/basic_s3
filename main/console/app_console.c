#include "app_console.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "app_littlefs.h"
#include "app_speaker.h"

/* ================== PROMPT & CONFIG ================== */
#define PROMPT_STR          CONFIG_IDF_TARGET
#define DF_CONSOLE_MAX_CMDLINE_LENGTH   128
#define DF_CONSOLE_TASK_STACK_SIZE      (4096)

/* ================== COMMAND HANDLERS ================== */

static int app_console_CmdVersion(int argc, char **argv)
{
    printf("Name    : %s\n", DF_FIRMWARE_NAME);
    printf("Version : %s\n", app_common_GetVersion());
    printf("Target  : %s\n", CONFIG_IDF_TARGET);
    printf("IDF     : %s\n", esp_get_idf_version());
    return 0;
}

static int app_console_CmdRestart(int argc, char **argv)
{
    printf("Restarting...\n");
    esp_restart();
    return 0;
}

static int app_console_CmdDebug(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: debug <0|1>\n");
        return 1;
    }

    int iVal = atoi(argv[1]);
    app_common_SetDebug(iVal != 0);
    printf("Debug is now %s\n", g_bDebug ? "ON" : "OFF");
    return 0;
}

static int app_console_CmdHeap(int argc, char **argv)
{
    size_t sram_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t sram_min   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    printf("Internal SRAM Free : %u bytes (Min Free: %u bytes)\n", (unsigned int)sram_free, (unsigned int)sram_min);
    printf("External PSRAM Free: %u bytes\n", (unsigned int)psram_free);
    return 0;
}

static int app_console_CmdLS(int argc, char **argv)
{
    DIR *pDir = opendir(DF_LITTLEFS_MOUNT_POINT);
    if (!pDir) {
        printf("Khong the mo thu muc %s\n", DF_LITTLEFS_MOUNT_POINT);
        return 1;
    }

    struct dirent *pEntry;
    struct stat st;
    char acFullPath[300];
    uint32_t u32TotalSize = 0;
    uint32_t u32FileCount = 0;

    printf("\n--- DANH SÁCH FILE CHI TIẾT TRONG %s ---\n", DF_LITTLEFS_MOUNT_POINT);
    printf("%-25s | %-12s | %s\n", "Tên File", "Kích thước", "Loại");
    printf("---------------------------------------------------\n");

    while ((pEntry = readdir(pDir)) != NULL) {
        /* Tạo đường dẫn tuyệt đối: /storage/filename */
        snprintf(acFullPath, sizeof(acFullPath), "%s/%s", DF_LITTLEFS_MOUNT_POINT, pEntry->d_name);

        /* Lấy thông tin thuộc tính của file */
        if (stat(acFullPath, &st) == 0) {
            bool bIsDir = S_ISDIR(st.st_mode);
            
            if (bIsDir) {
                printf("%-24s | %-10s | [DIR]\n", pEntry->d_name, "<DIR>");
            } else {
                printf("%-24s | %6ld Bytes | [FILE]\n", pEntry->d_name, (long)st.st_size);
                u32TotalSize += st.st_size;
                u32FileCount++;
            }
        } else {
            printf("%-24s | %-10s | [UNKNOWN]\n", pEntry->d_name, "Error");
        }
    }

    closedir(pDir);

    printf("---------------------------------------------------\n");
    printf("Tổng số file: %u | Tổng dung lượng: %u Bytes (%.2f KB)\n\n", 
           (unsigned int)u32FileCount, 
           (unsigned int)u32TotalSize, 
           (float)u32TotalSize / 1024.0f);

    return 0;
}

static int app_console_CmdCat(int argc, char **argv)
{
    if (argc < 2) {
        printf("Sử dụng: cat <ten_file> (Ví dụ: cat config.json)\n");
        return 1;
    }

    char acFullPath[256];
    /* Nếu người dùng chỉ gõ tên file, tự động nối với /storage/ */
    if (argv[1][0] == '/') {
        snprintf(acFullPath, sizeof(acFullPath), "%s", argv[1]);
    } else {
        snprintf(acFullPath, sizeof(acFullPath), "%s/%s", DF_LITTLEFS_MOUNT_POINT, argv[1]);
    }

    char *pcFileData = NULL;
    int32_t s32Ret = app_littlefs_ReadTextFile(acFullPath, &pcFileData);

    if (s32Ret == ERROR_CODE_OK && pcFileData != NULL) {
        printf("\n--- NỘI DUNG FILE: %s ---\n", acFullPath);
        printf("%s\n", pcFileData);
        printf("-----------------------------------\n\n");
        FREE_SAFE(pcFileData);
    } else {
        printf("Không thể đọc file: %s (Mã lỗi: %d)\n", acFullPath, (int)s32Ret);
    }

    return 0;
}

static int app_console_CmdPlay(int argc, char **argv)
{
    if (argc < 2) {
        printf("Sử dụng: play <ten_file.wav> (Ví dụ: play starting_up.wav)\n");
        return 1;
    }

    /* Tự động ghép /storage/ nếu người dùng chỉ gõ tên file */
    char acFullPath[128];
    if (argv[1][0] == '/') {
        snprintf(acFullPath, sizeof(acFullPath), "%s", argv[1]);
    } else {
        snprintf(acFullPath, sizeof(acFullPath), "%s/%s", DF_LITTLEFS_MOUNT_POINT, argv[1]);
    }

    printf("Đang kích hoạt phát file: %s ...\n", acFullPath);
    int32_t s32Ret = app_speaker_PlayByName(acFullPath);
    if (s32Ret != ERROR_CODE_OK) {
        printf("Phát file thất bại! Mã lỗi: %d\n", (int)s32Ret);
    }

    return 0;
}

static int app_console_CmdVolume(int argc, char **argv)
{
    if (argc < 2) {
        printf("Âm lượng hiện tại: %.1f%%\n", app_speaker_GetVolume());
        printf("Sử dụng: volume <0-100> (Ví dụ: volume 80)\n");
        return 0;
    }

    float fVol = (float)atof(argv[1]);
    if (fVol < 0.0f || fVol > 100.0f) {
        printf("Âm lượng không hợp lệ! Vui lòng nhập từ 0 đến 100.\n");
        return 1;
    }

    app_speaker_SetVolume(fVol);
    printf("Đã cài đặt âm lượng hệ thống: %.1f%%\n", fVol);
    return 0;
}

static int app_console_CmdBeep(int argc, char **argv)
{
    int s32Freq = (argc > 1) ? atoi(argv[1]) : 2000; // Tần số Hz (Mặc định 2000Hz)
    int s32Dur  = (argc > 2) ? atoi(argv[2]) : 200;  // Thời lượng ms (Mặc định 200ms)
    int s32Vol  = (argc > 3) ? atoi(argv[3]) : 80;   // % âm lượng (Mặc định 80%)

    printf("Phát tiếng Beep: %d Hz, %d ms, %d%%\n", s32Freq, s32Dur, s32Vol);
    app_speaker_PlayBeep(s32Freq, s32Dur, s32Vol);
    return 0;
}

/* ================== REGISTER COMMANDS ================== */

static void app_console_RegisterCommands(void)
{
    const esp_console_cmd_t stCmdVersion = {
        .command = "version",
        .help    = "Show firmware version",
        .hint    = NULL,
        .func    = &app_console_CmdVersion,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stCmdVersion));

    const esp_console_cmd_t stCmdRestart = {
        .command = "restart",
        .help    = "Restart the chip",
        .hint    = NULL,
        .func    = &app_console_CmdRestart,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stCmdRestart));

    const esp_console_cmd_t stCmdDebug = {
        .command = "debug",
        .help    = "Turn debug log on/off (debug 0|1)",
        .hint    = NULL,
        .func    = &app_console_CmdDebug,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stCmdDebug));

    const esp_console_cmd_t stCmdHeap = {
        .command = "heap",
        .help    = "Show free heap memory",
        .hint    = NULL,
        .func    = &app_console_CmdHeap,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stCmdHeap));

    const esp_console_cmd_t stCmdLS = {
        .command = "ls",
        .help    = "Liet ke tat ca cac file trong LittleFS",
        .hint    = NULL,
        .func    = &app_console_CmdLS,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stCmdLS));

    const esp_console_cmd_t stCmdCat = {
        .command = "cat",
        .help    = "Xem noi dung file trong LittleFS (cat <file>)",
        .func    = &app_console_CmdCat,
        .hint    = NULL,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stCmdCat));

    const esp_console_cmd_t stCmdPlay = {
        .command = "play",
        .help    = "Phat file am thanh WAV tu LittleFS (play <file.wav>)",
        .hint    = NULL,
        .func    = &app_console_CmdPlay,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stCmdPlay));

    const esp_console_cmd_t stCmdVolume = {
        .command = "volume",
        .help    = "Cai dat hoac xem am luong (volume [0-100])",
        .hint    = NULL,
        .func    = &app_console_CmdVolume,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stCmdVolume));

    const esp_console_cmd_t stCmdBeep = {
        .command = "beep",
        .help    = "Phat tieng Beep thu nghiem (beep <freq> <duration_ms> <vol>)",
        .hint    = NULL,
        .func    = &app_console_CmdBeep,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stCmdBeep));
}

/* ================== INIT ================== */

void app_console_Init(void)
{
    esp_console_repl_t *pRepl = NULL;
    esp_console_repl_config_t stReplConfig = ESP_CONSOLE_REPL_CONFIG_DEFAULT();

    stReplConfig.prompt = PROMPT_STR ">";
    stReplConfig.max_cmdline_length = DF_CONSOLE_MAX_CMDLINE_LENGTH;
    stReplConfig.task_stack_size = DF_CONSOLE_TASK_STACK_SIZE;

    /* Đăng ký lệnh help mặc định */
    esp_console_register_help_command();

    /* Đăng ký các lệnh của mình */
    app_console_RegisterCommands();

#if CONFIG_ESP_CONSOLE_UART
    /* Dùng UART (phổ biến nhất) */
    esp_console_dev_uart_config_t stHwConfig = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&stHwConfig, &stReplConfig, &pRepl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    /* Dùng USB CDC (nếu board hỗ trợ) */
    esp_console_dev_usb_cdc_config_t stHwConfig = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&stHwConfig, &stReplConfig, &pRepl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    /* Dùng USB Serial JTAG */
    esp_console_dev_usb_serial_jtag_config_t stHwConfig = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&stHwConfig, &stReplConfig, &pRepl));
#else
    #error "Unsupported console type"
#endif

    printf("\n");
    printf("========================================\n");
    printf("  %s Console  v%s\n", DF_FIRMWARE_NAME, app_common_GetVersion());
    printf("  Gõ 'help' để xem danh sách lệnh \n");
    printf("========================================\n\n");

    /* Khởi động REPL (chạy trong task riêng) */
    ESP_ERROR_CHECK(esp_console_start_repl(pRepl));
}