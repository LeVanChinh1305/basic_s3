#ifndef APP_BLUFI_H_
#define APP_BLUFI_H_
#include "app_common.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    APP_BLUFI_STATE_IDLE = 0,
    APP_BLUFI_STATE_STARTING,
    APP_BLUFI_STATE_ADVERTISING,
    APP_BLUFI_STATE_CONNECTED,
    APP_BLUFI_STATE_CONFIGURING,
    APP_BLUFI_STATE_SUCCESS,
    APP_BLUFI_STATE_FAIL,
} app_blufi_state_t;
int32_t app_blufi_Init(void);
int32_t app_blufi_Start(void);
void    app_blufi_Stop(void);
bool    app_blufi_IsRunning(void);
app_blufi_state_t app_blufi_GetState(void);
void    app_blufi_OnWifiResult(bool bGotIp);
int32_t app_blufi_SendCustomData(const uint8_t *data, int len);
#ifdef __cplusplus
}
#endif
#endif