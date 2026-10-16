#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <esp_ble_mesh_defs.h>

#define DF_MESH_CID_ESP                 0x02E5
#define DF_MESH_VND_MODEL_ID_CLIENT     0x0001
#define DF_MESH_VND_MODEL_ID_SERVER     0x0002

#define DF_MESH_VND_OP_SET      ESP_BLE_MESH_MODEL_OP_3(0x01, DF_MESH_CID_ESP)
#define DF_MESH_VND_OP_GET      ESP_BLE_MESH_MODEL_OP_3(0x02, DF_MESH_CID_ESP)
#define DF_MESH_VND_OP_STATUS   ESP_BLE_MESH_MODEL_OP_3(0x03, DF_MESH_CID_ESP)

typedef struct __attribute__((packed)) {
    uint8_t  u8Cmd;
    uint16_t u16PacketId;
    int16_t  s16Temperature;
    uint16_t u16Humidity;
    uint8_t  u8RelayState;      /* 0/1 */
    char     acDeviceTag[16];   /* tag text */
} st_vendor_custom_data_t;