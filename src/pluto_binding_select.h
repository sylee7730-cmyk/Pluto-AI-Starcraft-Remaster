#pragma once
#include "pluto_bindings.h"
#include <cstdint>

// Picks the binding table whose every entry matches the loaded DLL, or nullptr when none does. A table is only
// considered if all of its RVAs lie inside the image, so a smaller binary can never be read out of bounds.
// read(rva) returns the 32-bit word at that offset of the loaded image.
template<class Read> const PlutoBindingSet* select_binding_set(Read read,uint32_t image_size) {
  for(const auto& set:pluto_binding_sets) {
    bool matches=set.count>0;
    for(size_t i=0;i<set.count && matches;++i) {
      const auto& binding=set.bindings[i];
      matches=binding.rva<=image_size && image_size-binding.rva>=4 && read(binding.rva)==binding.expected;
    }
    if(matches)return &set;
  }
  return nullptr;
}
