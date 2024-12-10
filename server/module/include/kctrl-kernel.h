#ifndef _KCTRL_KERNEL_H_
#define _KCTRL_KERNEL_H_

#include <psp2/ctrl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KCTRL_MODULE_API 1 // +1, if module changed

void kctrlScreenOn(void);
void kctrlScreenOff(void);
void kctrlToggleScreen(void);
int kctrlGetCtrlData(int port, SceCtrlData *pad_data, int count);
uint8_t kctrlVersion(void);

#ifdef __cplusplus
}
#endif

#endif // __KCTRL_KERNEL_H_
