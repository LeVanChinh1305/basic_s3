#include "app_cmd.h"
#include "app_config.h"
#include "app_wifi.h"

#if DF_ENABLE_BLUFI
#include "app_blufi.h"
#endif

#include "cJSON.h"
#include "esp_timer.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "app_cmd";

#define CMD_GET_DEVICE_ID       "CmdGetDeviceID"
#define CMD_SET_DEVICE_CONFIG   "CmdSetDeviceConfig"
#define CMD_EXIT_CONFIGURATION  "CmdExitConfiguration"
#define CMD_GET_DEVICE_CONFIG   "CmdGetDeviceConfig"
#define CMD_DELETE_DEVICE       "CmdDeleteDevice"

#define NVS_NS_DEVICE_CFG       "dev_cfg"

typedef struct {
    char broker[128];
    char username[64];
    char password[64];
    char mqttsub[96];
    char mqttpub[96];
    char mqttalert[96];
    char forceOtaUrl[192];
    char beSharedKey[128];
    bool bValid;
} app_device_cfg_t;

static app_device_cfg_t g_stCfg = {0};

static int64_t app_cmd_Timestamp(void)
{
    time_t t = time(NULL);
    if (t > 1000000000) {
        return (int64_t)t;
    }
    return (int64_t)(esp_timer_get_time() / 1000000ULL);
}

