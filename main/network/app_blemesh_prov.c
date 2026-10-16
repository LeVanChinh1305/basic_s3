#include "app_blemesh_prov.h"
#include "app_littlefs.h"
#include "cJSON.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "app_mesh_vendor.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "app_mesh_prov";

#define PROV_MUTEX_NAME "MESH_PROV_MUTEX"
#define PROV_NET_IDX 0x0000
#define PROV_APP_IDX 0x0000

// AppKey cố định do Provisioner quản lý để nạp cho các Node trong mạng 
static uint8_t g_au8AppKey[16] = {0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF};

// Khai báo UUID và Composition Data của Provisioner 
static uint8_t g_au8DevUuid[16] = {0x32, 0x10};

static esp_ble_mesh_prov_t g_stProv = {
    .uuid = g_au8DevUuid,
    .prov_uuid = g_au8DevUuid,
    .prov_unicast_addr = 0x0001, // ESP32 Provisioner luôn mang địa chỉ 0x0001 
    .prov_start_address = 0x0002, // Tự phân bổ địa chỉ cho Node mới bắt đầu từ 0x0002 
};

static esp_ble_mesh_cfg_srv_t g_stCfgSrv = {
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .default_ttl = 7,
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

// Provisioner cần Config Client để tự gửi lệnh cấu hình (Add/Bind AppKey) sang Node mới 
static esp_ble_mesh_client_t g_stCfgCli;
static esp_ble_mesh_client_t g_stOnoffCli;

static esp_ble_mesh_model_t g_astRootModels[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&g_stCfgSrv),
    ESP_BLE_MESH_MODEL_CFG_CLI(&g_stCfgCli),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(NULL, &g_stOnoffCli),
};

static bool g_bProvInitialized = false;

// Bảng Opcode tiếp nhận của Client Model (Lắng nghe phản hồi STATUS từ Server) 
static esp_ble_mesh_model_op_t g_astVndClientOp[] = {
    ESP_BLE_MESH_MODEL_OP(DF_MESH_VND_OP_STATUS, sizeof(st_vendor_custom_data_t)),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t g_astVndModelsClient[] = {
    ESP_BLE_MESH_VENDOR_MODEL(
        DF_MESH_CID_ESP,
        DF_MESH_VND_MODEL_ID_CLIENT,
        g_astVndClientOp,
        NULL,
        NULL
    ),
};

// Nhúng vào Element của Provisioner 
static esp_ble_mesh_elem_t g_astElements[] = {
    ESP_BLE_MESH_ELEMENT(0, g_astRootModels, g_astVndModelsClient),
};

static esp_ble_mesh_comp_t g_stComp = {
    .cid = DF_MESH_COMPANY_ID,
    .element_count = ARRAY_SIZE(g_astElements),
    .elements = g_astElements,
};

static void app_mesh_VendorClientCb(esp_ble_mesh_model_cb_event_t eEvent, esp_ble_mesh_model_cb_param_t *pstParam)
{
    switch (eEvent) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT: {
        if (pstParam->model_operation.opcode == DF_MESH_VND_OP_STATUS) {
            st_vendor_custom_data_t *pstRsp = (st_vendor_custom_data_t *)pstParam->model_operation.msg;
            ESP_LOGI("VND_CLI", "===> [NODE 0x%04X ĐÃ PHẢN HỒI STATUS] <===", pstParam->model_operation.ctx->addr);
            ESP_LOGI("VND_CLI", "   Packet ID   : %u", pstRsp->u16PacketId);
            ESP_LOGI("VND_CLI", "   Relay State : %u", pstRsp->u8RelayState);
            ESP_LOGI("VND_CLI", "   Tag Text    : %s", pstRsp->acDeviceTag);
        }
        break;
    }

    default:
        break;
    }
}

// ================== CALLBACK PROVISIONER (HANDSHAKE TỰ ĐỘNG)===================================

