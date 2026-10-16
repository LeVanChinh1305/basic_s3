#include "app_blemesh.h"
#include "app_littlefs.h"
#include "cJSON.h"

#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "app_mesh_vendor.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "app_mesh";

#define MESH_INFO_MUTEX_NAME    "MESH_INFO"

#ifndef ROLE_NODE
#define ROLE_NODE 0
#endif

/* ================== KIỂU DỮ LIỆU ================== */
typedef enum {
    MESH_EVT_ENABLED = 0,   /* Prov enable xong -> doi chieu trang thai da luu */
    MESH_EVT_PROV_COMPLETE, /* App cap mang xong */
    MESH_EVT_PROV_RESET,    /* Node bi reset (tu App hoac local) */
    MESH_EVT_APPKEY_ADD,    /* App them AppKey */
    MESH_EVT_APP_BIND,      /* Bind AppKey vao OnOff Server */
    MESH_EVT_SUB_ADD,       /* Subscribe Group */
    MESH_EVT_SUB_DEL,       /* Un-subscribe Group */
    MESH_EVT_ONOFF_SET      /* Nhan lenh Generic OnOff Set */
} en_mesh_evt_t;

/* Sự kiện đẩy từ callback của BLE stack sang task ứng dụng (qua queue) */
typedef struct {
    en_mesh_evt_t eType;
    int64_t       s64TimeUs;   /* esp_timer lúc vào callback */
    uint16_t      u16Src;      /* Địa chỉ gửi (ONOFF) / NetIdx (PROV_COMPLETE, APPKEY_ADD) */
    uint16_t      u16Dst;      /* Địa chỉ đích (unicast hoac group) */
    uint16_t      u16Param;    /* Unicast (PROV) / AppIdx / Group */
    uint8_t       u8OnOff;
    uint8_t       u8Ttl;
    int8_t        s8Rssi;
} st_mesh_evt_t;

/* Trạng thái Node - được lưu vào LittleFS (DF_MESH_STATE_FILE) */
typedef struct {
    bool     bProvisioned;
    uint16_t u16Unicast;
    uint16_t u16NetIdx;
    uint16_t u16AppIdx;
    bool     bAppBound;
    uint16_t u16Group;       /* 0 = chua subscribe */
    uint8_t  u8OnOff;
    uint32_t u32BootCount;
    uint32_t u32RxCount;
    uint16_t u16LastSrc;
    uint16_t u16LastDst;
} st_mesh_info_t;

/* Một dòng log vòng tròn */
typedef struct {
    int64_t  s64TimeUs;
    uint16_t u16Src;
    uint16_t u16Dst;
    uint8_t  u8OnOff;
    uint8_t  u8Ttl;
    int8_t   s8Rssi;
} st_mesh_log_t;

/* ================== TÀI NGUYÊN HỆ THỐNG ==================
 * Một mutex g_hMeshMutex bảo vệ CA g_pstInfo VA ring buffer g_pstLog (cùng một miền dự liệu).
 */
static st_mesh_info_t   *g_pstInfo      = NULL;     /* PSRAM */
static st_mesh_log_t    *g_pstLog       = NULL;     /* PSRAM */
static uint32_t          g_u32LogHead   = 0;        // Con trỏ ghi (write index), chỉ tới vị trí sẽ ghi bản ghi log tiếp theo trong mảng g_pstLog
static uint32_t          g_u32LogCount  = 0;        // Đếm tổng số lượng log hiện đang có trong bộ đệm

static SemaphoreHandle_t g_hMeshMutex   = NULL;     // Mutex bảo vệ vùng dữ liệu dùng chung
static QueueHandle_t     g_hMeshEvtQ    = NULL;     // Queue dùng để truyền tin nhắn/sự kiện (st_mesh_evt_t) bất đồng bộ từ các callback của BLE stack sang task xử lý app_mesh_Task
static TaskHandle_t      g_hMeshTask    = NULL;     // Handle của Task FreeRTOS phụ trách xử lý sự kiện Mesh và đồng bộ ghi Flash

static volatile bool     g_bDirty       = false;    // Cờ đánh dấu dữ liệu trong RAM đã bị thay đổi và cần được đồng bộ (lưu) xuống bộ nhớ Flash (LittleFS).
static volatile int64_t  g_s64DirtyUs   = 0;        // Lưu mốc thời gian (esp_timer_get_time()) tại thời điểm cờ g_bDirty được bật
static volatile uint32_t g_u32DropEvt   = 0;        // Bộ đếm số lượng sự kiện bị tràn/bỏ rơi (drop) khi Queue g_hMeshEvtQ bị đầy.
static bool              g_bMeshInitialized = false;// cờ dùng xác định module đã được khởi tạo trước đó hay chưa

/* ================== MODEL BLE MESH ================== */

static uint8_t g_au8DevUuid[16] = { 0x32, 0x10 };   /* 6 byte tiep theo = MAC BT */

static esp_ble_mesh_prov_t g_stProv = {
    .uuid = g_au8DevUuid,
};

static esp_ble_mesh_cfg_srv_t g_stCfgSrv = {
#if DF_MESH_RELAY
    .relay            = ESP_BLE_MESH_RELAY_ENABLED,
#else
    .relay            = ESP_BLE_MESH_RELAY_DISABLED,
#endif
    .beacon           = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy       = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy       = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .friend_state     = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .default_ttl      = 7,
    .net_transmit     = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(g_stOnoffPub, 2 + 3, ROLE_NODE);

// đại diện cho thực thể Generic OnOff Server Model trên Node
static esp_ble_mesh_gen_onoff_srv_t g_stOnoffSrv = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
};

