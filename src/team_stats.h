#pragma once
#include <cstdint>
#include <cstring>
#include <cwchar>
#include "scr_layout.h"
#include "session.h"

// Team strength experiment: Pluto keeps seeing only its own units, but the
// per-player statistics it reads report (part of) the whole team.
//   army  - allies' combat units are added to Pluto's unit counts, plus all kills.
//           Buildings, workers, larvae/eggs and child units (mines, interceptors,
//           scarabs) are excluded so Pluto does not believe it already owns its
//           allies' bases and economy. (Counting everything made it over-expand.)
//   kills - only kills are added.
//   all   - every unit type is added (the first, over-confident variant).
// Supplies and resources are always left alone: inflating them would make the
// model build what it cannot afford or house.
enum class TeamStatsMode { off, army, kills, all };

inline bool team_stats_counts_as_army(unsigned type) {
  if(type>=106)return false;                       // buildings, special buildings, neutral
  switch(type) {
    case 7: case 41: case 64:                        // SCV, Drone, Probe
    case 35: case 36: case 59: case 97:              // Larva, Egg, Cocoon, Lurker Egg
    case 13: case 73: case 85:                       // Spider Mine, Interceptor, Scarab
      return false;
    default: return true;
  }
}

inline bool team_stats_adds_count(TeamStatsMode mode,unsigned type) {
  return mode==TeamStatsMode::all || (mode==TeamStatsMode::army && team_stats_counts_as_army(type));
}
inline bool team_stats_adds_kills(TeamStatsMode mode) {return mode!=TeamStatsMode::off;}

inline TeamStatsMode parse_team_stats_mode(const wchar_t* text) {
  if(!text)return TeamStatsMode::off;
  if(std::wcscmp(text,L"army")==0 || std::wcscmp(text,L"1")==0)return TeamStatsMode::army;
  if(std::wcscmp(text,L"kills")==0)return TeamStatsMode::kills;
  if(std::wcscmp(text,L"all")==0)return TeamStatsMode::all;
  return TeamStatsMode::off;
}
inline const char* team_stats_mode_name(TeamStatsMode mode) {
  switch(mode){case TeamStatsMode::army:return "army";case TeamStatsMode::kills:return "kills";case TeamStatsMode::all:return "all";default:return "off";}
}

// Applies the aggregation to a copy of the 1.16.1 game structure.
inline void aggregate_team_stats(uint8_t* game,unsigned self,const AllyMask& allies,TeamStatsMode mode=TeamStatsMode::all) {
  if(self>=8 || mode==TeamStatsMode::off)return;
  const auto add_table=[&](size_t base,bool count_table) {
    for(unsigned type=0;type<228;++type) {
      if(count_table && !team_stats_adds_count(mode,type))continue;
      uint32_t total;std::memcpy(&total,game+base+(type*12+self)*4,4);
      for(unsigned owner=0;owner<8;++owner) {
        if(!allies.hides(owner))continue;
        uint32_t value;std::memcpy(&value,game+base+(type*12+owner)*4,4);total+=value;
      }
      std::memcpy(game+base+(type*12+self)*4,&total,4);
    }
  };
  add_table(layout::Game::all_units_count,true);
  add_table(layout::Game::completed_units_count,true);
  if(team_stats_adds_kills(mode))add_table(layout::Game::unit_kills,false);
}