static void app_mesh_ProvisionerProvCb(esp_ble_mesh_prov_cb_event_t eEvent, esp_ble_mesh_prov_cb_param_t *pstParam) {
    switch (eEvent) {
    // [Bước 1/7]: Nhận Unprovisioned Device Beacon 
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT: {
        LREP(TAG, "[HANDSHAKE 1/7] Phát hiện thiết bị chưa có mạng! UUID: %02X%02X...",
            pstParam->provisioner_recv_unprov_adv_pkt.dev_uuid[0],
            pstParam->provisioner_recv_unprov_adv_pkt.dev_uuid[1]);

        esp_ble_mesh_unprov_dev_add_t stUnprovDev = {0};
        memcpy(stUnprovDev.addr, pstParam->provisioner_recv_unprov_adv_pkt.addr, 6);
        stUnprovDev.addr_type = pstParam->provisioner_recv_unprov_adv_pkt.addr_type;
        memcpy(stUnprovDev.uuid, pstParam->provisioner_recv_unprov_adv_pkt.dev_uuid, 16);
        stUnprovDev.oob_info = pstParam->provisioner_recv_unprov_adv_pkt.oob_info;
        stUnprovDev.bearer = pstParam->provisioner_recv_unprov_adv_pkt.bearer;

        // Tự động kích hoạt chu trình cấp mạng (Invite -> Capabilities -> Public Key -> Complete) 
        esp_ble_mesh_provisioner_add_unprov_dev(&stUnprovDev, ADD_DEV_START_PROV_NOW_FLAG | ADD_DEV_FLUSHABLE_DEV_FLAG);
        break;
    }

    // [Bước 7/7]: Cấp mạng thành công -> Tiến hành gửi AppKey sang Node mới 
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT: {
        uint16_t u16NodeAddr = pstParam->provisioner_prov_complete.unicast_addr;
        LREP(TAG,
            "[HANDSHAKE 7/7] CẤP MẠNG THÀNH CÔNG! Đã phân bổ Unicast Address: "
            "0x%04X",
            u16NodeAddr);

        // Tự động tạo bản tin Add AppKey gửi sang Node mới 
        esp_ble_mesh_client_common_param_t stCommon = {
            .opcode = ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD,
            .model = g_stCfgCli.model,
            .msg_timeout = 5000,
            .ctx.net_idx = PROV_NET_IDX,
            .ctx.app_idx = PROV_APP_IDX,
            .ctx.addr = u16NodeAddr,
            .ctx.send_ttl = 7,
        };

        esp_ble_mesh_cfg_app_key_add_t stAppKeyAdd = {
            .net_idx = PROV_NET_IDX,
            .app_idx = PROV_APP_IDX,
        };
        memcpy(stAppKeyAdd.app_key, g_au8AppKey, 16);

        esp_err_t err = esp_ble_mesh_config_client_set_state(
            &stCommon, (esp_ble_mesh_cfg_client_set_state_t *)&stAppKeyAdd);
        if (err == ESP_OK) {
            LREP(TAG, "Đang tự động gửi AppKey sang Node 0x%04X...", u16NodeAddr);
        } else {
            LREP_ERROR(TAG, "Gửi AppKey thất bại: %s", esp_err_to_name(err));
        }
        break;
    }

    // Thêm AppKey cục bộ thành công -> Bind vào Generic OnOff Client cục bộ của Provisioner 
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT: {
        LREP(TAG,
            "Provisioner: Đã thêm AppKey 0x%04X -> Đang Bind vào Generic OnOff "
            "Client cục bộ...",
            pstParam->provisioner_add_app_key_comp.app_idx);
        esp_err_t err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(
            g_stProv.prov_unicast_addr,
            pstParam->provisioner_add_app_key_comp.app_idx,
            ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI, 
            ESP_BLE_MESH_CID_NVAL);
        if (err != ESP_OK) {
            LREP_ERROR(TAG, "Bind AppKey cục bộ thất bại: %s", esp_err_to_name(err));
        }
        break;
    }

    case ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT: {
        LREP(TAG, "===> Provisioner ĐÃ BIND APPKEY 0x%04X VÀO CLIENT MODEL (0x%04X) THÀNH CÔNG! <===",
            pstParam->provisioner_bind_app_key_to_model_comp.app_idx,
            pstParam->provisioner_bind_app_key_to_model_comp.element_addr);
        break;
    }

    default:
        break;
    }
}

// ================== CALLBACK CONFIG CLIENT (TỰ ĐỘNG BIND APPKEY) ================== 

