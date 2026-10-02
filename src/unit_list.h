#pragma once
#include <cstdint>

// Walks a unit list link (prev or next) past entries that must stay hidden from
// Pluto and returns the first visible entry, or 0 at the end of the list.
// `limit` bounds the walk so a corrupt, cyclic list cannot hang the game thread.
template<class Hidden,class Step>
inline uint32_t skip_hidden_links(uint32_t raw,unsigned limit,Hidden hidden,Step step) {
  for(unsigned guard=0;raw && guard<=limit;++guard) {
    if(!hidden(raw))return raw;
    raw=step(raw);
  }
  return 0;
}
