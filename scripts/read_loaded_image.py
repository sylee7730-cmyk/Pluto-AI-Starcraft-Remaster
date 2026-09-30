"""Read only the selected StarCraft executable's mapped PE sections for local analysis.

No process writes, injection, or heap dump. Requires Windows and pefile.
The output is analysis input, not a runnable or redistributable game executable.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import pathlib

import pefile


class ModuleEntry(c.Structure):
    _fields_ = [("size", w.DWORD), ("id", w.DWORD), ("pid", w.DWORD),
                ("global_use", w.DWORD), ("process_use", w.DWORD),
                ("base", c.c_void_p), ("image_size", w.DWORD),
                ("module", c.c_void_p), ("name", w.WCHAR * 256),
                ("path", w.WCHAR * 260)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--exe", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    expected = args.exe.resolve(strict=True)
    if expected.name.lower() != "starcraft.exe":
        parser.error("Expected a StarCraft.exe image")
    if args.out.resolve() == expected:
        parser.error("Output must not overwrite the installed executable")
    k = c.WinDLL("kernel32", use_last_error=True)
    k.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    k.CreateToolhelp32Snapshot.restype = w.HANDLE
    k.Module32FirstW.argtypes = [w.HANDLE, c.POINTER(ModuleEntry)]
    k.Module32NextW.argtypes = [w.HANDLE, c.POINTER(ModuleEntry)]
    k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    k.OpenProcess.restype = w.HANDLE
    k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    k.CloseHandle.argtypes = [w.HANDLE]
    snapshot = k.CreateToolhelp32Snapshot(0x08 | 0x10, args.pid)
    if snapshot == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    try:
        entry = ModuleEntry()
        entry.size = c.sizeof(entry)
        found = k.Module32FirstW(snapshot, c.byref(entry))
        base = None
        while found:
            if pathlib.Path(entry.path).resolve() == expected:
                base, image_size = entry.base, entry.image_size
                break
            found = k.Module32NextW(snapshot, c.byref(entry))
        if base is None:
            raise RuntimeError("PID does not contain the exact requested executable")
    finally:
        k.CloseHandle(snapshot)
    process = k.OpenProcess(0x0010 | 0x0400, False, args.pid)
    if not process:
        raise c.WinError(c.get_last_error())
    try:
        def read(address, size):
            buf, count = c.create_string_buffer(size), c.c_size_t()
            if not k.ReadProcessMemory(process, address, buf, size, c.byref(count)) or count.value != size:
                raise c.WinError(c.get_last_error())
            return buf.raw

        original = expected.read_bytes()
        pe = pefile.PE(data=original)
        pe.OPTIONAL_HEADER.ImageBase = base
        output = bytearray(pe.write())
        sections = []
        for section in pe.sections:
            name = section.Name.rstrip(b"\0").decode("ascii")
            if name not in (".text", ".rdata", ".data", ".rodata", "_RDATA"):
                continue
            size = min(section.SizeOfRawData, section.Misc_VirtualSize)
            if section.VirtualAddress + size > image_size:
                raise RuntimeError("Section exceeds loaded image")
            raw = read(base + section.VirtualAddress, size)
            offset = section.PointerToRawData
            output[offset:offset + size] = raw
            sections.append({"name": name, "size": size, "sha256": hashlib.sha256(raw).hexdigest()})
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open("xb") as target:
            target.write(output)
        print(json.dumps({"pid": args.pid, "exe": str(expected), "base": hex(base),
            "disk_sha256": hashlib.sha256(original).hexdigest(), "sections": sections,
            "out": str(args.out), "process_modified": False}, indent=2))
    finally:
        k.CloseHandle(process)


if __name__ == "__main__":
    main()
