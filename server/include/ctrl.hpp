#ifndef __CTRL_H__
#define __CTRL_H__

#include <netprotocol_generated.h>

#include "events.hpp"

void get_ctrl(SceCtrlData *pad, SceMotionState *motion_data, SceTouchData *touch_data_front,
              SceTouchData *touch_data_back);
void ctrl_as_netprotocol(SceCtrlData *pad, SceMotionState *motion_data,
                         SceTouchData *touch_data_front, SceTouchData *touch_data_back,
                         flatbuffers::FlatBufferBuilder &builder, int battery_level);

#endif // __CTRL_H__
