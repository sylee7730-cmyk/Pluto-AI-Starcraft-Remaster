#include <Windows.h>
#include <TlHelp32.h>
#include <bcrypt.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Handle {
  HANDLE value;
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(std::string(message) + " (win32=" + std::to_string(GetLastError()) + ")");
}
std::string sha256(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  require(bool(file), "Cannot read target executable");
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
  std::array<unsigned char, 32> digest{};
  require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, bytes.data(),
    static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size())) >= 0, "SHA256 failed");
  std::string hex;
  const char* digits = "0123456789abcdef";
  for (auto byte : digest) { hex += digits[byte >> 4]; hex += digits[byte & 15]; }
  return hex;
}
uintptr_t remote_module(DWORD pid, const wchar_t* name) {
  Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)};
  require(snapshot.value != INVALID_HANDLE_VALUE, "Cannot inspect target modules");
  MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
  for (BOOL ok = Module32FirstW(snapshot.value, &entry); ok; ok = Module32NextW(snapshot.value, &entry))
    if (_wcsicmp(entry.szModule, name) == 0) return reinterpret_cast<uintptr_t>(entry.modBaseAddr);
  return 0;
}
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 3) { std::fputs("Usage: scr-loader <StarCraft PID> <absolute bridge DLL path>\n", stderr); return 2; }
  try {
    const DWORD pid = std::stoul(argv[1]);
    auto dll = std::filesystem::canonical(argv[2]);
    require(dll.filename() == L"pluto-scr.dll", "Expected pluto-scr.dll");
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_CREATE_THREAD |
      PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid)};
    require(process.value != nullptr, "Cannot open test game process");
    std::array<wchar_t, 32768> exe{}; DWORD size = static_cast<DWORD>(exe.size());
    require(QueryFullProcessImageNameW(process.value, 0, exe.data(), &size), "Cannot identify target");
    require(sha256(exe.data()) == "32dbbdd001dd381cb1b3a719b7ad1fc918a9d4bc99661c675e00254efecca827",
      "Unsupported executable: expected verified Remastered 1.23.10.13515 x86");
    require(remote_module(pid, dll.filename().c_str()) == 0, "Bridge is already loaded; restart the test game before replacing it");
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto load = GetProcAddress(kernel, "LoadLibraryW");
    HMODULE owner = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(load), &owner), "Cannot locate LoadLibraryW module");
    wchar_t owner_path[MAX_PATH]{}; GetModuleFileNameW(owner, owner_path, MAX_PATH);
    uintptr_t remote_owner = remote_module(pid, std::filesystem::path(owner_path).filename().c_str());
    require(remote_owner != 0, "Loader module missing from target");
    auto remote_load = remote_owner + reinterpret_cast<uintptr_t>(load) - reinterpret_cast<uintptr_t>(owner);
    const auto path = dll.wstring(); const size_t bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process.value, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    require(remote_path != nullptr, "Cannot allocate DLL path");
    SIZE_T written = 0;
    if (!WriteProcessMemory(process.value, remote_path, path.c_str(), bytes, &written) || written != bytes) {
      VirtualFreeEx(process.value, remote_path, 0, MEM_RELEASE); require(false, "Cannot write DLL path");
    }
    Handle thread{CreateRemoteThread(process.value, nullptr, 0,
      reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_load), remote_path, 0, nullptr)};
    if (!thread.value) { VirtualFreeEx(process.value, remote_path, 0, MEM_RELEASE); require(false, "Cannot load bridge"); }
    DWORD wait = WaitForSingleObject(thread.value, 15000);
    // Keep the path alive if the loader is still using it after a timeout.
    require(wait == WAIT_OBJECT_0, "DLL load did not finish within 15 seconds");
    DWORD loaded = 0; require(GetExitCodeThread(thread.value, &loaded), "Cannot read DLL load result");
    VirtualFreeEx(process.value, remote_path, 0, MEM_RELEASE);
    require(loaded != 0, "LoadLibraryW rejected the bridge DLL");
    std::printf("{\"pid\":%lu,\"bridge_loaded\":true,\"check_log_for_initialization\":true}\n", pid);
    return 0;
  } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
