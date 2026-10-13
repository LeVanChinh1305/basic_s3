#pragma once
 
#include "app_common.h"
 
/**
 * @brief Khoi tao BLE Mesh Node (OnOff Server): Bluetooth, Provisioning, Models,
 *        task xu ly su kien, nap lai trang thai tu LittleFS.
 *        Yeu cau: nvs_flash_init() va app_littlefs_Init() da duoc goi truoc.
 */
int32_t app_mesh_Init(void);
 
/** @brief In trang thai Node (Unicast, NetKey/AppKey, Group, OnOff, bo dem) */
void app_mesh_PrintStatus(void);
 
/** @brief In bo dem log cac su kien nhan gan nhat (kem delta thoi gian) */
void app_mesh_PrintLog(void);
 
/** @brief Xoa thong tin Provisioning (local reset). Thiet bi se tu khoi dong lai */
int32_t app_mesh_Reset(void);