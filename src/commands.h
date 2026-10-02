#pragma once
#include <cstdint>
#include <functional>
#include <vector>
using Packet=std::vector<uint8_t>;
// Convert the old 16-bit unit handles into SCR's 32-bit command format.
// Optional safety filter for the "allies as own forces" experiment. Pluto then
// believes it owns its allies' units, but the game would not obey orders for
// them. Handles reported as foreign are never selected, and commands that act on
// the current selection are dropped while the selection Pluto asked for held no
// controllable units, so orders cannot fall through to a previous selection.
struct CommandFilter {
  std::function<bool(uint16_t)> foreign;
  bool selection_blocked=false;  // Persists across calls like the game's selection.
};
std::vector<Packet> translate_commands(const uint8_t* bytes,size_t length,const std::function<uint32_t(uint16_t)>& unit,CommandFilter* filter=nullptr);
