#ifndef APP_WIFI_H_
#define APP_WIFI_H_
#include "app_common.h"
#include "esp_wifi.h"
#ifdef __cplusplus
extern "C" {
#endif
int32_t app_wifi_Init(void);
int32_t app_wifi_ConnectWithConfig(const char *pcSsid, const char *pcPassword);
void    app_wifi_Disconnect(void);
bool    app_wifi_IsConnected(void);
const char *app_wifi_GetIp(void);
void    app_wifi_GetMac(uint8_t *pu8Mac);
#ifdef __cplusplus
}
#endif
#endif