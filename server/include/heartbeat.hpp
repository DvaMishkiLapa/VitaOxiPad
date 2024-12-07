#ifndef HEARTBEAT_HPP
#define HEARTBEAT_HPP

#include <array>

const std::array<uint8_t, 6> heartbeat_magic{0xff, 0xff, 0xff, 0xff, 0x42, 0x54};

#endif // HEARTBEAT_HPP
