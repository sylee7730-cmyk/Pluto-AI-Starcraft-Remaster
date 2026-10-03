#pragma once
#include <cstdint>
#include <cstring>
#include "scr_layout.h"
#include "session.h"

// Team strength experiment: Pluto keeps seeing only its own units, but the
// per-player statistics it reads report the whole team. Adds each ally's
// per-unit-type counts (all, completed, kills) to Pluto's own slot in a copy of
// the 1.16.1 game structure. Supplies and resources are deliberately left alone:
// inflating them would make the model build what it cannot afford or house.
inline void aggregate_team_stats(uint8_t* game,unsigned self,const AllyMask& allies) {
  if(self>=8)return;
  const auto add_table=[&](size_t base) {
    for(unsigned type=0;type<228;++type) {
      uint32_t total;std::memcpy(&total,game+base+(type*12+self)*4,4);
      for(unsigned owner=0;owner<8;++owner) {
        if(!allies.hides(owner))continue;
        uint32_t value;std::memcpy(&value,game+base+(type*12+owner)*4,4);total+=value;
      }
      std::memcpy(game+base+(type*12+self)*4,&total,4);
    }
  };
  add_table(layout::Game::all_units_count);
  add_table(layout::Game::completed_units_count);
  add_table(layout::Game::unit_kills);
}