static esp_ble_mesh_client_t g_stOnoffCli;

static esp_ble_mesh_model_t g_astRootModels[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&g_stCfgSrv),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&g_stOnoffPub, &g_stOnoffSrv),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&g_stOnoffPub, &g_stOnoffCli),
};

/* ================== VENDOR SERVER MODEL (TIẾP NHẬN CUSTOM DATA) ================== */
ESP_BLE_MESH_MODEL_PUB_DEFINE(g_stVndSrvPub, 3 + sizeof(st_vendor_custom_data_t), ROLE_NODE);

static esp_ble_mesh_model_op_t g_astVndServerOp[] = {
    ESP_BLE_MESH_MODEL_OP(DF_MESH_VND_OP_SET, sizeof(st_vendor_custom_data_t)),
    ESP_BLE_MESH_MODEL_OP(DF_MESH_VND_OP_GET, 0),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t g_astVndModelsServer[] = {
    ESP_BLE_MESH_VENDOR_MODEL(
        DF_MESH_CID_ESP,
        DF_MESH_VND_MODEL_ID_SERVER,
        g_astVndServerOp,
        &g_stVndSrvPub,
        NULL
    ),
};

static esp_ble_mesh_elem_t g_astElements[] = {
    ESP_BLE_MESH_ELEMENT(0, g_astRootModels, g_astVndModelsServer),
};

static esp_ble_mesh_comp_t g_stComp = {
    .cid           = DF_MESH_COMPANY_ID,
    .element_count = ARRAY_SIZE(g_astElements),
    .elements      = g_astElements,
};

/* ================== TIỆN ÍCH ================== */

// Bật cờ g_bDirty = true và ghi lại mốc thời gian
static void app_mesh_MarkDirty(void)
{
    g_s64DirtyUs = esp_timer_get_time();
    g_bDirty     = true;
}

/* Quy tắc mẫu A: gọi từ callback của BLE stack -> KHÔNG block, KHÔNG lấy mutex, KHÔNG ghi flash */
static void app_mesh_PostEvt(const st_mesh_evt_t *pstEvt)
{
    if (g_hMeshEvtQ == NULL) return;
    if (xQueueSend(g_hMeshEvtQ, pstEvt, 0) != pdTRUE) {
        g_u32DropEvt++;
    }
}

/* ================== LƯU / NẠP TRẠNG THÁI (LITTLEFS) ================== */

static void app_mesh_LoadState(void)
{
    if (!app_littlefs_FileExists(DF_MESH_STATE_FILE)) {
        LREP(TAG, "Chua co file trang thai, dung gia tri mac dinh");
        return;
    }

    char *pcJson = NULL;
    if (app_littlefs_ReadTextFile(DF_MESH_STATE_FILE, &pcJson) != ERROR_CODE_OK) {
        return;
    }

    cJSON *pRoot = cJSON_Parse(pcJson);
    FREE_SAFE(pcJson);
    if (pRoot == NULL) {
        LREP_WARNING(TAG, "File %s sai dinh dang JSON, bo qua", DF_MESH_STATE_FILE);
        return;
    }

    /* Parse xong ngoài khóa, chỉ giữ mutex lúc gán vào struct dùng chung */
    st_mesh_info_t stLoaded = {0};
    cJSON *pItem = NULL;
    if ((pItem = cJSON_GetObjectItem(pRoot, "onoff"))   && cJSON_IsNumber(pItem)) stLoaded.u8OnOff      = pItem->valueint ? 1 : 0;
    if ((pItem = cJSON_GetObjectItem(pRoot, "boot"))    && cJSON_IsNumber(pItem)) stLoaded.u32BootCount = (uint32_t)pItem->valuedouble;
    if ((pItem = cJSON_GetObjectItem(pRoot, "rx"))      && cJSON_IsNumber(pItem)) stLoaded.u32RxCount   = (uint32_t)pItem->valuedouble;
    if ((pItem = cJSON_GetObjectItem(pRoot, "unicast")) && cJSON_IsNumber(pItem)) stLoaded.u16Unicast   = (uint16_t)pItem->valueint;
    if ((pItem = cJSON_GetObjectItem(pRoot, "net_idx")) && cJSON_IsNumber(pItem)) stLoaded.u16NetIdx    = (uint16_t)pItem->valueint;
    if ((pItem = cJSON_GetObjectItem(pRoot, "app_idx")) && cJSON_IsNumber(pItem)) stLoaded.u16AppIdx    = (uint16_t)pItem->valueint;
    if ((pItem = cJSON_GetObjectItem(pRoot, "bound"))   && cJSON_IsNumber(pItem)) stLoaded.bAppBound    = (pItem->valueint != 0);
    if ((pItem = cJSON_GetObjectItem(pRoot, "group"))   && cJSON_IsNumber(pItem)) stLoaded.u16Group     = (uint16_t)pItem->valueint;
    cJSON_Delete(pRoot);

    {
        APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
        if (!stGuard.bLocked) return;
        *g_pstInfo = stLoaded;
    }

    LREP(TAG, "Nap trang thai: onoff=%u boot=%u rx=%u unicast=0x%04X group=0x%04X",
        stLoaded.u8OnOff, 
        (unsigned)stLoaded.u32BootCount, 
        (unsigned)stLoaded.u32RxCount,
        stLoaded.u16Unicast, stLoaded.u16Group);
}

/* Mau snapshot: sao chep struct trong khoa ngan, dung JSON + ghi flash ngoai khoa.
 * Ghi file tam roi rename -> không hỏng file nếu mất điện giữa chừng. */
static void app_mesh_SaveState(void)
{
    st_mesh_info_t stSnap;
    {
        APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
        if (!stGuard.bLocked) return;
        stSnap = *g_pstInfo;
    }

    cJSON *pRoot = cJSON_CreateObject();
    if (pRoot == NULL) return;

    cJSON_AddStringToObject(pRoot, "fw", DF_FIRMWARE_VERSION);
    cJSON_AddNumberToObject(pRoot, "onoff",    stSnap.u8OnOff);
    cJSON_AddNumberToObject(pRoot, "boot",     stSnap.u32BootCount);
    cJSON_AddNumberToObject(pRoot, "rx",       stSnap.u32RxCount);
    cJSON_AddNumberToObject(pRoot, "unicast",  stSnap.u16Unicast);
    cJSON_AddNumberToObject(pRoot, "net_idx",  stSnap.u16NetIdx);
    cJSON_AddNumberToObject(pRoot, "app_idx",  stSnap.u16AppIdx);
    cJSON_AddNumberToObject(pRoot, "bound",    stSnap.bAppBound ? 1 : 0);
    cJSON_AddNumberToObject(pRoot, "group",    stSnap.u16Group);
    cJSON_AddNumberToObject(pRoot, "last_src", stSnap.u16LastSrc);
    cJSON_AddNumberToObject(pRoot, "last_dst", stSnap.u16LastDst);

    char *pcJson = cJSON_PrintUnformatted(pRoot);
    cJSON_Delete(pRoot);
    if (pcJson == NULL) return;

    const char *pcTmp = DF_MESH_STATE_FILE ".tmp";
    if (app_littlefs_WriteTextFile(pcTmp, pcJson) == ERROR_CODE_OK) {
        if (rename(pcTmp, DF_MESH_STATE_FILE) != 0) {
            LREP_ERROR(TAG, "Rename %s that bai", pcTmp);
        }
    }
    cJSON_free(pcJson);
}

/* ================== CALLBACK BLE STACK (CHẠY TRONG TASK CỦA BLE) ================== */

static void app_mesh_ProvCb(esp_ble_mesh_prov_cb_event_t eEvent, esp_ble_mesh_prov_cb_param_t *pstParam)
{
    st_mesh_evt_t stEvt = { .s64TimeUs = esp_timer_get_time() };

    switch (eEvent) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        LREP(TAG, "PROV_REGISTER_COMP, err=%d", pstParam->prov_register_comp.err_code);
        break;

    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        LREP(TAG, "PROV_ENABLE_COMP, err=%d", pstParam->node_prov_enable_comp.err_code);
        stEvt.eType = MESH_EVT_ENABLED;
        app_mesh_PostEvt(&stEvt);
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        LREP(TAG, "Provisioning link OPEN (bearer=%s)",
             pstParam->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        LREP(TAG, "Provisioning link CLOSE (bearer=%s)",
             pstParam->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;

    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        stEvt.eType    = MESH_EVT_PROV_COMPLETE;
        stEvt.u16Src   = pstParam->node_prov_complete.net_idx;
        stEvt.u16Param = pstParam->node_prov_complete.addr;
        app_mesh_PostEvt(&stEvt);
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        stEvt.eType = MESH_EVT_PROV_RESET;
        app_mesh_PostEvt(&stEvt);
        break;

    default:
        break;
    }
}

static void app_mesh_CfgSrvCb(esp_ble_mesh_cfg_server_cb_event_t eEvent, esp_ble_mesh_cfg_server_cb_param_t *pstParam)
{
    if (eEvent != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) return;

    st_mesh_evt_t stEvt = { .s64TimeUs = esp_timer_get_time() };

    switch (pstParam->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        stEvt.eType    = MESH_EVT_APPKEY_ADD;
        stEvt.u16Src   = pstParam->value.state_change.appkey_add.net_idx;
        stEvt.u16Param = pstParam->value.state_change.appkey_add.app_idx;
        app_mesh_PostEvt(&stEvt);
        break;

    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        if (pstParam->value.state_change.mod_app_bind.company_id == 0xFFFF &&
            pstParam->value.state_change.mod_app_bind.model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
            stEvt.eType    = MESH_EVT_APP_BIND;
            stEvt.u16Param = pstParam->value.state_change.mod_app_bind.app_idx;
            app_mesh_PostEvt(&stEvt);
        } else if (pstParam->value.state_change.mod_app_bind.company_id == DF_MESH_CID_ESP &&
                   pstParam->value.state_change.mod_app_bind.model_id == DF_MESH_VND_MODEL_ID_SERVER) {
            LREP(TAG, "===> [NODE] DA BIND APPKEY 0x%04X VAO VENDOR SERVER MODEL (CID=0x%04X, ID=0x%04X) <===",
                 pstParam->value.state_change.mod_app_bind.app_idx,
                 DF_MESH_CID_ESP, DF_MESH_VND_MODEL_ID_SERVER);
        }
        break;

    case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
        if (pstParam->value.state_change.mod_sub_add.company_id == 0xFFFF &&
            pstParam->value.state_change.mod_sub_add.model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
            stEvt.eType    = MESH_EVT_SUB_ADD;
            stEvt.u16Param = pstParam->value.state_change.mod_sub_add.sub_addr;
            app_mesh_PostEvt(&stEvt);
        }
        break;

    case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_DELETE:
        if (pstParam->value.state_change.mod_sub_delete.company_id == 0xFFFF &&
            pstParam->value.state_change.mod_sub_delete.model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
            stEvt.eType    = MESH_EVT_SUB_DEL;
            stEvt.u16Param = pstParam->value.state_change.mod_sub_delete.sub_addr;
            app_mesh_PostEvt(&stEvt);
        }
        break;

    default:
        break;
    }
}

static void app_mesh_GenericSrvCb(esp_ble_mesh_generic_server_cb_event_t eEvent, esp_ble_mesh_generic_server_cb_param_t *pstParam)
{
    if (eEvent != ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT) return;

    uint32_t u32Op = pstParam->ctx.recv_op;
    if (u32Op != ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET &&
        u32Op != ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {
        return;
    }

    st_mesh_evt_t stEvt = {
        .eType     = MESH_EVT_ONOFF_SET,
        .s64TimeUs = esp_timer_get_time(),
        .u16Src    = pstParam->ctx.addr,
        .u16Dst    = pstParam->ctx.recv_dst,
        .u8OnOff   = pstParam->value.state_change.onoff_set.onoff,
        .u8Ttl     = pstParam->ctx.recv_ttl,
        .s8Rssi    = pstParam->ctx.recv_rssi,
    };

    /* Callback chi dong goi va day vao queue; log/luu file do task xu ly */
    app_mesh_PostEvt(&stEvt);
}

/* ================== CALLBACK VENDOR SERVER MODEL ================== */

static void app_mesh_VendorServerCb(esp_ble_mesh_model_cb_event_t eEvent,
                                    esp_ble_mesh_model_cb_param_t *pstParam)
{
    if (!pstParam) return;

    switch (eEvent) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT: {
        uint32_t u32Opcode = pstParam->model_operation.opcode;
        uint16_t u16Src    = pstParam->model_operation.ctx->addr;

        if (u32Opcode == DF_MESH_VND_OP_SET || u32Opcode == DF_MESH_VND_OP_GET) {
            st_vendor_custom_data_t stRsp = {0};

            if (u32Opcode == DF_MESH_VND_OP_SET) {
                if (pstParam->model_operation.length < sizeof(st_vendor_custom_data_t)) {
                    LREP_WARNING(TAG, "[VND SRV] Payload SET qua ngan: %u bytes", pstParam->model_operation.length);
                    break;
                }
                st_vendor_custom_data_t stRx = {0};
                memcpy(&stRx, pstParam->model_operation.msg, sizeof(stRx));
                stRx.acDeviceTag[sizeof(stRx.acDeviceTag) - 1] = '\0';

                LREP(TAG, "\n========================================================");
                LREP(TAG, " ===> [VND SRV] NHAN LENH SET TU PROVISIONER 0x%04X", u16Src);
                LREP(TAG, "   Cmd Code    : %u", stRx.u8Cmd);
                LREP(TAG, "   Packet ID   : %u", stRx.u16PacketId);
                LREP(TAG, "   Nhiet do    : %.1f oC (raw=%d)", (float)stRx.s16Temperature / 10.0f, stRx.s16Temperature);
                LREP(TAG, "   Do am       : %.1f %% (raw=%u)", (float)stRx.u16Humidity / 10.0f, stRx.u16Humidity);
                LREP(TAG, "   Relay State : %s (%u)", stRx.u8RelayState ? "ON" : "OFF", stRx.u8RelayState);
                LREP(TAG, "   Device Tag  : '%s'", stRx.acDeviceTag);
                LREP(TAG, "   RSSI / TTL  : %d dBm / %u", pstParam->model_operation.ctx->recv_rssi, pstParam->model_operation.ctx->recv_ttl);
                LREP(TAG, "========================================================\n");

                /* Cap nhat trang thai OnOff noi bo va kich hoat ghi Flash */
                {
                    APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
                    if (stGuard.bLocked) {
                        g_pstInfo->u8OnOff    = stRx.u8RelayState ? 1 : 0;
                        g_pstInfo->u16LastSrc = u16Src;
                        g_pstInfo->u32RxCount++;
                    }
                }
                g_stOnoffSrv.state.onoff = stRx.u8RelayState ? 1 : 0;
                app_mesh_MarkDirty();

                /* Chuan bi ban tin phan hoi STATUS (kem thong so cam bien phia Node) */
                stRsp.u8Cmd          = 3; /* ACK / STATUS */
                stRsp.u16PacketId    = stRx.u16PacketId;
                stRsp.s16Temperature = 285; /* Gia lap doc cam bien: 28.5 oC */
                stRsp.u16Humidity    = 650; /* Gia lap doc cam bien: 65.0 % */
                stRsp.u8RelayState   = stRx.u8RelayState ? 1 : 0;
                snprintf(stRsp.acDeviceTag, sizeof(stRsp.acDeviceTag), "ACK_NODE_%04X",
                         esp_ble_mesh_get_primary_element_address());
            } else {
                /* Lenh GET */
                uint8_t u8CurState = 0;
                {
                    APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
                    if (stGuard.bLocked) u8CurState = g_pstInfo->u8OnOff;
                }
                stRsp.u8Cmd          = 3;
                stRsp.u16PacketId    = 0;
                stRsp.s16Temperature = 285;
                stRsp.u16Humidity    = 650;
                stRsp.u8RelayState   = u8CurState;
                snprintf(stRsp.acDeviceTag, sizeof(stRsp.acDeviceTag), "GET_RSP_%04X",
                         esp_ble_mesh_get_primary_element_address());
            }

            /* Gui goi STATUS ve lai Provisioner */
            esp_err_t err = esp_ble_mesh_server_model_send_msg(
                pstParam->model_operation.model,
                pstParam->model_operation.ctx,
                DF_MESH_VND_OP_STATUS,
                sizeof(stRsp),
                (uint8_t *)&stRsp
            );
            if (err == ESP_OK) {
                LREP(TAG, "[VND SRV] Da gui phan hoi STATUS thanh cong toi 0x%04X", u16Src);
            } else {
                LREP_ERROR(TAG, "[VND SRV] Gui STATUS that bai: %s", esp_err_to_name(err));
            }
        }
        break;
    }

    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (pstParam->model_send_comp.err_code != 0) {
            LREP_ERROR(TAG, "[VND SRV] Gui tin that bai, ma loi: %d", pstParam->model_send_comp.err_code);
        }
        break;

    default:
        break;
    }
}

/* ================== TASK XU LY SU KIEN ================== */

static void app_mesh_HandleEvt(const st_mesh_evt_t *pstEvt)
{
    switch (pstEvt->eType) {

    case MESH_EVT_ENABLED: {
        /* Goi API cua mesh stack NGOAI khoa */
        bool     bProv    = esp_ble_mesh_node_is_provisioned();
        uint16_t u16Addr  = bProv ? esp_ble_mesh_get_primary_element_address() : 0;
        {
            APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
            if (!stGuard.bLocked) break;
            g_pstInfo->bProvisioned = bProv;
            g_pstInfo->u16Unicast   = u16Addr;
            if (!bProv) {
                /* NVS da bi xoa -> bo thong tin cu trong file */
                g_pstInfo->bAppBound = false;
                g_pstInfo->u16Group  = 0;
            }
        }
        if (bProv) {
            LREP(TAG, "Node DA duoc cap mang (khoi phuc tu NVS), Unicast=0x%04X", u16Addr);
        } else {
            LREP(TAG, "Node CHUA duoc cap mang -> dang phat Unprovisioned Beacon, mo nRF Mesh de Scan");
        }
        app_mesh_MarkDirty();
        break;
    }

    case MESH_EVT_PROV_COMPLETE: {
        {
            APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
            if (!stGuard.bLocked) break;
            g_pstInfo->bProvisioned = true;
            g_pstInfo->u16NetIdx    = pstEvt->u16Src;
            g_pstInfo->u16Unicast   = pstEvt->u16Param;
        }
        LREP(TAG, "PROVISIONING THANH CONG: NetIdx=0x%04X Unicast=0x%04X", pstEvt->u16Src, pstEvt->u16Param);
        app_mesh_MarkDirty();
        break;
    }

    case MESH_EVT_PROV_RESET: {
        LREP_WARNING(TAG, "Node bi reset provisioning -> xoa trang thai va khoi dong lai");
        {
            APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
            if (stGuard.bLocked) {
                g_pstInfo->bProvisioned = false;
                g_pstInfo->u16Unicast = 0;
                g_pstInfo->u16NetIdx  = 0;
                g_pstInfo->u16AppIdx  = 0;
                g_pstInfo->bAppBound  = false;
                g_pstInfo->u16Group   = 0;
                g_pstInfo->u8OnOff    = 0;
            }
        }
        app_mesh_SaveState();
        app_common_DelayMs(300);
        esp_restart();
        break;
    }

    case MESH_EVT_APPKEY_ADD:
        LREP(TAG, "AppKey ADD: NetIdx=0x%04X AppIdx=0x%04X", pstEvt->u16Src, pstEvt->u16Param);
        break;

    case MESH_EVT_APP_BIND: {
        {
            APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
            if (!stGuard.bLocked) break;
            g_pstInfo->u16AppIdx = pstEvt->u16Param;
            g_pstInfo->bAppBound = true;
        }
        LREP(TAG, "BIND AppKey 0x%04X -> Generic OnOff Server", pstEvt->u16Param);
        app_mesh_MarkDirty();
        break;
    }

    case MESH_EVT_SUB_ADD: {
        {
            APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
            if (!stGuard.bLocked) break;
            g_pstInfo->u16Group = pstEvt->u16Param;
        }
        LREP(TAG, "SUBSCRIBE Group 0x%04X", pstEvt->u16Param);
        app_mesh_MarkDirty();
        break;
    }

    case MESH_EVT_SUB_DEL: {
        {
            APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
            if (!stGuard.bLocked) break;
            if (g_pstInfo->u16Group == pstEvt->u16Param) g_pstInfo->u16Group = 0;
        }
        LREP(TAG, "UNSUBSCRIBE Group 0x%04X", pstEvt->u16Param);
        app_mesh_MarkDirty();
        break;
    }

    case MESH_EVT_ONOFF_SET: {
        {
            APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
            if (!stGuard.bLocked) break;

            g_pstInfo->u8OnOff    = pstEvt->u8OnOff;
            g_pstInfo->u16LastSrc = pstEvt->u16Src;
            g_pstInfo->u16LastDst = pstEvt->u16Dst;
            g_pstInfo->u32RxCount++;

            /* Ring buffer log cung mien du lieu -> cung mutex */
            st_mesh_log_t *pstEntry = &g_pstLog[g_u32LogHead];
            pstEntry->s64TimeUs = pstEvt->s64TimeUs;
            pstEntry->u16Src    = pstEvt->u16Src;
            pstEntry->u16Dst    = pstEvt->u16Dst;
            pstEntry->u8OnOff   = pstEvt->u8OnOff;
            pstEntry->u8Ttl     = pstEvt->u8Ttl;
            pstEntry->s8Rssi    = pstEvt->s8Rssi;
            g_u32LogHead = (g_u32LogHead + 1) % DF_MESH_LOG_DEPTH;
            if (g_u32LogCount < DF_MESH_LOG_DEPTH) g_u32LogCount++;
        }
        LREP(TAG, "[RX] OnOff=%u src=0x%04X dst=0x%04X(%s) ttl=%u rssi=%d t=%lld us",
             pstEvt->u8OnOff, pstEvt->u16Src, pstEvt->u16Dst,
             ESP_BLE_MESH_ADDR_IS_GROUP(pstEvt->u16Dst) ? "GROUP" : "UNICAST",
             pstEvt->u8Ttl, pstEvt->s8Rssi, (long long)pstEvt->s64TimeUs);
        app_mesh_MarkDirty();
        break;
    }

    default:
        break;
    }
}

static void app_mesh_Task(void *pvParam)
{
    st_mesh_evt_t stEvt;

    while (1) {
        if (xQueueReceive(g_hMeshEvtQ, &stEvt, pdMS_TO_TICKS(500)) == pdTRUE) {
            app_mesh_HandleEvt(&stEvt);
        }

        /* Gom nhieu thay doi thanh 1 lan ghi flash (giam mon Flash) */
        if (g_bDirty &&
            (esp_timer_get_time() - g_s64DirtyUs) >= ((int64_t)DF_MESH_PERSIST_DELAY_MS * 1000)) {
            g_bDirty = false;
            app_mesh_SaveState();
        }
    }
}

/* ================== KHOI TAO BLUETOOTH ================== */
static esp_err_t app_mesh_BluetoothInit(void)
{
    esp_err_t eRet;
    esp_bt_controller_config_t stBtCfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    eRet = esp_bt_controller_init(&stBtCfg);
    if (eRet != ESP_OK) { LREP_ERROR(TAG, "bt_controller_init: %s", esp_err_to_name(eRet)); return eRet; }

    eRet = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (eRet != ESP_OK) { LREP_ERROR(TAG, "bt_controller_enable: %s", esp_err_to_name(eRet)); return eRet; }

    eRet = esp_bluedroid_init();
    if (eRet != ESP_OK) { LREP_ERROR(TAG, "bluedroid_init: %s", esp_err_to_name(eRet)); return eRet; }

    eRet = esp_bluedroid_enable();
    if (eRet != ESP_OK) { LREP_ERROR(TAG, "bluedroid_enable: %s", esp_err_to_name(eRet)); return eRet; }

    return ESP_OK;
}

/* ================== PUBLIC APIS ================== */

int32_t app_mesh_Init(void)
{
    if (g_bMeshInitialized) return ERROR_CODE_OK;   

    // Cấp phát bộ nhớ vừa đủ để chứa 1 struct st_mesh_info_t
    g_pstInfo = (st_mesh_info_t *)  MALLOC_PSRAM(sizeof(st_mesh_info_t));
    //Cấp phát mảng Ring Buffer lưu log gồm DF_MESH_LOG_DEPTH phần tử
    g_pstLog  = (st_mesh_log_t *)   MALLOC_PSRAM(sizeof(st_mesh_log_t) * DF_MESH_LOG_DEPTH);

    if (g_pstInfo == NULL || g_pstLog == NULL) {
        LREP_ERROR(TAG, "Không đủ PSRAM cho Mesh!");
        FREE_SAFE(g_pstInfo);
        FREE_SAFE(g_pstLog);
        return ERROR_CODE_NO_MEMORY;
    }

    // Khởi tạo một Mutex (Mutual Exclusion) để bảo vệ vùng dữ liệu chung (g_pstInfo và g_pstLog) 
    // khỏi nguy cơ tranh chấp dữ liệu (Race Condition) khi được truy cập từ nhiều Task khác nhau.
    g_hMeshMutex = xSemaphoreCreateMutex();

    // Khởi tạo hàng đợi FIFO (First-In, First-Out) chứa các sự kiện nhận từ mạng Mesh
    g_hMeshEvtQ  = xQueueCreate(DF_MESH_EVT_QUEUE_LEN, sizeof(st_mesh_evt_t));

    if (g_hMeshMutex == NULL || g_hMeshEvtQ == NULL) {
        LREP_ERROR(TAG, "Tạo mutex/queue thất bại!");
        return ERROR_CODE_FAIL;
    }

    /* 1. Nạp trạng thái cũ từ LittleFS */
    app_mesh_LoadState();

    uint8_t u8OnOff = 0;
    {
        // Giới hạn vùng hoạt động của Lock Guard. Mutex chỉ bị giữ trong đúng khoảng thời gian đọc/ghi 2 biến, 
        // sau đó tự động giải phóng ngay lập tức khi ra khỏi cặp dấu ngoặc nhọn {}
        APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
        if (!stGuard.bLocked) return ERROR_CODE_TIMEOUT;    // Kiểm tra xem việc lấy Mutex có bị quá thời gian cho phép
        g_pstInfo->u32BootCount++;                          // Tăng số lần thiết bị đã khởi động lên 1 unit.
        u8OnOff = g_pstInfo->u8OnOff;                       // Lấy trạng thái On/Off cũ
    }
    g_stOnoffSrv.state.onoff = u8OnOff;
    app_mesh_MarkDirty();

    /* 2. Task xu ly su kien (tao truoc de callback BLE khong mat su kien) */
    if (xTaskCreate(app_mesh_Task, "app_mesh_task", DF_MESH_TASK_STACK, NULL, DF_MESH_TASK_PRIO, &g_hMeshTask) != pdPASS) {
        LREP_ERROR(TAG, "Tạo task mesh thất bại!");
        return ERROR_CODE_FAIL;
    }

    /* 3. Bluetooth */
    if (app_mesh_BluetoothInit() != ESP_OK) return ERROR_CODE_FAIL;

    /* 4. UUID thiet bi = 0x3210 + MAC BT; ten hien thi tren App = prefix + 2 byte cuoi MAC */
    uint8_t au8Mac[6] = {0};
    esp_read_mac(au8Mac, ESP_MAC_BT);
    memcpy(&g_au8DevUuid[2], au8Mac, sizeof(au8Mac));

    char acName[32];
    snprintf(acName, sizeof(acName), "%s%02X%02X", DF_MESH_DEVICE_NAME_PREFIX, au8Mac[4], au8Mac[5]);

    // 5. đăng ký các hàm phản hồi sự kiện (Callback Functions) từ ứng dụng với ESP-IDF BLE Mesh Stack trước khi tiến hành khởi tạo mạng
    // Tất cả 3 hàm callback trên (app_mesh_ProvCb, app_mesh_CfgSrvCb, app_mesh_GenericSrvCb) đều chạy trực tiếp trên Task của BLE Stack (Host Task)
    esp_ble_mesh_register_prov_callback(app_mesh_ProvCb);                   // Đăng ký Callback Provisioning
    esp_ble_mesh_register_config_server_callback(app_mesh_CfgSrvCb);        // Đăng ký Callback Configuration Server
    esp_ble_mesh_register_generic_server_callback(app_mesh_GenericSrvCb);   // Đăng ký Generic Server
    esp_ble_mesh_register_custom_model_callback(app_mesh_VendorServerCb);    // Đăng ký Vendor Model Server (nhận SET/GET tùy biến)

    // Khởi tạo lõi (Core) của BLE Mesh Stack với các thông số cấu hình chính
    esp_err_t eRet = esp_ble_mesh_init(&g_stProv, &g_stComp);
    if (eRet != ESP_OK) {
        LREP_ERROR(TAG, "esp_ble_mesh_init: %s", esp_err_to_name(eRet));
        return ERROR_CODE_FAIL;
    }

    // Đặt tên thiết bị sẽ được phát ra trong gói tin quảng bá Unprovisioned Beacon
    esp_ble_mesh_set_unprovisioned_device_name(acName);

    // Bật chế độ Cấp mạng: Cho phép Node phát quảng bá sẵn sàng chờ App/Gateway kết nối để nạp mạng (Provisioning).
    eRet = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (eRet != ESP_OK) {
        LREP_ERROR(TAG, "node_prov_enable: %s", esp_err_to_name(eRet));
        return ERROR_CODE_FAIL;
    }

    g_bMeshInitialized = true;
    LREP(TAG, "BLE Mesh Node OnOff Server san sang. Ten thiet bi: %s", acName);
    return ERROR_CODE_OK;
}

void app_mesh_PrintStatus(void)
{
    if (!g_bMeshInitialized) {
        printf("Mesh chua khoi tao\n");
        return;
    }

    /* Snapshot: giu khoa ngan, in va goi API mesh ngoai khoa */
    st_mesh_info_t stSnap;
    {
        APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
        if (!stGuard.bLocked) {
            printf("Mesh dang ban (timeout lay mutex)\n");
            return;
        }
        stSnap = *g_pstInfo;
    }

    bool     bProvLive = esp_ble_mesh_node_is_provisioned();
    uint16_t u16Addr   = bProvLive ? esp_ble_mesh_get_primary_element_address() : 0;

    printf("\n--- TRANG THAI BLE MESH NODE ---\n");
    printf("Provisioned   : %s\n", bProvLive ? "YES" : "NO (dang cho cap mang)");
    printf("Unicast       : 0x%04X\n", u16Addr);
    printf("NetIdx/AppIdx : 0x%04X / 0x%04X\n", stSnap.u16NetIdx, stSnap.u16AppIdx);
    printf("AppKey bound  : %s\n", stSnap.bAppBound ? "YES" : "NO");
    printf("Vendor Model  : Server 0x%04X (CID=0x%04X)\n", DF_MESH_VND_MODEL_ID_SERVER, DF_MESH_CID_ESP);
    if (stSnap.u16Group != 0) printf("Group sub     : 0x%04X\n", stSnap.u16Group);
    else                      printf("Group sub     : (chua)\n");
    printf("OnOff state   : %u\n", stSnap.u8OnOff);
    printf("Boot / RX cnt : %u / %u (drop evt: %u)\n",
           (unsigned)stSnap.u32BootCount, (unsigned)stSnap.u32RxCount, (unsigned)g_u32DropEvt);
    printf("Last src/dst  : 0x%04X -> 0x%04X\n", stSnap.u16LastSrc, stSnap.u16LastDst);
    printf("Relay         : %s\n", DF_MESH_RELAY ? "ENABLED" : "DISABLED");
    printf("--------------------------------\n\n");
}

void app_mesh_PrintLog(void)
{
    if (!g_bMeshInitialized) {
        printf("Mesh chua khoi tao\n");
        return;
    }

    /* Snapshot ring buffer (32 muc, ~768 byte) ra stack roi in ngoai khoa */
    st_mesh_log_t astSnap[DF_MESH_LOG_DEPTH];
    uint32_t u32Count = 0;
    uint32_t u32Start = 0;
    {
        APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
        if (!stGuard.bLocked) {
            printf("Mesh dang ban (timeout lay mutex)\n");
            return;
        }
        memcpy(astSnap, g_pstLog, sizeof(astSnap));
        u32Count = g_u32LogCount;
        u32Start = (g_u32LogCount < DF_MESH_LOG_DEPTH) ? 0 : g_u32LogHead;
    }

    printf("\n--- LOG %u LENH ONOFF GAN NHAT ---\n", (unsigned)u32Count);
    printf("%-3s | %-8s | %-8s | %-5s | %-3s | %-4s | %s\n", "#", "src", "dst", "onoff", "ttl", "rssi", "delta(ms)");

    int64_t s64Prev = 0;
    for (uint32_t i = 0; i < u32Count; i++) {
        const st_mesh_log_t *pstEntry = &astSnap[(u32Start + i) % DF_MESH_LOG_DEPTH];
        double dDeltaMs = (i == 0) ? 0.0 : (double)(pstEntry->s64TimeUs - s64Prev) / 1000.0;
        printf("%-3u | 0x%04X   | 0x%04X   | %-5u | %-3u | %-4d | %.1f\n",
               (unsigned)i, pstEntry->u16Src, pstEntry->u16Dst, pstEntry->u8OnOff,
               pstEntry->u8Ttl, pstEntry->s8Rssi, dDeltaMs);
        s64Prev = pstEntry->s64TimeUs;
    }
    printf("---------------------------------\n\n");
}

int32_t app_mesh_Reset(void)
{
    if (!g_bMeshInitialized) return ERROR_CODE_FAIL;

    LREP_WARNING(TAG, "Thuc hien Reset Node (Local Reset)...");

    /* 1. Xoa NetKey/AppKey/Unicast trong NVS của BLE Mesh Stack */
    esp_err_t eRet = esp_ble_mesh_node_local_reset();
    if (eRet != ESP_OK) {
        LREP_ERROR(TAG, "node_local_reset that bai: %s", esp_err_to_name(eRet));
    }

    /* 2. Chu dong xoa file trang thai trong LittleFS ngay lap tuc */
    if (app_littlefs_FileExists(DF_MESH_STATE_FILE)) {
        remove(DF_MESH_STATE_FILE);
        LREP(TAG, "Da xoa file trang thai LittleFS: %s", DF_MESH_STATE_FILE);
    }

    /* 3. Restart chip sau 500ms de dam bao log in xong */
    app_common_DelayMs(500);
    esp_restart();

    return ERROR_CODE_OK;
}

int32_t app_mesh_SendOnOff(uint16_t u16DstAddr, uint8_t u8OnOff)
{
    if (!g_bMeshInitialized) {
        printf("Mesh chua khoi tao!\n");
        return ERROR_CODE_FAIL;
    }

    uint16_t u16NetIdx = 0;
    uint16_t u16AppIdx = 0;

    {
        APP_LOCK_GUARD(stGuard, g_hMeshMutex, MESH_INFO_MUTEX_NAME, DF_LOCK_TIMEOUT_MS);
        if (!stGuard.bLocked) return ERROR_CODE_TIMEOUT;
        u16NetIdx = g_pstInfo->u16NetIdx;
        u16AppIdx = g_pstInfo->u16AppIdx;
    }

    esp_ble_mesh_generic_client_set_state_t stSetParam = {
        .onoff_set.onoff = (u8OnOff ? 1 : 0),
        .onoff_set.op_en = false,
    };

    esp_ble_mesh_client_common_param_t stCommonParam = {
        .opcode      = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK,
        .model       = &g_astRootModels[2], /* Model Client thứ 3 trong mảng */
        .msg_timeout = 0,
        .ctx.net_idx   = u16NetIdx,
        .ctx.app_idx   = u16AppIdx,
        .ctx.addr      = u16DstAddr,
        .ctx.send_ttl  = 7,
    };

    esp_err_t eRet = esp_ble_mesh_generic_client_set_state(&stCommonParam, &stSetParam);
    if (eRet != ESP_OK) {
        LREP_ERROR(TAG, "Gui lenh OnOff_Set that bai: %s", esp_err_to_name(eRet));
        return ERROR_CODE_FAIL;
    }

    LREP(TAG, "[TX] Gui OnOff=%u -> Dst=0x%04X (NetIdx=0x%04X, AppIdx=0x%04X)",
         u8OnOff, u16DstAddr, u16NetIdx, u16AppIdx);
    return ERROR_CODE_OK;
}