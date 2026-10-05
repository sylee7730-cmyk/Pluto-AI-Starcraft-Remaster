#include "command_watch.h"
#include "debug_registers.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#if !defined(_M_IX86) && !defined(__i386__)
#error "command_watch reads the 32-bit CONTEXT; the bridge is a Win32 build"
#endif

namespace command_watch {
namespace {
uintptr_t watched = 0;
Observer observer_function = nullptr;
std::atomic<unsigned long long> hits{0};
std::atomic<bool> stormed{false};
std::atomic<unsigned> armed_threads{0}, failed_threads{0};
std::mutex ids_mutex;
std::vector<DWORD> known_threads;       // every thread asked for, armed or not
thread_local bool stepping = false;     // this thread is between a breakpoint hit and the step that follows it

LONG CALLBACK on_exception(PEXCEPTION_POINTERS info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* context = info->ContextRecord;
  if (stepping) {  // The single step right after the breakpoint: put the breakpoint back.
    stepping = false;
    context->EFlags = end_step_over(context->EFlags);
    if (!stormed.load()) context->Dr7 = execute_breakpoint_dr7(context->Dr7);
    context->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (!breakpoint_zero_hit(context->Dr6) || context->Eip != watched) return EXCEPTION_CONTINUE_SEARCH;
  ++hits;
  // __cdecl on entry: [esp] = return address, [esp+4] = data, [esp+8] = size.
  const auto* arguments = reinterpret_cast<const uintptr_t*>(context->Esp);
  const auto* data = reinterpret_cast<const uint8_t*>(arguments[1]);
  const size_t size = arguments[2];
  if (observer_function && data && size && size <= 1024) observer_function(data, size);
  context->Dr7 = disable_breakpoint_dr7(context->Dr7);  // do not stop on this instruction again ...
  context->EFlags = begin_step_over(context->EFlags);   // ... and stop right after it (RF + TF)
  context->Dr6 = 0;
  stepping = true;
  return EXCEPTION_CONTINUE_EXECUTION;
}

// Debug registers are per thread and a running thread cannot change its own, so the target is suspended
// for the moment it takes to write them. Nothing that allocates runs while it is suspended.
bool write_debug_registers(DWORD id, bool enable) {
  HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, id);
  if (!thread) return false;
  bool written = false;
  if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context)) {
      context.Dr0 = static_cast<DWORD>(watched);
      context.Dr7 = enable ? execute_breakpoint_dr7(context.Dr7) : disable_breakpoint_dr7(context.Dr7);
      context.Dr6 = 0;
      written = SetThreadContext(thread, &context) != FALSE;
    }
    ResumeThread(thread);
  }
  CloseHandle(thread);
  return written;
}

DWORD WINAPI arm_worker(LPVOID parameter) {
  const DWORD id = static_cast<DWORD>(reinterpret_cast<uintptr_t>(parameter));
  if (write_debug_registers(id, true)) ++armed_threads; else ++failed_threads;
  return 0;
}

// Cuts a storm short: if the breakpoint ever fires faster than any game could issue commands, the CPU is
// not getting past it. The registers are cleared from outside, which frees the spinning thread.
DWORD WINAPI watchdog(LPVOID) {
  StormGuard guard;
  for (;;) {
    Sleep(100);
    if (!guard.storm(hits.load()) || stormed.exchange(true)) continue;
    std::vector<DWORD> ids;
    { std::lock_guard<std::mutex> lock(ids_mutex); ids = known_threads; }
    for (const DWORD id : ids) write_debug_registers(id, false);
  }
}
}  // namespace

Report install(uintptr_t target, Observer observer) {
  Report report;
  watched = target;
  observer_function = observer;
  if (!AddVectoredExceptionHandler(1, on_exception)) {
    report.handler_error = GetLastError();
    return report;
  }
  if (HANDLE guard = CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr)) CloseHandle(guard);
  return report;
}

void watch_thread(uint32_t thread_id) {
  if (stormed.load()) return;
  {
    std::lock_guard<std::mutex> lock(ids_mutex);
    if (std::find(known_threads.begin(), known_threads.end(), thread_id) != known_threads.end()) return;
    known_threads.push_back(thread_id);
  }
  if (HANDLE helper = CreateThread(nullptr, 0, arm_worker, reinterpret_cast<LPVOID>(static_cast<uintptr_t>(thread_id)), 0, nullptr))
    CloseHandle(helper);
  else ++failed_threads;
}

Status status() {
  return {armed_threads.load(), failed_threads.load(), hits.load(), stormed.load()};
}
}  // namespace command_watch
