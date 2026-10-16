#ifndef APP_CMD_H_
#define APP_CMD_H_

#include "app_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_CMD_SRC_BLUFI = 0,
    APP_CMD_SRC_UDP,
    APP_CMD_SRC_MQTT,
} app_cmd_src_t;

int32_t app_cmd_Handle(const char *pcMsg, app_cmd_src_t eSrc);
void    app_cmd_SendGetDeviceIdResponse(app_cmd_src_t eSrc);

#ifdef __cplusplus
}
#endif

#endif