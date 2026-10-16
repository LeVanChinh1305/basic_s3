#pragma once
 
#include "app_common.h"
 
/**
 * @brief Khởi tạo BLE Mesh Node (OnOff Server): Bluetooth, Provisioning, Models,
 *        task xử lý sự kiện, nạp lại trạng thái từ LittleFS.
 * @note: Yêu cầu: nvs_flash_init() va app_littlefs_Init() đã được gọi trước.
 */
int32_t app_mesh_Init(void);
 
/** @brief In trạng thái Node (Unicast, NetKey/AppKey, Group, OnOff, bộ đếm) */
void app_mesh_PrintStatus(void);
 
/** @brief In bộ đếm log các sự kiện gần nhất (kèm delta thời gian) */
void app_mesh_PrintLog(void);
 
/** @brief Xóa thông tin Provisioning (local reset). Thiết bi sẽ tự khởi động lại */
int32_t app_mesh_Reset(void);

/** 
 * @brief Gửi lệnh Generic OnOff Set Unack từ Console CLI tới một Node/Group
 * @param u16DstAddr Địa chỉ đích (Unicast của Server, ví dụ: 0x0003 hoặc 0xFFFF)
 * @param u8OnOff 0 (OFF) hoặc 1 (ON)
 * @return 0 nếu thành công, mã lỗi nếu thất bại
 */
int32_t app_mesh_SendOnOff(uint16_t u16DstAddr, uint8_t u8OnOff);