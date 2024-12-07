#include <psp2/appmgr.h>
#include <psp2/libdbg.h>
#include <psp2/motion.h>
#include <psp2/power.h>
#include <psp2/shellutil.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>
#include <psp2/vshbridge.h>
#include <taihen.h>

#include "ctrl.hpp"
#include "draw_helper.hpp"
#include "net.hpp"
#include "status.hpp"
#include "thread_helper.hpp"

#include "kctrl-kernel.h"

// #include <common.h>

#define MOD_PATH "ux0:app/VOXIPAD01/module/kctrl.skprx"

constexpr size_t NET_INIT_SIZE = 1 * 1024 * 1024;
constexpr size_t TARGET_FPS = 15;
constexpr size_t FRAME_DURATION_MS = 1000 / TARGET_FPS;

std::atomic<bool> g_net_thread_running(true);
std::atomic<bool> g_status_thread_running(true);

int main() {
  SceUID mod_id;
  int search_param[2];
  SceUID res = _vshKernelSearchModuleByName("kctrl", search_param);
  if (res <= 0) {
    tai_module_args_t argg;
    memset(&argg, 0, sizeof(argg));
    argg.size = sizeof(argg);
    argg.pid = KERNEL_PID;
    mod_id = taiLoadStartKernelModuleForUser(MOD_PATH, &argg);
    SCE_DBG_LOG_DEBUG("kctrl.skprx loading status: 0x%08X", mod_id);
    sceKernelDelayThread(1000000);
    sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
  }

  // Enabling analog, motion and touch support
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  sceMotionStartSampling();
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchEnableTouchForce(SCE_TOUCH_PORT_FRONT);
  sceTouchEnableTouchForce(SCE_TOUCH_PORT_BACK);

  // Motion stuffs
  sceMotionSetGyroBiasCorrection(1);
  sceMotionSetTiltCorrection(1);
  sceMotionSetDeadband(0);

  // Reduce CPU and GPU frequency to save battery
  scePowerSetArmClockFrequency(41);
  scePowerSetBusClockFrequency(55);
  scePowerSetGpuClockFrequency(41);
  scePowerSetGpuXbarClockFrequency(83);

  // Initializing graphics stuffs
  vita2d_init();
  vita2d_set_clear_color(RGBA8(0x00, 0x00, 0x00, 0xFF));
  debug_font = vita2d_load_default_pgf();

  // Initializing network stuffs
  sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  char vita_ip[INET_ADDRSTRLEN];
  std::vector<uint8_t> net_init_memory(NET_INIT_SIZE);
  SceNetInitParam initparam = {net_init_memory.data(), NET_INIT_SIZE, 0};
  int ret = sceNetShowNetstat();
  if ((unsigned)ret == SCE_NET_ERROR_ENOTINIT) {
    ret = sceNetInit(&initparam);
    if (ret < 0) {
      SCE_DBG_LOG_ERROR("Network initialization failed: %s", sce_net_strerror(ret));
      return -1;
    }
  }

  sceNetCtlInit();
  SceNetCtlInfo info;

  SharedData shared_data;
  SceUID ev_flag = sceKernelCreateEventFlag("main_event_flag", 0, 0, NULL);
  shared_data.events = 0;
  shared_data.battery_level = scePowerGetBatteryLifePercent();
  shared_data.charger_connected = scePowerIsBatteryCharging();
  shared_data.pad_mode = false;
  shared_data.display_on = true;
  ThreadMessage message = {ev_flag, &shared_data};

  // Creating events and status thread
  ThreadParams status_thread_params{
      "StatusThread", &status_thread, 0x10000100,           0x10000, 0, SCE_KERNEL_CPU_MASK_USER_1,
      nullptr,        &message,       sizeof(ThreadMessage)};
  SceUID status_thread_uid = create_and_start_thread(status_thread_params);
  if (status_thread_uid < 0) {
    return -1;
  }

  // Creating events and network thread
  ThreadParams net_thread_params{"NetThread", &net_thread, 0x10000100,
                                 0x10000,     0,           SCE_KERNEL_CPU_MASK_USER_2,
                                 nullptr,     &message,    sizeof(ThreadMessage)};
  SceUID net_thread_uid = create_and_start_thread(net_thread_params);
  if (net_thread_uid < 0) {
    return -1;
  }

  uint32_t events;
  sceNetCtlInetGetState(reinterpret_cast<int *>(&events));
  bool connected_to_network = events & SCE_NETCTL_STATE_CONNECTED;
  bool pc_connect_state = false;
  if (connected_to_network) {
    sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);
    snprintf(vita_ip, INET_ADDRSTRLEN, "%s", info.ip_address);
  }
  events = 0;

  bool exit_state = true;
  while (exit_state) {
    auto frame_start = std::chrono::high_resolution_clock::now();
    sceKernelPollEventFlag(ev_flag, 0xFFFFFFFF, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR, &events);

    vita2d_start_drawing();
    vita2d_clear_screen();

    if (events & MainEvent::NET_CONNECT) {
      connected_to_network = true;
      sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);
      snprintf(vita_ip, INET_ADDRSTRLEN, "%s", info.ip_address);
    } else if (events & MainEvent::NET_DISCONNECT) {
      connected_to_network = false;
    }

    if (events & MainEvent::PC_CONNECT) {
      pc_connect_state = true;
    } else if (events & MainEvent::PC_DISCONNECT) {
      pc_connect_state = false;
    }

    if ((shared_data.pad_data.buttons & SCE_CTRL_CROSS) && !shared_data.pad_mode) {
      shared_data.pad_mode = true;
      sceShellUtilInitEvents(0);
      sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2);
      sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_POWEROFF_MENU);
    }

    if ((shared_data.pad_data.buttons & SCE_CTRL_SELECT &&
         shared_data.pad_data.buttons & SCE_CTRL_START) &&
        shared_data.pad_mode) {
      shared_data.pad_mode = false;
      sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2);
      sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_POWEROFF_MENU);
      shared_data.display_on = true;
      kctrlScreenOn();
    }

    if (shared_data.pad_mode) {
      if ((shared_data.pad_data.buttons & SCE_CTRL_UP) &&
          (shared_data.pad_data.buttons & SCE_CTRL_START)) {
        shared_data.display_on = !shared_data.display_on;
        kctrlToggleScreen();
        sceKernelDelayThread(300 * 1000);
      }
      if (shared_data.display_on) {
        draw_pad_mode(connected_to_network, pc_connect_state, vita_ip, &shared_data);
      }
    } else {
      draw_start_mode(connected_to_network, pc_connect_state, vita_ip, &shared_data);
    }

    vita2d_end_drawing();
    vita2d_wait_rendering_done();
    vita2d_swap_buffers();

    auto frame_end = std::chrono::high_resolution_clock::now();
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(frame_end - frame_start).count();
    if (elapsed_ms < FRAME_DURATION_MS) {
      SceUInt delay_us = static_cast<SceUInt>((FRAME_DURATION_MS - elapsed_ms) * 1000);
      sceKernelDelayThread(delay_us);
    }
  }

  // Turn on network thread stop signal and wait for its normal termination
  SceUInt THREAD_TIMEOUT = (SceUInt)(15 * 1000 * 1000);
  g_net_thread_running.store(false);
  g_status_thread_running.store(false);
  sceKernelSetEventFlag(ev_flag, MainEvent::NET_DISCONNECT);
  SCE_DBG_LOG_TRACE("StatusThread stop...");
  stop_thread(status_thread_uid, THREAD_TIMEOUT);
  SCE_DBG_LOG_TRACE("NetThread stop...");
  stop_thread(net_thread_uid, THREAD_TIMEOUT);

  sceNetCtlTerm();
  sceNetTerm();
  sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);

  vita2d_fini();
  vita2d_free_pgf(debug_font);
  return 1;
}
