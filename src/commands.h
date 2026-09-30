#pragma once
#include <cstdint>
#include <functional>
#include <vector>
using Packet=std::vector<uint8_t>;
// Convert the old 16-bit unit handles into SCR's 32-bit command format.
std::vector<Packet> translate_commands(const uint8_t* bytes,size_t length,const std::function<uint32_t(uint16_t)>& unit);
