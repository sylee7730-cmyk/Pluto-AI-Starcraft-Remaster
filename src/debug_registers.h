#pragma once
#include <cstdint>

// x86 debug-register helpers, kept free of Windows headers so they can be tested anywhere.
// DR7: bit 0 = DR0 enabled for this thread; bits 16-17 = DR0 condition (00 = execute);
// bits 18-19 = DR0 length (00 = one byte, which is what an execute breakpoint requires).
inline uint32_t execute_breakpoint_dr7(uint32_t dr7) {
  dr7 |= 0x1u;
  dr7 &= ~(0xFu << 16);
  return dr7;
}
// DR6 bit 0 tells that DR0 raised the exception.
inline bool breakpoint_zero_hit(uint32_t dr6) { return (dr6 & 0x1u) != 0; }
