#pragma once 
#include <stdint.h>
#include <stdbool.h>

#include <esp_ble_mesh_defs.h>

// định nghĩa CID và model ID
#define DF_MESH_CID_ESP                 0x02E5  // Company ID của Espressif (SIG cấp)
#define DF_MESH_VND_MODEL_ID_CLIENT     0x0001  // Model ID của Client (Phía gửi lệnh - S3)
#define DF_MESH_VND_MODEL_ID_SERVER     0x0002  // Model ID của Server (Phía chấp hành - C3)

// định nghĩa opcode 3 bytes chuẩn bị bluetooth mesh 
#define DF_MESH_VND_OP_SET      ESP_BLE_MESH_MODEL_OP_3(0X01, DF_MESH_CID_ESP)      // gửi lệnh set
#define DF_MESH_VND_OP_GET      ESP_BLE_MESH_MODEL_OP_3(0X02, DF_MESH_CID_ESP)      // gửi lệnh get 
#define DF_MESH_VND_OP_STATUS   ESP_BLE_MESH_MODEL_OP_3(0X03, DF_MESH_CID_ESP)      // phản hồi status

// định nghĩa gói dữ liệu tùy biến - BẮT BUỘC có __attribute__((packed)) để không bị compiler chèn byte rỗng (padding)
typedef struct __attribute__((packed)){
    uint8_t u8Cmd;                  // 1: bật/ tắt    2: cài đặt ngưỡng     3: đồng bộ
    uint16_t u16PacketId;           // số thứ tự bản tin chống trùng lặp
    int16_t s16Temperature;         // nhiệt độ 
    uint16_t u16Humidity;           // độ ẩm 
    uint8_t u8RelayState[16];       // chuỗi ký tự ghi chú thiết bị
} st_vendor_custom_data_t; 