#ifndef APP_BUTTON_H_
#define APP_BUTTON_H_
#include "app_common.h"
#ifdef __cplusplus
extern "C" {
#endif
int32_t app_button_Init(void);
bool    app_button_IsPressed(void);
#ifdef __cplusplus
}
#endif
#endif