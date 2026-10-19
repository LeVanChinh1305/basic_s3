#ifndef APP_BLUFI_H_
#define APP_BLUFI_H_
#include "app_common.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    APP_BLUFI_STATE_IDLE = 0,       // Chưa chạy provisioning                           : Sau Init, hoặc sau khi session kết thúc (trước restart)
    APP_BLUFI_STATE_STARTING,       // Đang bật BT / Bluedroid / profile                : Ngay khi app_blufi_Start()
    APP_BLUFI_STATE_ADVERTISING,    // Đang phát BLE ADV VCONNEX-XXXX                   : Profile init xong, chờ App kết nối
    APP_BLUFI_STATE_CONNECTED,      // App đã kết nối BLE GATT                          : Event BLE connect
    APP_BLUFI_STATE_CONFIGURING,    // Đang nhận SSID/pass và/hoặc đang connect Wi‑Fi   : REQ_CONNECT_TO_AP, đang chờ Got IP
    APP_BLUFI_STATE_SUCCESS,        // Cấu hình xong (Wi‑Fi OK +/hoặc SetDeviceConfig)  : Got IP + report SUCCESS, hoặc session end thành công
    APP_BLUFI_STATE_FAIL,           // Thất bại / timeout                               : Timeout 90s, init BT lỗi, session end không thành công
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