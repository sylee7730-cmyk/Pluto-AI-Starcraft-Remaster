#pragma once
#include <cstddef>
#include <cstdint>

// Watches a function in the game without writing to its code.
//
// The game's code pages are mapped read-execute only, so an inline hook is refused
// (VirtualProtect fails with ERROR_INVALID_PARAMETER). A hardware execute breakpoint needs no
// write access: the CPU raises an exception when the first instruction of the function is about
// to run, the handler reads the arguments, and execution continues unchanged.
namespace command_watch {
using Observer = void (*)(const uint8_t* data, size_t size);
struct Report {
  unsigned armed = 0;           // threads that now carry the breakpoint
  unsigned failed = 0;          // threads that refused it
  unsigned long handler_error = 0;
};
// target: address of `void __cdecl f(const uint8_t* data, size_t size)`.
// Arms every current thread, then keeps arming threads created later.
Report install(uintptr_t target, Observer observer);
}
