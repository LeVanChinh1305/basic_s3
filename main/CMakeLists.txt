set(MAIN_SRCS
    "app_main.c"
    "app_common.c"
    "console/app_console.c"
    "app_littlefs.c"
    "peripheral/app_speaker.c"
    "peripheral/app_button.c"
    "network/app_wifi.c"
    "network/app_blufi.c"
    "json/app_cmd.c"
)

# BLE Mesh tạm tắt (bật lại khi cần)
# list(APPEND MAIN_SRCS "network/app_blemesh.c" "network/app_blemesh_prov.c")

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS
        "."
        "console"
        "peripheral"
        "network"
        "json"
    REQUIRES
        freertos
        console
        esp_timer
        joltwallet__littlefs
        driver
        json
        nvs_flash
        bt
        esp_wifi
        esp_netif
        esp_event
)

littlefs_create_partition_image(storage ../littlefs_voice FLASH_IN_PROJECT)
