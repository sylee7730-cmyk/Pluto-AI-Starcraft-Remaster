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
inline uint32_t disable_breakpoint_dr7(uint32_t dr7) { return dr7 & ~0x1u; }
// DR6 bit 0 tells that DR0 raised the exception.
inline bool breakpoint_zero_hit(uint32_t dr6) { return (dr6 & 0x1u) != 0; }

// An execute breakpoint is a fault: the CPU stops before the instruction runs, so resuming at the same
// address would stop again. Stepping over it is done two ways at once, so one failing is not enough to
// loop forever: the resume flag (RF) tells the CPU to ignore the breakpoint once, and the trap flag (TF)
// makes it stop again right after the instruction, where the breakpoint is put back.
constexpr uint32_t kTrapFlag = 0x100u;
constexpr uint32_t kResumeFlag = 0x10000u;
inline uint32_t begin_step_over(uint32_t eflags) { return eflags | kResumeFlag | kTrapFlag; }
inline uint32_t end_step_over(uint32_t eflags) { return eflags & ~kTrapFlag; }

// Detects an exception storm: the breakpoint firing again and again because the CPU never got past it.
// A healthy game issues a few hundred commands a second; a storm is orders of magnitude more.
class StormGuard {
public:
  explicit StormGuard(unsigned long long limit_per_interval = 20000) : limit(limit_per_interval) {}
  // Called once per interval with the running total of hits; true when the hits since the last call exceed the limit.
  bool storm(unsigned long long total_hits) {
    const unsigned long long delta = total_hits - last;
    last = total_hits;
    return delta > limit;
  }
private:
  unsigned long long limit;
  unsigned long long last = 0;
};
