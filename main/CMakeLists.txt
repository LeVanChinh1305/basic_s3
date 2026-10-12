idf_component_register(
    SRCS    
        "app_main.c" 
        "app_common.c"
        "console/app_console.c"
        "app_littlefs.c"
        "peripheral/app_speaker.c"

    INCLUDE_DIRS 
        "."
        "console"
        "peripheral"
    REQUIRES
        freertos
        console
        esp_timer
        joltwallet__littlefs
        driver
        json
)