static void app_cmd_GetMacStr(char *pcOut, size_t u32Len)
{
    uint8_t mac[6] = {0};
    app_wifi_GetMac(mac);
    snprintf(pcOut, u32Len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* BT MAC = STA MAC, byte cuoi + 2 (giong firmware goc) */
static void app_cmd_GetBleMacStr(char *pcOut, size_t u32Len)
{
    uint8_t mac[6] = {0};
    app_wifi_GetMac(mac);
    mac[5] = (uint8_t)(mac[5] + 2);
    snprintf(pcOut, u32Len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void app_cmd_SendResponse(const char *pcJson, app_cmd_src_t eSrc)
{
    if (!pcJson) {
        return;
    }
    LREP(TAG, "TX [%d]: %s", (int)eSrc, pcJson);
#if DF_ENABLE_BLUFI
    if (eSrc == APP_CMD_SRC_BLUFI) {
        app_blufi_SendCustomData((const uint8_t *)pcJson, (int)strlen(pcJson));
        return;
    }
#endif
    (void)eSrc;
}

static void app_cmd_CfgSave(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_DEVICE_CFG, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, "broker", g_stCfg.broker);
    nvs_set_str(h, "user", g_stCfg.username);
    nvs_set_str(h, "pass", g_stCfg.password);
    nvs_set_str(h, "sub", g_stCfg.mqttsub);
    nvs_set_str(h, "pub", g_stCfg.mqttpub);
    nvs_set_str(h, "alert", g_stCfg.mqttalert);
    nvs_set_str(h, "ota", g_stCfg.forceOtaUrl);
    nvs_set_str(h, "key", g_stCfg.beSharedKey);
    nvs_set_u8(h, "valid", 1);
    nvs_commit(h);
    nvs_close(h);
    g_stCfg.bValid = true;
    LREP(TAG, "Saved device config to NVS");
}

static void app_cmd_CfgLoad(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_DEVICE_CFG, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len;
    uint8_t valid = 0;
    nvs_get_u8(h, "valid", &valid);
    if (!valid) {
        nvs_close(h);
        return;
    }
    len = sizeof(g_stCfg.broker);
    nvs_get_str(h, "broker", g_stCfg.broker, &len);
    len = sizeof(g_stCfg.username);
    nvs_get_str(h, "user", g_stCfg.username, &len);
    len = sizeof(g_stCfg.password);
    nvs_get_str(h, "pass", g_stCfg.password, &len);
    len = sizeof(g_stCfg.mqttsub);
    nvs_get_str(h, "sub", g_stCfg.mqttsub, &len);
    len = sizeof(g_stCfg.mqttpub);
    nvs_get_str(h, "pub", g_stCfg.mqttpub, &len);
    len = sizeof(g_stCfg.mqttalert);
    nvs_get_str(h, "alert", g_stCfg.mqttalert, &len);
    len = sizeof(g_stCfg.forceOtaUrl);
    nvs_get_str(h, "ota", g_stCfg.forceOtaUrl, &len);
    len = sizeof(g_stCfg.beSharedKey);
    nvs_get_str(h, "key", g_stCfg.beSharedKey, &len);
    nvs_close(h);
    g_stCfg.bValid = true;
}

static void app_cmd_CfgClear(void)
{
    memset(&g_stCfg, 0, sizeof(g_stCfg));
    nvs_handle_t h;
    if (nvs_open(NVS_NS_DEVICE_CFG, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    LREP(TAG, "Cleared device config");
}

static const char *app_cmd_GetName(cJSON *root, cJSON **ppValue)
{
    cJSON *name = cJSON_GetObjectItem(root, "name");
    if (cJSON_IsString(name)) {
        *ppValue = cJSON_GetObjectItem(root, "value");
        return name->valuestring;
    }
    *ppValue = NULL;
    return NULL;
}

/* ========== I.2 CmdGetDeviceID ========== */
static void app_cmd_HandleGetDeviceId(app_cmd_src_t eSrc)
{
    char mac[16];
    char bleMac[16];
    app_cmd_GetMacStr(mac, sizeof(mac));
    app_cmd_GetBleMacStr(bleMac, sizeof(bleMac));

    cJSON *rsp = cJSON_CreateObject();
    cJSON_AddStringToObject(rsp, "name", CMD_GET_DEVICE_ID);
    cJSON_AddNumberToObject(rsp, "devT", DEVICE_TYPE);
    cJSON_AddStringToObject(rsp, "devExtAddr", mac);
    cJSON_AddStringToObject(rsp, "devBleMac", bleMac);
    cJSON_AddNumberToObject(rsp, "timeStamp", (double)app_cmd_Timestamp());

    char *str = cJSON_PrintUnformatted(rsp);
    cJSON_Delete(rsp);
    if (str) {
        app_cmd_SendResponse(str, eSrc);
        free(str);
    }
}

void app_cmd_SendGetDeviceIdResponse(app_cmd_src_t eSrc)
{
    app_cmd_HandleGetDeviceId(eSrc);
}

/* ========== I.5 CmdSetDeviceConfig ========== */
static void app_cmd_HandleSetDeviceConfig(cJSON *pValue, app_cmd_src_t eSrc)
{
    if (!cJSON_IsObject(pValue)) {
        LREP_ERROR(TAG, "SetDeviceConfig: value invalid");
        return;
    }

    app_cmd_CfgClear();

    cJSON *j;
    j = cJSON_GetObjectItem(pValue, "broker");
    if (cJSON_IsString(j)) {
        strncpy(g_stCfg.broker, j->valuestring, sizeof(g_stCfg.broker) - 1);
    }
    j = cJSON_GetObjectItem(pValue, "username");
    if (cJSON_IsString(j)) {
        strncpy(g_stCfg.username, j->valuestring, sizeof(g_stCfg.username) - 1);
    }
    j = cJSON_GetObjectItem(pValue, "password");
    if (cJSON_IsString(j)) {
        strncpy(g_stCfg.password, j->valuestring, sizeof(g_stCfg.password) - 1);
    }
    j = cJSON_GetObjectItem(pValue, "mqttsub");
    if (cJSON_IsString(j)) {
        strncpy(g_stCfg.mqttsub, j->valuestring, sizeof(g_stCfg.mqttsub) - 1);
    }
    j = cJSON_GetObjectItem(pValue, "mqttpub");
    if (cJSON_IsString(j)) {
        strncpy(g_stCfg.mqttpub, j->valuestring, sizeof(g_stCfg.mqttpub) - 1);
    }
    j = cJSON_GetObjectItem(pValue, "mqttalert");
    if (cJSON_IsString(j)) {
        strncpy(g_stCfg.mqttalert, j->valuestring, sizeof(g_stCfg.mqttalert) - 1);
    }
    j = cJSON_GetObjectItem(pValue, "forceOtaUrl");
    if (cJSON_IsString(j)) {
        strncpy(g_stCfg.forceOtaUrl, j->valuestring, sizeof(g_stCfg.forceOtaUrl) - 1);
    }
    j = cJSON_GetObjectItem(pValue, "beSharedKey");
    if (cJSON_IsString(j)) {
        strncpy(g_stCfg.beSharedKey, j->valuestring, sizeof(g_stCfg.beSharedKey) - 1);
    }

    app_cmd_CfgSave();
    LREP(TAG, "SetDeviceConfig broker=%s", g_stCfg.broker);

    /* Response giong firmware goc */
    char mac[16];
    app_cmd_GetMacStr(mac, sizeof(mac));
    cJSON *rsp = cJSON_CreateObject();
    cJSON_AddStringToObject(rsp, "name", CMD_SET_DEVICE_CONFIG);
    cJSON_AddNumberToObject(rsp, "devT", DEVICE_TYPE);
    cJSON_AddStringToObject(rsp, "devExtAddr", mac);
    cJSON_AddNumberToObject(rsp, "errorCode", ERROR_CODE_OK); /* 50000 */
    cJSON_AddStringToObject(rsp, "software_version", DF_FIRMWARE_VERSION);
    cJSON_AddNumberToObject(rsp, "timeStamp", (double)app_cmd_Timestamp());

    char *str = cJSON_PrintUnformatted(rsp);
    cJSON_Delete(rsp);
    if (str) {
        app_cmd_SendResponse(str, eSrc);
        free(str);
    }

#if DF_ENABLE_BLUFI
    if (eSrc == APP_CMD_SRC_BLUFI) {
        app_common_DelayMs(200);
        app_blufi_Stop();
    }
#endif
}

/* ========== I.6 CmdExitConfiguration ========== */
static void app_cmd_HandleExitConfiguration(cJSON *pValue, app_cmd_src_t eSrc)
{
    char mac[16];
    app_cmd_GetMacStr(mac, sizeof(mac));
    int err = ERROR_CODE_OK;

    if (cJSON_IsObject(pValue)) {
        cJSON *jDevT = cJSON_GetObjectItem(pValue, "devT");
        cJSON *jAddr = cJSON_GetObjectItem(pValue, "devExtAddr");
        if (cJSON_IsNumber(jDevT) && (int)jDevT->valuedouble != DEVICE_TYPE) {
            err = ERROR_CODE_UNDEFINED;
        }
        if (cJSON_IsString(jAddr) && strcasecmp(jAddr->valuestring, mac) != 0) {
            err = ERROR_CODE_UNDEFINED;
        }
    }

    cJSON *rsp = cJSON_CreateObject();
    cJSON_AddStringToObject(rsp, "name", CMD_EXIT_CONFIGURATION);
    cJSON_AddNumberToObject(rsp, "devT", DEVICE_TYPE);
    cJSON_AddStringToObject(rsp, "devExtAddr", mac);
    cJSON_AddNumberToObject(rsp, "errorCode", err);
    cJSON_AddNumberToObject(rsp, "timeStamp", (double)app_cmd_Timestamp());

    char *str = cJSON_PrintUnformatted(rsp);
    cJSON_Delete(rsp);
    if (str) {
        app_cmd_SendResponse(str, eSrc);
        free(str);
    }

#if DF_ENABLE_BLUFI
    if (eSrc == APP_CMD_SRC_BLUFI) {
        app_blufi_Stop();
    }
#endif
}

/* ========== I.7 CmdGetDeviceConfig ========== */
static void app_cmd_HandleGetDeviceConfig(app_cmd_src_t eSrc)
{
    if (!g_stCfg.bValid) {
        app_cmd_CfgLoad();
    }

    char mac[16];
    app_cmd_GetMacStr(mac, sizeof(mac));
    cJSON *rsp = cJSON_CreateObject();
    cJSON_AddStringToObject(rsp, "name", CMD_GET_DEVICE_CONFIG);
    cJSON_AddNumberToObject(rsp, "devT", DEVICE_TYPE);
    cJSON_AddStringToObject(rsp, "devExtAddr", mac);
    cJSON_AddStringToObject(rsp, "broker", g_stCfg.broker);
    cJSON_AddStringToObject(rsp, "username", g_stCfg.username);
    cJSON_AddStringToObject(rsp, "password", g_stCfg.password);
    cJSON_AddStringToObject(rsp, "mqttsub", g_stCfg.mqttsub);
    cJSON_AddStringToObject(rsp, "mqttpub", g_stCfg.mqttpub);
    cJSON_AddStringToObject(rsp, "mqttalert", g_stCfg.mqttalert);
    cJSON_AddStringToObject(rsp, "software_version", DF_FIRMWARE_VERSION);
    cJSON_AddStringToObject(rsp, "forceOtaUrl", g_stCfg.forceOtaUrl);
    cJSON_AddNumberToObject(rsp, "timeStamp", (double)app_cmd_Timestamp());

    char *str = cJSON_PrintUnformatted(rsp);
    cJSON_Delete(rsp);
    if (str) {
        app_cmd_SendResponse(str, eSrc);
        free(str);
    }
}

/* ========== I.4 CmdDeleteDevice ========== */
static void app_cmd_HandleDeleteDevice(app_cmd_src_t eSrc)
{
    app_cmd_CfgClear();

    char mac[16];
    app_cmd_GetMacStr(mac, sizeof(mac));
    cJSON *rsp = cJSON_CreateObject();
    cJSON_AddStringToObject(rsp, "name", CMD_DELETE_DEVICE);
    cJSON_AddNumberToObject(rsp, "devT", DEVICE_TYPE);
    cJSON_AddStringToObject(rsp, "devExtAddr", mac);

    char *str = cJSON_PrintUnformatted(rsp);
    cJSON_Delete(rsp);
    if (str) {
        app_cmd_SendResponse(str, eSrc);
        free(str);
    }
}

int32_t app_cmd_Handle(const char *pcMsg, app_cmd_src_t eSrc)
{
    if (!pcMsg || !pcMsg[0]) {
        return ERROR_CODE_INVALID_PARAM;
    }
    LREP(TAG, "RX [%d]: %s", (int)eSrc, pcMsg);

    cJSON *root = cJSON_Parse(pcMsg);
    if (!root) {
        LREP_ERROR(TAG, "JSON parse fail");
        return ERROR_CODE_JSON_FORMAT;
    }

    cJSON *pValue = NULL;
    const char *name = app_cmd_GetName(root, &pValue);
    if (!name) {
        LREP_ERROR(TAG, "Missing name");
        cJSON_Delete(root);
        return ERROR_CODE_JSON_FORMAT;
    }

    if (strcmp(name, CMD_GET_DEVICE_ID) == 0) {
        app_cmd_HandleGetDeviceId(eSrc);
    } else if (strcmp(name, CMD_SET_DEVICE_CONFIG) == 0) {
        app_cmd_HandleSetDeviceConfig(pValue ? pValue : root, eSrc);
    } else if (strcmp(name, CMD_EXIT_CONFIGURATION) == 0) {
        app_cmd_HandleExitConfiguration(pValue, eSrc);
    } else if (strcmp(name, CMD_GET_DEVICE_CONFIG) == 0) {
        app_cmd_HandleGetDeviceConfig(eSrc);
    } else if (strcmp(name, CMD_DELETE_DEVICE) == 0) {
        app_cmd_HandleDeleteDevice(eSrc);
    } else {
        LREP_WARNING(TAG, "CMD not supported yet: %s", name);
    }

    cJSON_Delete(root);
    return ERROR_CODE_OK;
}