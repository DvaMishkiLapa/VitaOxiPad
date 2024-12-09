#include <psp2kern/ctrl.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/debug.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/threadmgr.h>
#include <taihen.h>

#include "kctrl-kernel.h"

// missing in vitasdk
int ksceOledDisplayOn(void);
int ksceOledDisplayOff(void);
int ksceOledGetBrightness(void);
int ksceOledSetBrightness(int brightness);

int ksceLcdDisplayOn(void);
int ksceLcdDisplayOff(void);
int ksceLcdGetBrightness(void);
int ksceLcdSetBrightness(int brightness);

static uint8_t g_is_oled = 0;
static uint8_t g_is_lcd = 0;
static int g_screen_off = 0;
static int g_prev_brightness;

void kctrlScreenOn() {
  uint32_t state;
  ENTER_SYSCALL(state);

  if (g_is_oled) {
    ksceOledDisplayOn();
    ksceOledSetBrightness(g_prev_brightness);
  } else if (g_is_lcd) {
    ksceLcdDisplayOn();
    ksceLcdSetBrightness(g_prev_brightness);
  }

  EXIT_SYSCALL(state);
}

void kctrlScreenOff() {
  uint32_t state;
  ENTER_SYSCALL(state);

  if (g_is_oled) {
    // g_prev_brightness = ksceOledGetBrightness();
    ksceOledDisplayOff();
  } else if (g_is_lcd) {
    // g_prev_brightness = ksceLcdGetBrightness();
    ksceLcdDisplayOff();
  }

  EXIT_SYSCALL(state);
}

void kctrlToggleScreen() {
  if (g_screen_off) {
    kctrlScreenOn();
    g_screen_off = 0;
  } else {
    kctrlScreenOff();
    g_screen_off = 1;
  }
}

int kctrlGetCtrlData(int port, SceCtrlData *pad_data, int count) {
  SceCtrlData pad;

  uint32_t state;
  ENTER_SYSCALL(state);

  int res = ksceCtrlReadBufferPositive(port, &pad, count);
  ksceKernelMemcpyKernelToUser(pad_data, &pad, sizeof(SceCtrlData));

  EXIT_SYSCALL(state);
  return res;
}

uint8_t kctrlVersion() { return KCTRL_MODULE_API; }

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, const void *argp) {
  ksceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  if (ksceKernelSearchModuleByName("SceLcd") >= 0) {
    g_is_lcd = 1;
  } else if (ksceKernelSearchModuleByName("SceOled") >= 0) {
    g_is_oled = 1;
  }

  if (g_is_oled) {
    g_prev_brightness = ksceOledGetBrightness();
  } else if (g_is_lcd) {
    g_prev_brightness = ksceLcdGetBrightness();
  }
  return SCE_KERNEL_START_SUCCESS;
}

void _stop() __attribute__((weak, alias("module_stop")));
int module_stop(SceSize args, const void *argp) { return SCE_KERNEL_STOP_SUCCESS; }
