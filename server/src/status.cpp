#include <psp2/net/netctl.h>
#include <psp2/power.h>

#include "status.hpp"
#include <assert.h>

int status_thread(unsigned int arglen, void *argp) {
  assert(arglen == sizeof(ThreadMessage));

  ThreadMessage *message = static_cast<ThreadMessage *>(argp);
  SharedData *shared_data = message->shared_data;

  int previous_battery_level = 0;
  bool previous_charger_connected = scePowerIsBatteryCharging();
  unsigned int previous_wifi_signal_strength = 0;

  while (g_status_thread_running.load()) {
    // Updating the battery level
    int current_battery_level = scePowerGetBatteryLifePercent();
    if (current_battery_level != previous_battery_level) {
      previous_battery_level = current_battery_level;
      {
        std::lock_guard<std::mutex> lock(shared_data->mutex);
        shared_data->battery_level = current_battery_level;
        shared_data->events |= MainEvent::BATTERY_LEVEL;
      }
      sceKernelSetEventFlag(message->ev_flag, MainEvent::BATTERY_LEVEL);
    }

    // Updating the status of the charger
    bool current_charger_connected = scePowerIsBatteryCharging();
    if (current_charger_connected != previous_charger_connected) {
      previous_charger_connected = current_charger_connected;
      {
        std::lock_guard<std::mutex> lock(shared_data->mutex);
        shared_data->charger_connected = current_charger_connected;
        shared_data->events |= MainEvent::STATUS_CHARGER;
      }
      sceKernelSetEventFlag(message->ev_flag, MainEvent::STATUS_CHARGER);
    }

    // Updating the Wi-Fi signal strength
    unsigned int current_wifi_signal_strength = 0;
    SceNetCtlInfo wifi_info;
    int ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &wifi_info);
    if (ret == 0) {
      current_wifi_signal_strength = wifi_info.rssi_percentage;
    }

    if (current_wifi_signal_strength != previous_wifi_signal_strength) {
      previous_wifi_signal_strength = current_wifi_signal_strength;
      {
        std::lock_guard<std::mutex> lock(shared_data->mutex);
        shared_data->wifi_signal_strength = current_wifi_signal_strength;
        shared_data->events |= MainEvent::WIFI_SIGNAL;
      }
      sceKernelSetEventFlag(message->ev_flag, MainEvent::WIFI_SIGNAL);
    }

    sceKernelDelayThread(1 * 1000 * 1000);
  }

  return 0;
}
