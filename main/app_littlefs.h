#ifndef APP_LITTLEFS_H_
#define APP_LITTLEFS_H_

#include <stdio.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo và Mount phân vùng LittleFS
 * @return int32_t ERROR_CODE_OK nếu thành công
 */
int32_t app_littlefs_Init(void);

/**
 * @brief Unmount phân vùng LittleFS
 * @return int32_t Mã lỗi hệ thống
 */
int32_t app_littlefs_Deinit(void);

/**
 * @brief Đọc file văn bản/JSON, cấp phát bộ nhớ an toàn trên PSRAM
 * @param pcFilepath Đường dẫn file (vd: "/storage/config.json")
 * @param ppcOutData Con trỏ nhận địa chỉ buffer chứa dữ liệu
 * @return int32_t Mã lỗi hệ thống
 */
int32_t app_littlefs_ReadTextFile(const char *pcFilepath, char **ppcOutData);

/**
 * @brief Ghi chuỗi văn bản/JSON vào file trong LittleFS
 * @param pcFilepath Đường dẫn file
 * @param pcData Chuỗi dữ liệu cần ghi
 * @return int32_t Mã lỗi hệ thống
 */
int32_t app_littlefs_WriteTextFile(const char *pcFilepath, const char *pcData);

/**
 * @brief Kiểm tra sự tồn tại của file
 * @param pcFilepath Đường dẫn file
 * @return true nếu file tồn tại, false nếu không
 */
bool app_littlefs_FileExists(const char *pcFilepath);

/**
 * @brief Xóa file khỏi LittleFS
 * @param pcFilepath Đường dẫn file cần xóa
 * @return int32_t Mã lỗi hệ thống
 */
int32_t app_littlefs_DeleteFile(const char *pcFilepath);

#ifdef __cplusplus
}
#endif

#endif