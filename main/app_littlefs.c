#include "app_littlefs.h"
#include "esp_littlefs.h"
#include <sys/stat.h>
#include <unistd.h>
#include "app_common.h"

static const char *TAG = "app_littlefs";

int32_t app_littlefs_Init(void)
{
    LREP(TAG, "Dang khoi tao va Mount phan vung LittleFS...");

    esp_vfs_littlefs_conf_t stConf = {
        .base_path = DF_LITTLEFS_MOUNT_POINT,
        .partition_label = DF_LITTLEFS_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false
    };

    esp_err_t eRet = esp_vfs_littlefs_register(&stConf);

    if (eRet != ESP_OK) {
        if (eRet == ESP_FAIL) {
            LREP_ERROR(TAG, "Mount hoac Format thiet bi that bai!");
        } else if (eRet == ESP_ERR_NOT_FOUND) {
            LREP_ERROR(TAG, "Khong tim thay partition label '%s'!", DF_LITTLEFS_PARTITION_LABEL);
        } else {
            LREP_ERROR(TAG, "Loi khoi tao LittleFS: %s", esp_err_to_name(eRet));
        }
        return ERROR_CODE_FAIL;
    }

    size_t u32Total = 0, u32Used = 0;
    eRet = esp_littlefs_info(stConf.partition_label, &u32Total, &u32Used);
    if (eRet == ESP_OK) {
        LREP(TAG, "Mount thanh cong! Tong: %u KB | Da dung: %u KB", 
             (unsigned int)(u32Total / 1024), 
             (unsigned int)(u32Used / 1024));
    }

    return ERROR_CODE_OK;
}

int32_t app_littlefs_Deinit(void)
{
    esp_err_t eRet = esp_vfs_littlefs_unregister(DF_LITTLEFS_PARTITION_LABEL);
    if (eRet != ESP_OK) {
        return ERROR_CODE_FAIL;
    }
    LREP(TAG, "Da Unmount LittleFS.");
    return ERROR_CODE_OK;
}

bool app_littlefs_FileExists(const char *pcFilepath)
{
    if (pcFilepath == NULL) return false;
    struct stat st;
    return (stat(pcFilepath, &st) == 0);
}

int32_t app_littlefs_WriteTextFile(const char *pcFilepath, const char *pcData)
{
    DF_CHECK_NULL_PARAM(pcFilepath);
    DF_CHECK_NULL_PARAM(pcData);

    FILE *pFile = fopen(pcFilepath, "w");
    if (pFile == NULL) {
        LREP_ERROR(TAG, "Khong the mo file %s de ghi!", pcFilepath);
        return ERROR_CODE_FAIL;
    }

    int32_t s32BytesWritten = fprintf(pFile, "%s", pcData);
    fclose(pFile);

    if (s32BytesWritten < 0) {
        LREP_ERROR(TAG, "Loi ghi du lieu vao file %s", pcFilepath);
        return ERROR_CODE_FAIL;
    }

    LREP(TAG, "Ghi file thanh cong: %s (%d bytes)", pcFilepath, (int)s32BytesWritten);
    return ERROR_CODE_OK;
}

int32_t app_littlefs_ReadTextFile(const char *pcFilepath, char **ppcOutData)
{
    DF_CHECK_NULL_PARAM(pcFilepath);
    DF_CHECK_NULL_PARAM(ppcOutData);

    *ppcOutData = NULL;

    struct stat st;
    if (stat(pcFilepath, &st) != 0) {
        LREP_WARNING(TAG, "File %s khong ton tai!", pcFilepath);
        return ERROR_CODE_NOT_FOUND;
    }

    FILE *pFile = fopen(pcFilepath, "r");
    if (pFile == NULL) {
        LREP_ERROR(TAG, "Khong the mo file %s de doc!", pcFilepath);
        return ERROR_CODE_FAIL;
    }

    /* Cấp phát bộ nhớ đệm trên External PSRAM */
    char *pcBuffer = (char *)MALLOC_PSRAM(st.st_size + 1);
    if (pcBuffer == NULL) {
        LREP_ERROR(TAG, "Khong du PSRAM de cap phat cho file %s", pcFilepath);
        fclose(pFile);
        return ERROR_CODE_NO_MEMORY;
    }

    size_t u32ReadBytes = fread(pcBuffer, 1, st.st_size, pFile);
    pcBuffer[u32ReadBytes] = '\0';

    fclose(pFile);

    *ppcOutData = pcBuffer;
    LREP(TAG, "Doc file thanh cong: %s (%u bytes)", pcFilepath, (unsigned int)u32ReadBytes);
    return ERROR_CODE_OK;
}

int32_t app_littlefs_DeleteFile(const char *pcFilepath)
{
    DF_CHECK_NULL_PARAM(pcFilepath);

    if (unlink(pcFilepath) == 0) {
        LREP(TAG, "Xoa file thanh cong: %s", pcFilepath);
        return ERROR_CODE_OK;
    }

    LREP_ERROR(TAG, "Xoa file that bai: %s", pcFilepath);
    return ERROR_CODE_FAIL;
}