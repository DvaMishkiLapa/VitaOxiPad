#ifndef EVENTS_HPP
#define EVENTS_HPP

#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>

#include <arpa/inet.h>

#include <atomic>
#include <mutex>

enum MainEvent {
  // For NetThread
  PC_DISCONNECT = 1 << 0,  // 0x0001
  PC_CONNECT = 1 << 1,     // 0x0002
  NET_CONNECT = 1 << 2,    // 0x0004
  NET_DISCONNECT = 1 << 3, // 0x0008

  // For StatusThread
  BATTERY_LEVEL = 1 << 8,  // 0x0100
  STATUS_CHARGER = 1 << 9, // 0x0200
  WIFI_SIGNAL = 1 << 10,   // 0x0400
};

struct SharedData {
  std::atomic<uint32_t> events;
  int battery_level = 0;
  bool charger_connected = false;
  unsigned int wifi_signal_strength = 0;
  char client_ip[INET_ADDRSTRLEN] = "N/A";
  SceCtrlData pad_data;
  bool pad_mode = false;
  bool display_on = true;
  std::mutex mutex;
};

typedef struct {
  SceUID ev_flag;
  SharedData *shared_data;
} ThreadMessage;

#endif // EVENTS_HPP
