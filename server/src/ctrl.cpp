#include <psp2/libdbg.h>
#include <psp2/motion.h>
#include <psp2/touch.h>

#include "ctrl.hpp"
#include "kctrl-kernel.h"

NetProtocol::ButtonsData convert_pad_data(const SceCtrlData &data) {
  return NetProtocol::ButtonsData(
      (data.buttons & SCE_CTRL_SELECT) > 0, (data.buttons & SCE_CTRL_START) > 0,
      (data.buttons & SCE_CTRL_UP) > 0, (data.buttons & SCE_CTRL_RIGHT) > 0,
      (data.buttons & SCE_CTRL_DOWN) > 0, (data.buttons & SCE_CTRL_LEFT) > 0,
      (data.buttons & SCE_CTRL_LTRIGGER) > 0, (data.buttons & SCE_CTRL_RTRIGGER) > 0,
      (data.buttons & SCE_CTRL_TRIANGLE) > 0, (data.buttons & SCE_CTRL_CIRCLE) > 0,
      (data.buttons & SCE_CTRL_CROSS) > 0, (data.buttons & SCE_CTRL_SQUARE) > 0,
      (data.buttons & SCE_CTRL_VOLUP) > 0, (data.buttons & SCE_CTRL_VOLDOWN) > 0,
      (data.buttons & SCE_CTRL_PSBUTTON) > 0);
}

flatbuffers::Offset<NetProtocol::TouchData>
convert_touch_data(flatbuffers::FlatBufferBuilder &builder, const SceTouchData &data) {
  std::vector<NetProtocol::TouchReport> reports;
  reports.reserve(data.reportNum);
  std::transform(data.report, data.report + data.reportNum, std::back_inserter(reports),
                 [](const SceTouchReport &report) {
                   return NetProtocol::TouchReport(report.force, report.id, report.x, report.y);
                 });
  return NetProtocol::CreateTouchDataDirect(builder, &reports);
}

void get_ctrl(SceCtrlData *pad, SceMotionState *motion_data, SceTouchData *touch_data_front,
              SceTouchData *touch_data_back) {
  static uint64_t last_ts = 1024;

  int res = kctrlGetCtrlData(0, pad, 1);
  if (res < 0) {
    SCE_DBG_LOG_ERROR("kctrlGetCtrlData failed: 0x%08X", res);
    return;
  }
  while (pad->timeStamp <= last_ts) {
    kctrlGetCtrlData(0, pad, 1);
  }

  if (pad->buttons & SCE_CTRL_VOLDOWN || pad->buttons & SCE_CTRL_VOLUP)
    sceKernelDelayThread(100 * 1000);

  sceTouchPeek(SCE_TOUCH_PORT_FRONT, touch_data_front, 1);
  sceTouchPeek(SCE_TOUCH_PORT_BACK, touch_data_back, 1);
  sceMotionGetState(motion_data);
  last_ts = pad->timeStamp;
}

void ctrl_as_netprotocol(SceCtrlData *pad, SceMotionState *motion_data,
                         SceTouchData *touch_data_front, SceTouchData *touch_data_back,
                         flatbuffers::FlatBufferBuilder &builder, int battery_level) {
  builder.Clear();

  auto buttons = convert_pad_data(*pad);
  auto data_front = convert_touch_data(builder, *touch_data_front);
  auto data_back = convert_touch_data(builder, *touch_data_back);

  NetProtocol::Vector3 accel(motion_data->acceleration.x, motion_data->acceleration.y,
                             motion_data->acceleration.z);
  NetProtocol::Vector3 gyro(motion_data->angularVelocity.x, motion_data->angularVelocity.y,
                            motion_data->angularVelocity.z);
  NetProtocol::MotionData motion(gyro, accel);

  auto content =
      NetProtocol::CreatePad(builder, &buttons, pad->lx, pad->ly, pad->rx, pad->ry, data_front,
                             data_back, &motion, pad->timeStamp, battery_level);

  auto packet =
      NetProtocol::CreatePacket(builder, NetProtocol::PacketContent::Pad, content.Union());
  builder.FinishSizePrefixed(packet);
}