static void app_mesh_ConfigClientCb(esp_ble_mesh_cfg_client_cb_event_t eEvent, esp_ble_mesh_cfg_client_cb_param_t *pstParam) {
    if (eEvent != ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT)
        return;

    uint16_t u16NodeAddr = pstParam->params->ctx.addr;

    switch (pstParam->params->opcode) {
    // Khi Node mới phản hồi đã nhận AppKey thành công -> Tự động gửi tiếp lệnh Bind AppKey vào Generic OnOff Server 
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD: {
        LREP(TAG, "Node 0x%04X đã nhận AppKey! Đang gửi lệnh BIND AppKey vào Generic OnOff Server...", u16NodeAddr);

        esp_ble_mesh_client_common_param_t stCommon = {
            .opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND,
            .model = g_stCfgCli.model,
            .msg_timeout = 5000,
            .ctx.net_idx = PROV_NET_IDX,
            .ctx.app_idx = PROV_APP_IDX,
            .ctx.addr = u16NodeAddr,
            .ctx.send_ttl = 7,
        };

        esp_ble_mesh_cfg_model_app_bind_t stModelBind = {
            .element_addr = u16NodeAddr,
            .model_app_idx = PROV_APP_IDX,
            .company_id = 0xFFFF, // Standard Bluetooth SIG Model 
            .model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV,
        };

        esp_ble_mesh_config_client_set_state( &stCommon, (esp_ble_mesh_cfg_client_set_state_t *)&stModelBind);
        break;
    }

    // Khi Node mới phản hồi đã Bind AppKey xong -> Mạng hoàn tất toàn bộ cấu hình tự động 
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND: {
        LREP(TAG,
            "===> HOÀN TẤT CẤU HÌNH TỰ ĐỘNG CHO NODE 0x%04X! SẴN SÀNG TRUYỀN LỆNH <===",
            u16NodeAddr);
        break;
    }

    default:
        break;
    }
}

// ================== CALLBACK GENERIC CLIENT (ON/OFF STATUS) ==================

static void app_mesh_GenericClientCb(esp_ble_mesh_generic_client_cb_event_t eEvent, esp_ble_mesh_generic_client_cb_param_t *pstParam) {
    if (!pstParam || !pstParam->params) return;

    switch (eEvent) {
    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
        if (pstParam->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
            LREP(TAG, "===> [PROV ACK THÀNH CÔNG] Node 0x%04X đã phản hồi! Trạng thái hiện tại: OnOff=%u <===",
                pstParam->params->ctx.addr,
                pstParam->status_cb.onoff_status.present_onoff);
        }
        break;

    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
        LREP_WARNING(TAG, "[PROV TIMEOUT] Quá thời gian chờ phản hồi từ Node 0x%04X (Opcode 0x%04X)!",
            pstParam->params->ctx.addr, (unsigned int)pstParam->params->opcode);
        break;

    default:
        LREP(TAG, "[PROV GEN CLI] Event=0x%02X Opcode=0x%04X Addr=0x%04X err=0x%02X",
            eEvent, (unsigned int)pstParam->params->opcode,
            pstParam->params->ctx.addr, pstParam->error_code);
        break;
    }
}

// ================== KHI BẬT BLUETOOTH & KHỞI TẠO ================== 

static esp_err_t app_mesh_BluetoothInit(void) {
    esp_bt_controller_config_t stBtCfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&stBtCfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    return ESP_OK;
}

