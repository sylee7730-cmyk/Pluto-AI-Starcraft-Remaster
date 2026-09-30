#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>
// Generated from samase_scarf analysis of the running, verified 13515 x86 image.
namespace scr {
constexpr uint32_t analyzed_base = 0x4a0000;
inline uintptr_t addr(uint32_t va) { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + (va - analyzed_base); }
template<class T> inline T read(uintptr_t p) { T v; std::memcpy(&v,reinterpret_cast<void*>(p),sizeof(v)); return v; }
inline uint32_t game() { return (read<uint32_t>(addr(0xfa63ccu)) ^ 0x10fae560u); }
inline uint32_t players() { return ((0xfb5969c2u - read<uint32_t>(addr(0x101f0e4u))) ^ read<uint32_t>(addr(0x1240e38u))); }
inline uint32_t first_active_unit() { return read<uint32_t>(addr(0x10436a4u)); }
inline uint32_t first_hidden_unit() { return read<uint32_t>(addr(0x10436b4u)); }
inline uint32_t units() { return read<uint32_t>(addr(0x10436f0u)); }
inline uint32_t local_player_id() { return read<uint32_t>(addr(0xfcacf4u)); }
inline uint32_t is_replay() { return read<uint32_t>(addr(0x1244580u)); }
inline uint32_t is_multiplayer() { return read<uint8_t>(addr(0x123f7bcu)); }
inline uint32_t is_paused() { return read<uint32_t>(addr(0x104a1ccu)); }
inline uint32_t map_tile_flags() { return read<uint32_t>(addr(0x1049478u)); }
inline uint32_t minitile_data() { return read<uint32_t>(addr(0x1049470u)); }
inline uint32_t tileset_cv5() { return read<uint32_t>(addr(0x1273020u)); }
inline uint32_t tileset_indexed_map_tiles() { return read<uint32_t>(addr(0x104948cu)); }
inline uint32_t pathing() { return read<uint32_t>(addr(0x1246478u)); }
inline uint32_t screen_x() { return read<uint32_t>(addr(0x1085b88u)); }
inline uint32_t screen_y() { return read<uint32_t>(addr(0x1085b8cu)); }
inline uint32_t command_user() { return read<uint32_t>(addr(0xfcace8u)); }
inline uint32_t unique_command_user() { return read<uint32_t>(addr(0xfcacecu)); }
inline uintptr_t step_objects() { return addr(0x0077f820u); }
inline uintptr_t send_command() { return addr(0x007da250u); }
inline uintptr_t process_commands() { return addr(0x007d6870u); }
inline uintptr_t print_text() { return addr(0x007b3200u); }
}