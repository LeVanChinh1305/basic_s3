#pragma once

#include "app_common.h"
#include "app_mesh_vendor.h"

/**
 * @brief Khởi tạo BLE Mesh Provisioner Engine trên ESP32 #1.
 *        Tự động quét, cấp mạng (Provision) và Bind AppKey cho Node mới.
 * @return ERROR_CODE_OK nếu thành công.
 */
int32_t app_mesh_prov_Init(void);

/**
 * @brief Gửi lệnh OnOff từ Provisioner tới Node trong mạng.
 * @param u16DstAddr Địa chỉ Unicast của Node đích (ví dụ: 0x0002)
 * @param u8OnOff 0 (OFF) hoặc 1 (ON)
 */
int32_t app_mesh_prov_SendOnOff(uint16_t u16DstAddr, uint8_t u8OnOff);

/**
 * @brief Gửi gói tin Vendor tùy biến từ Provisioner tới Node trong mạng.
 * @param u16DstAddr Địa chỉ Unicast của Node đích
 * @param pstData Con trỏ struct chứa dữ liệu tùy biến
 */
int32_t app_mesh_prov_SendVendorData(uint16_t u16DstAddr, const st_vendor_custom_data_t *pstData);

/**
 * @brief Kiểm tra Provisioner đã được khởi tạo thành công chưa.
 */
bool app_mesh_prov_IsInitialized(void);

/**
 * @brief In trạng thái chi tiết của ESP32 Provisioner.
 */
void app_mesh_prov_PrintStatus(void);