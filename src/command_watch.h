#pragma once
#include <cstddef>
#include <cstdint>

// Watches a function in the game without writing to its code.
//
// The game's code pages are mapped read-execute only, so an inline hook is refused
// (VirtualProtect fails with ERROR_INVALID_PARAMETER). A hardware execute breakpoint needs no
// write access: the CPU raises an exception when the first instruction of the function is about
// to run, the handler reads the arguments, and execution continues unchanged.
//
// Arming every thread at start-up froze the game (one core at 100%), so nothing is armed by install():
// the bridge asks for the threads that actually run the game loop, once a match has started. A watchdog
// switches the breakpoint off again if it ever fires in a storm, so the worst case is a lost feature,
// never a frozen game.
namespace command_watch {
using Observer = void (*)(const uint8_t* data, size_t size);
struct Report {
  unsigned long handler_error = 0;  // non-zero: the exception handler could not be registered
};
struct Status {
  unsigned armed = 0;               // threads that now carry the breakpoint
  unsigned failed = 0;              // threads that refused it
  unsigned long long hits = 0;      // times the breakpoint fired
  bool stormed = false;             // the watchdog saw a storm and disarmed everything
};
// target: address of `void __cdecl f(const uint8_t* data, size_t size)`. Registers the handler and the
// watchdog; arms nothing.
Report install(uintptr_t target, Observer observer);
// Arms one thread (idempotent). Safe to call from that very thread: the registers are written by a helper.
void watch_thread(uint32_t thread_id);
Status status();
}
