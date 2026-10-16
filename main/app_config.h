#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "driver/gpio.h"

#define DF_FIRMWARE_VERSION         "1.0.0"
#define DF_FIRMWARE_NAME            "SMART_IR_S3"
#define DEVICE_TYPE                 3096
#define DF_HARDWARE_VERSION         "HW_V1.0"
#define DF_TARGET_CHIP              CONFIG_IDF_TARGET

#define DF_LITTLEFS_MOUNT_POINT     "/storage"
#define DF_LITTLEFS_PARTITION_LABEL "storage"

#define DF_SPK_EN_PIN               GPIO_NUM_38
#define DF_I2S_BCLK_PIN             GPIO_NUM_40
#define DF_I2S_WS_PIN               GPIO_NUM_41
#define DF_I2S_DOUT_PIN             GPIO_NUM_39
#define DF_SPK_I2S_PORT             0
#define DF_SPK_SAMPLE_RATE_DEFAULT  16000
#define DF_SPK_PLAY_BUF_SIZE        (16 * 1024)
#define DF_SPK_CONFIG_FILE          "/storage/extra_cfg.json"

#define DF_BUTTON_PIN               GPIO_NUM_8
#define DF_BUTTON_ACTIVE_LEVEL      0
#define DF_BUTTON_DEBOUNCE_MS       30
#define DF_BUTTON_SCAN_PERIOD_MS    10
#define DF_BUTTON_HOLD_3S_MS        3000
#define DF_BUTTON_HOLD_10S_MS       10000

#define DF_WIFI_STA_HOSTNAME        "Vconnex"
#define DF_WIFI_SSID_MAX_LEN        32
#define DF_WIFI_PASS_MAX_LEN        64
#define DF_WIFI_CONNECT_TIMEOUT_MS  15000

#define DF_BLUFI_ADV_NAME_PREFIX    "VCONNEX-"
#define DF_BLUFI_TIMEOUT_SEC        90
#define DF_BLUFI_TASK_STACK         4096
#define DF_BLUFI_TASK_PRIO          5
#define DF_BLUFI_CUSTOM_MSG_MAX     1024

#define DF_LOCK_TIMEOUT_MS          1000

#define DF_MESH_COMPANY_ID          0x02E5
#define DF_MESH_RELAY               1
#define DF_MESH_DEVICE_NAME_PREFIX  "VCONNEX_MESH_"
#define DF_MESH_EVT_QUEUE_LEN       16
#define DF_MESH_TASK_STACK          4096
#define DF_MESH_TASK_PRIO           5
#define DF_MESH_LOG_DEPTH           32
#define DF_MESH_STATE_FILE          "/storage/mesh_node.json"
#define DF_MESH_PERSIST_DELAY_MS    2000

#define DF_ENABLE_BLE_MESH          0
#define DF_ENABLE_WIFI              1
#define DF_ENABLE_BLUFI             1
#define DF_ENABLE_MQTT              0
#define DF_ENABLE_LITTLEFS          1
#define DF_ENABLE_PSRAM             1
#define DF_ENABLE_BUTTON            1

#define ERROR_CODE_OK                           50000
#define ERROR_CODE_FAIL                         50001
#define ERROR_CODE_UNDEFINED                    50004
#define ERROR_CODE_INVALID_PARAM                50005
#define ERROR_CODE_TIMEOUT                      50010
#define ERROR_CODE_NO_MEMORY                    50011
#define ERROR_CODE_NOT_SUPPORT                  50012
#define ERROR_CODE_BUSY                         50013
#define ERROR_CODE_NOT_FOUND                    50014
#define ERROR_CODE_JSON_FORMAT                  50206
#define ERROR_CODE_OTA_WIFI_NOT_CONNECTED       50201
#define ERROR_CODE_OTA_FIRMWARE_NOT_DOWNLOADED  50202
#define ERROR_CODE_OTA_FIRMWARE_NOT_VALID       50203
#define ERROR_CODE_OTA_WIFI_NOT_CONFIG          50204
#define ERROR_CODE_WIFI_NOT_CONNECTED           50301
#define ERROR_CODE_WIFI_CONNECT_FAIL            50302
#define ERROR_CODE_BLUFI_ALREADY_RUNNING        50310
#define ERROR_CODE_BLUFI_INIT_FAIL              50311

#define DF_CHECK_NULL_PARAM(ptr) do { \
    if ((ptr) == NULL) { \
        ESP_LOGE("PARAM", "Loi con tro NULL tai %s:%d", __FUNCTION__, __LINE__); \
        return ERROR_CODE_INVALID_PARAM; \
    } \
} while (0)

