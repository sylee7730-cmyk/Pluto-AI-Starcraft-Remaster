#pragma once
#include <string>

// The public Pluto releases this bridge is built for. Pluto reads StarCraft 1.16.1's memory at fixed addresses, and
// the bridge rewrites those addresses inside pluto.dll (see pluto_bindings.h), so only binaries it has been
// generated for are accepted. The launcher and scripts/start-pluto.ps1 keep the same list of hashes.
inline constexpr char pluto_sha256[]="7e360b643c8c0156c03fe0cad9972a3058138ccfe22f921c5b4e0cd0aaf0abef";  // cog2026-2578600
// The same DLL with its resignation disabled: a 4-byte patch (the "frames below the
// win threshold" comparison at RVA 0xb155 becomes `cmp edx,0x7fffffff`, which is
// never exceeded). Applied locally by the launcher; everything else is identical.
inline constexpr char pluto_noresign_sha256[]="ed19fff2ff212fcbf8e7b286aebb825754ce8d747cda45e03a75d70f146e0224";
inline constexpr char pluto_v2_0_sha256[]="603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc";  // v2.0

struct PlutoDll {
  const char* sha256;
  const char* release;       // must name a table in pluto_binding_sets
  bool no_resign_patch;      // carries the launcher's binary patch
  bool honors_resign_env;    // reads BWRL_RESIGN_THRESHOLD (v2.0 and later)
};
inline constexpr PlutoDll pluto_dlls[]={
  {pluto_sha256,"cog2026-2578600",false,false},
  {pluto_noresign_sha256,"cog2026-2578600",true,false},
  {pluto_v2_0_sha256,"v2.0",false,true},
};
// sha256: lowercase hex, exactly as file_sha256() prints it.
inline const PlutoDll* find_pluto_dll(const std::string& sha256) {
  for(const auto& dll:pluto_dlls)if(sha256==dll.sha256)return &dll;
  return nullptr;
}
