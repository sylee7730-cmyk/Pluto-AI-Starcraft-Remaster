# Dependencies and provenance

## BWAPI 4.4.0

Source: https://github.com/bwapi/bwapi

Commit: `7687da8abc4726f8366401f11ab648d421385793`

The client, shared data definitions, type library, and public headers used by the bridge are included under `vendor/bwapi/`. The LGPL v3 license is retained there. The local source includes a forward declaration adjustment in `CommandTemp.h` for the newer MSVC compiler and generated version headers. The game injection and 1.16.1 code patching portions of BWAPI are not used.

The incorporated GPL v3 text is provided as `vendor/bwapi/COPYING.GPL-3.0.txt`. This project includes the bridge source and CMake build inputs so that the combined DLL can be rebuilt against a modified BWAPI client library; use `scripts/build.ps1` and retain these sources alongside the binaries.

## MinHook

Source: https://github.com/TsudaKageyu/minhook

Commit: `8af6b4acae5a9388fd742b56fa79ece89d96f823`

The x86 hook implementation is included under `vendor/minhook/`, with its license and third-party notices. It is used for the Win32 GetTickCount hook.

## Analysis references

The StarCraft profile and structure offsets were checked against the installed executable using the upstream `samase_scarf` analyzer and the `bw_dat` structure definitions from https://github.com/neivv/aise. ShieldBattery's https://github.com/ShieldBattery/ShieldBattery source was consulted to verify command formats and function signatures. These tools and the game executable are not redistributed in this project.

## Pluto

Source/release page: https://github.com/tscmoo/pluto

README revision examined: `d41f473aeebafcfc90e28527b57324656c0ffca1`

The original bot DLL, inference executable and model weights are external inputs. This project contains neither their source nor a modified on-disk copy. The runtime applies validated operand bindings to the loaded DLL only.

The required SHA-256 values are recorded in `src/file_hash.h` and `scripts/start-pluto.ps1`. Replacing any of the upstream inputs requires a new compatibility analysis and validation.
