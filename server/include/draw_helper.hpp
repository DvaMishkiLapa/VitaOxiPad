#include <vita2d.h>

#include "events.hpp"

#ifndef DRAW_HELPER_HPP
#define DRAW_HELPER_HPP

extern vita2d_pgf *debug_font;
extern uint32_t need_color;
extern uint32_t common_color;
extern uint32_t error_color;
extern uint32_t done_color;

void draw_rectangle_outline(float x, float y, float width, float height, uint32_t color);
void draw_start_mode(bool connected_to_network, bool pc_connect_state, char *vita_ip,
                   SharedData *shared_data);
void draw_pad_mode(bool connected_to_network, bool pc_connect_state, char *vita_ip,
                   SharedData *shared_data);

#endif // DRAW_HELPER_HPP