int32_t app_mesh_prov_Init(void) {
    if (g_bProvInitialized)
        return ERROR_CODE_OK;

    // 1. Khởi tạo Bluetooth Controller 
    if (app_mesh_BluetoothInit() != ESP_OK)
        return ERROR_CODE_FAIL;

    // 2. Đọc địa chỉ MAC Bluetooth 
    uint8_t au8Mac[6] = {0};
    esp_read_mac(au8Mac, ESP_MAC_BT);
    memcpy(&g_au8DevUuid[2], au8Mac, sizeof(au8Mac));

    // 3. Đăng ký các Callback cho Provisioner Engine 
    esp_ble_mesh_register_prov_callback(app_mesh_ProvisionerProvCb);
    esp_ble_mesh_register_config_client_callback(app_mesh_ConfigClientCb);
    esp_ble_mesh_register_generic_client_callback(app_mesh_GenericClientCb);
    esp_ble_mesh_register_custom_model_callback(app_mesh_VendorClientCb);

    // 4. Khởi tạo Mesh Core 
    esp_err_t eRet = esp_ble_mesh_init(&g_stProv, &g_stComp);
    if (eRet != ESP_OK) {
        LREP_ERROR(TAG, "esp_ble_mesh_init thất bại: %s", esp_err_to_name(eRet));
        return ERROR_CODE_FAIL;
    }
    
    // 5. KÍCH HOẠT PROVISIONER TRƯỚC (Stack sẽ tự động tạo NetKey 0x0000 tại đây)
    eRet = esp_ble_mesh_provisioner_prov_enable(
        (esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (eRet != ESP_OK) {
        LREP_ERROR(TAG, "provisioner_prov_enable thất bại: %s", esp_err_to_name(eRet));
        return ERROR_CODE_FAIL;
    }
    
    // 6. NẠP APPKEY VÀO NETKEY 0x0000 VỪA ĐƯỢC TẠO 
    eRet = esp_ble_mesh_provisioner_add_local_app_key(g_au8AppKey, PROV_NET_IDX, PROV_APP_IDX);
    if (eRet != ESP_OK && eRet != ESP_ERR_INVALID_ARG) {
        LREP_ERROR(TAG, "add_local_app_key thất bại: %s", esp_err_to_name(eRet));
        return ERROR_CODE_FAIL;
    }

    // Đảm bảo Generic OnOff Client cục bộ được Bind với AppKey 
    esp_ble_mesh_provisioner_bind_app_key_to_local_model(
        g_stProv.prov_unicast_addr, PROV_APP_IDX,
        ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI, ESP_BLE_MESH_CID_NVAL);

    g_bProvInitialized = true;
    LREP(TAG, "=========================================================");
    LREP(TAG, "  ESP32 PROVISIONER CỨNG ĐÃ KÍCH HOẠT SẮN SÀNG!");
    LREP(TAG, "  Đang tự động quét và gia nhập các Node mới...");
    LREP(TAG, "=========================================================");

    return ERROR_CODE_OK;
}

int32_t app_mesh_prov_SendOnOff(uint16_t u16DstAddr, uint8_t u8OnOff) {
    if (!g_bProvInitialized)
        return ERROR_CODE_FAIL;

    static uint8_t s_u8Tid = 0;
    esp_ble_mesh_generic_client_set_state_t stSetParam = {
        .onoff_set.op_en = false,
        .onoff_set.onoff = (u8OnOff ? 1 : 0),
        .onoff_set.tid   = ++s_u8Tid,
    };

    esp_ble_mesh_client_common_param_t stCommonParam = {
        .opcode      = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET, // Lệnh có ACK để theo dõi phản hồi 
        .model       = &g_astRootModels[2],
        .msg_timeout = 3000,                                // Chờ ACK 3 giây 
        .ctx.net_idx = PROV_NET_IDX,
        .ctx.app_idx = PROV_APP_IDX,
        .ctx.addr    = u16DstAddr,
        .ctx.send_ttl = 7,
    };

    esp_err_t eRet = esp_ble_mesh_generic_client_set_state(&stCommonParam, &stSetParam);
    if (eRet != ESP_OK) {
        LREP_ERROR(TAG, "Gửi OnOff thất bại: %s", esp_err_to_name(eRet));
        return ERROR_CODE_FAIL;
    }

    LREP(TAG, "[PROV TX] Đã gửi lệnh OnOff=%u (TID=%u) tới Node 0x%04X (Đang chờ phản hồi ACK...)",
        u8OnOff, stSetParam.onoff_set.tid, u16DstAddr);
    return ERROR_CODE_OK;
}

bool app_mesh_prov_IsInitialized(void) {
    return g_bProvInitialized;
}

void app_mesh_prov_PrintStatus(void) {
    printf("\n--- TRẠNG THÁI BLE MESH PROVISIONER (ESP32-S3) ---\n");
    printf("Trạng thái       : %s\n", g_bProvInitialized ? "ĐÃ KÍCH HOẠT SẴN SÀNG" : "CHƯA KHỞI TẠO");
    printf("Unicast Provision: 0x%04X\n", g_stProv.prov_unicast_addr);
    printf("NetIdx / AppIdx  : 0x%04X / 0x%04X\n", PROV_NET_IDX, PROV_APP_IDX);
    printf("Client Model     : Generic OnOff Client (Bound: YES)\n");
    printf("Node kế tiếp     : 0x%04X\n", g_stProv.prov_start_address);
    printf("---------------------------------------------------\n\n");
}