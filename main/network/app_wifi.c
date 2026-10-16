#include "app_wifi.h"
#include "app_config.h"

#if DF_ENABLE_BLUFI
#include "app_blufi.h"
#endif

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <string.h>

static const char *TAG = "app_wifi";

/* ================== TRẠNG THÁI ================== */
static bool         g_bInited     = false;
static bool         g_bGotIp      = false;
static char         g_acIpStr[16] = {0};
static uint8_t      g_au8Mac[6]   = {0};
static esp_netif_t *g_pstStaNetif = NULL;

/* ================== EVENT HANDLER (nội bộ) ================== */

static void app_wifi_EventHandler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            LREP(TAG, "STA_START");
            /* Không tự connect — chờ BluFi / ConnectWithConfig */
            break;

        case WIFI_EVENT_STA_CONNECTED:
            LREP(TAG, "STA_CONNECTED");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc =
                (wifi_event_sta_disconnected_t *)event_data;
            LREP_WARNING(TAG, "STA_DISCONNECTED reason=%d",
                         disc ? disc->reason : -1);
            g_bGotIp = false;
            memset(g_acIpStr, 0, sizeof(g_acIpStr));

#if DF_ENABLE_BLUFI
            /* Báo BluFi nếu đang trong session provisioning */
            if (app_blufi_IsRunning()) {
                app_blufi_OnWifiResult(false);
            }
#endif
            break;
        }

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(g_acIpStr, sizeof(g_acIpStr), IPSTR, IP2STR(&event->ip_info.ip));
        g_bGotIp = true;
        LREP(TAG, "GOT_IP: %s", g_acIpStr);

#if DF_ENABLE_BLUFI
        if (app_blufi_IsRunning()) {
            app_blufi_OnWifiResult(true);
        }
#endif
    }
}

/* ================== API ================== */

int32_t app_wifi_Init(void)
{
    if (g_bInited) {
        return ERROR_CODE_OK;
    }

    esp_err_t err;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LREP_ERROR(TAG, "esp_netif_init: %s", esp_err_to_name(err));
        return ERROR_CODE_FAIL;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LREP_ERROR(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(err));
        return ERROR_CODE_FAIL;
    }

    g_pstStaNetif = esp_netif_create_default_wifi_sta();
    if (g_pstStaNetif == NULL) {
        LREP_ERROR(TAG, "create default wifi sta failed");
        return ERROR_CODE_FAIL;
    }
    esp_netif_set_hostname(g_pstStaNetif, DF_WIFI_STA_HOSTNAME);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        LREP_ERROR(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        return ERROR_CODE_FAIL;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &app_wifi_EventHandler, NULL, NULL);
    if (err != ESP_OK) {
        LREP_ERROR(TAG, "register WIFI_EVENT: %s", esp_err_to_name(err));
        return ERROR_CODE_FAIL;
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &app_wifi_EventHandler, NULL, NULL);
    if (err != ESP_OK) {
        LREP_ERROR(TAG, "register IP_EVENT: %s", esp_err_to_name(err));
        return ERROR_CODE_FAIL;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        LREP_ERROR(TAG, "set_mode STA: %s", esp_err_to_name(err));
        return ERROR_CODE_FAIL;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        LREP_ERROR(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
        return ERROR_CODE_FAIL;
    }

    esp_wifi_get_mac(WIFI_IF_STA, g_au8Mac);
    LREP(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
         g_au8Mac[0], g_au8Mac[1], g_au8Mac[2],
         g_au8Mac[3], g_au8Mac[4], g_au8Mac[5]);

    g_bInited = true;
    LREP(TAG, "Wi-Fi STA init OK");
    return ERROR_CODE_OK;
}

int32_t app_wifi_ConnectWithConfig(const char *pcSsid, const char *pcPassword)
{
    if (!g_bInited) {
        LREP_ERROR(TAG, "Wi-Fi chua init");
        return ERROR_CODE_FAIL;
    }
    if (pcSsid == NULL || pcSsid[0] == '\0') {
        LREP_ERROR(TAG, "SSID rong");
        return ERROR_CODE_INVALID_PARAM;
    }

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, pcSsid, sizeof(wifi_cfg.sta.ssid) - 1);
    if (pcPassword) {
        strncpy((char *)wifi_cfg.sta.password, pcPassword,
                sizeof(wifi_cfg.sta.password) - 1);
    }
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable    = true;
    wifi_cfg.sta.pmf_cfg.required   = false;

    esp_wifi_disconnect();
    app_common_DelayMs(100);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) {
        LREP_ERROR(TAG, "set_config: %s", esp_err_to_name(err));
        return ERROR_CODE_FAIL;
    }

    g_bGotIp = false;
    memset(g_acIpStr, 0, sizeof(g_acIpStr));

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        LREP_ERROR(TAG, "connect: %s", esp_err_to_name(err));
        return ERROR_CODE_FAIL;
    }

    LREP(TAG, "Connecting to SSID=\"%s\" ...", pcSsid);
    return ERROR_CODE_OK;
}

void app_wifi_Disconnect(void)
{
    if (!g_bInited) return;
    esp_wifi_disconnect();
    g_bGotIp = false;
    memset(g_acIpStr, 0, sizeof(g_acIpStr));
    LREP(TAG, "Disconnected");
}

bool app_wifi_IsConnected(void)
{
    return g_bGotIp;
}

const char *app_wifi_GetIp(void)
{
    return g_bGotIp ? g_acIpStr : NULL;
}

void app_wifi_GetMac(uint8_t *pu8Mac)
{
    if (pu8Mac) {
        memcpy(pu8Mac, g_au8Mac, 6);
    }
}
