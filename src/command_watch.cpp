#include "command_watch.h"
#include "debug_registers.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <vector>

#if !defined(_M_IX86) && !defined(__i386__)
#error "command_watch reads the 32-bit CONTEXT; the bridge is a Win32 build"
#endif

namespace command_watch {
namespace {
uintptr_t watched = 0;
Observer observer_function = nullptr;
std::vector<DWORD> handled_threads;  // armed or refused; touched by one thread at a time

LONG CALLBACK on_exception(PEXCEPTION_POINTERS info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* context = info->ContextRecord;
  if (!breakpoint_zero_hit(context->Dr6) || context->Eip != watched) return EXCEPTION_CONTINUE_SEARCH;
  // __cdecl on entry: [esp] = return address, [esp+4] = data, [esp+8] = size.
  const auto* arguments = reinterpret_cast<const uintptr_t*>(context->Esp);
  const auto* data = reinterpret_cast<const uint8_t*>(arguments[1]);
  const size_t size = arguments[2];
  if (observer_function && data && size && size <= 1024) observer_function(data, size);
  context->Dr6 = 0;
  context->EFlags |= 0x10000;  // RF: resume this instruction without triggering the breakpoint again
  return EXCEPTION_CONTINUE_EXECUTION;
}

// Debug registers are per thread, and a thread cannot change its own while running, so the thread is
// suspended for the moment it takes to write them. Nothing that allocates runs while it is suspended.
bool arm_thread(DWORD id) {
  HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, id);
  if (!thread) return false;
  bool armed = false;
  if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context)) {
      context.Dr0 = static_cast<DWORD>(watched);
      context.Dr7 = execute_breakpoint_dr7(context.Dr7);
      context.Dr6 = 0;
      armed = SetThreadContext(thread, &context) != FALSE;
    }
    ResumeThread(thread);
  }
  CloseHandle(thread);
  return armed;
}

std::vector<DWORD> process_threads() {
  std::vector<DWORD> ids;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return ids;
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  const DWORD process = GetCurrentProcessId();
  for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry))
    if (entry.th32OwnerProcessID == process) ids.push_back(entry.th32ThreadID);
  CloseHandle(snapshot);
  return ids;
}

void arm_new_threads(Report& report) {
  const DWORD self = GetCurrentThreadId();
  for (const DWORD id : process_threads()) {
    if (id == self || std::find(handled_threads.begin(), handled_threads.end(), id) != handled_threads.end()) continue;
    const bool armed = arm_thread(id);
    handled_threads.push_back(id);  // a refused thread is not retried every second
    if (armed) ++report.armed; else ++report.failed;
  }
}

DWORD WINAPI rearm_loop(LPVOID) {
  for (;;) {
    Sleep(1000);
    Report ignored;
    arm_new_threads(ignored);
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
  arm_new_threads(report);
  if (report.armed > 0)
    if (HANDLE worker = CreateThread(nullptr, 0, rearm_loop, nullptr, 0, nullptr)) CloseHandle(worker);
  return report;
}
}  // namespace command_watch
