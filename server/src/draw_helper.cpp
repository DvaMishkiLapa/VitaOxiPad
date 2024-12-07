#include "draw_helper.hpp"

#include <common.h>

vita2d_pgf *debug_font;
uint32_t need_color = 0;
uint32_t white_color = RGBA8(0xFF, 0xFF, 0xFF, 0xFF); // White color
uint32_t error_color = RGBA8(0xFF, 0x00, 0x00, 0xFF); // Bright red color
uint32_t done_color = RGBA8(0x00, 0xFF, 0x00, 0xFF);  // Bright green color

float screen_width = 960.0f;
float screen_height = 544.0f;

void draw_rectangle_outline(float x, float y, float width, float height, uint32_t color) {
  vita2d_draw_line(x, y, x + width, y, color);
  vita2d_draw_line(x + width, y, x + width, y + height, color);
  vita2d_draw_line(x + width, y + height, x, y + height, color);
  vita2d_draw_line(x, y + height, x, y, color);
}

void draw_start_mode(bool connected_to_network, bool pc_connect_state, char *vita_ip,
                     SharedData *shared_data) {
  float offset = 40;
  float rect_outline_spase = 5;
  float text_offset = 15;
  draw_rectangle_outline(offset, offset, screen_width - 2 * offset, screen_height - 2 * offset,
                         white_color);
  draw_rectangle_outline(rect_outline_spase + offset, rect_outline_spase + offset,
                         screen_width - 2 * (offset + rect_outline_spase),
                         screen_height - 2 * (offset + rect_outline_spase), white_color);
  vita2d_pgf_draw_textf(debug_font, text_offset + offset, 35 + offset, white_color, 1.0,
                        "VitaOxiPad v1.2.0 build %s, %s by theSame, santarl and saidsay-so.",
                        __DATE__, __TIME__);

  vita2d_draw_line(10 + offset, 55 + offset, screen_width - 10 - offset, 55 + offset, white_color);
  vita2d_pgf_draw_text(debug_font, text_offset + offset, 85 + offset, white_color, 1.0, "Control:");
  vita2d_pgf_draw_text(debug_font, text_offset + offset, 115 + offset, white_color, 1.0,
                       "* CROSS => Enter Pad mode");
  vita2d_pgf_draw_text(debug_font, text_offset + offset, 145 + offset, white_color, 1.0,
                       "* START + SELECT in Pad mode => Exit Pad mode");
  vita2d_pgf_draw_text(debug_font, text_offset + offset, 175 + offset, white_color, 1.0,
                       "* START + DPAD UP in Pad mode => Toggle screen On/Off");

  vita2d_draw_line(10 + offset, 380 + offset, screen_width - 10 - offset, 380 + offset,
                   white_color);
  if (connected_to_network) {
    vita2d_pgf_draw_textf(debug_font, 740 - offset, screen_height - offset - 55, white_color, 1.0,
                          "Listening on:\nIP: %s\nPort: %d", vita_ip, NET_PORT);
  } else {
    vita2d_pgf_draw_text(debug_font, 740 - offset, screen_height - offset - 55, error_color, 1.0,
                         "Not connected\nto a network :(");
  }
  if (pc_connect_state) {
    vita2d_pgf_draw_textf(debug_font, text_offset + offset, screen_height - offset - 40, done_color,
                          1.0, "Status:\nConnected (%s)", shared_data->client_ip);
  } else {
    vita2d_pgf_draw_text(debug_font, text_offset + offset, screen_height - offset - 40, error_color,
                         1.0, "Status:\nNot connected :(");
  }
}

void draw_pad_mode(bool connected_to_network, bool pc_connect_state, char *vita_ip,
                   SharedData *shared_data) {
  draw_rectangle_outline(1, 1, screen_width - 1, screen_height - 1, white_color);

  if (connected_to_network) {
    vita2d_pgf_draw_textf(debug_font, 740, 20, white_color, 1.0, "Listening on:\nIP: %s\nPort: %d",
                          vita_ip, NET_PORT);
  } else {
    vita2d_pgf_draw_text(debug_font, 740, 20, error_color, 1.0, "Not connected\nto a network :(");
  }

  if (pc_connect_state) {
    vita2d_pgf_draw_textf(debug_font, 5, 20, done_color, 1.0, "Status:\nConnected (%s)",
                          shared_data->client_ip);
  } else {
    vita2d_pgf_draw_text(debug_font, 5, 20, error_color, 1.0, "Status:\nNot connected :(");
  }

  vita2d_pgf_draw_text(debug_font, 5, 515, white_color, 1.0, "START + SELECT => Exit Pad mode");
  vita2d_pgf_draw_text(debug_font, 5, 535, white_color, 1.0,
                       "START + DPAD UP => Toggle screen On/Off");

  if (shared_data->charger_connected) {
    need_color = done_color;
  } else if (shared_data->battery_level < 30) {
    need_color = error_color;
  } else {
    need_color = white_color;
  }
  vita2d_pgf_draw_textf(debug_font, 740, 515, need_color, 1.0, "Battery: %s%d%%",
                        shared_data->charger_connected ? "+" : "", shared_data->battery_level);
  if (shared_data->wifi_signal_strength < 50) {
    need_color = error_color;
  } else {
    need_color = white_color;
  }
  vita2d_pgf_draw_textf(debug_font, 740, 535, need_color, 1.0, "WiFi signal: %d%%",
                        shared_data->wifi_signal_strength);
}
