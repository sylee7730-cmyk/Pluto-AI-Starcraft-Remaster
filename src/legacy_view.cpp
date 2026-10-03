#include "legacy_view.h"
#include "pluto_bindings.h"
#include "scr_profile_13515_x86.h"
#include "scr_layout.h"
#include "commands.h"
#include "session_reader.h"
#include "unit_list.h"
#include "team_stats.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace {
template<class T> void put(uintptr_t p,T value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));}
thread_local LegacyView* current_view=nullptr;
}
LegacyView::LegacyView():memory(last-first){}
void LegacyView::reset() {
  std::fill(memory.begin(),memory.end(),uint8_t{0});slots={};sprites={};lookup.clear();
  raw_start=raw_length=0;flush_error.clear();sent=0;pending_turns.clear();
}
void LegacyView::begin_frame(FILE* log,int frame) {
  current_view=this;frame_log=log;frame_number=frame;sent=0;
  const auto send=reinterpret_cast<void (__cdecl*)(const uint8_t*,size_t)>(scr::send_command());
  for(auto turn=pending_turns.begin();turn!=pending_turns.end();) {
    if(turn->frame>=frame){++turn;continue;}
    for(const auto& packet:turn->packets) {
      send(packet.data(),packet.size());++sent;
      std::fprintf(log,"{\"command_frame\":%d,\"queued_frame\":%d,\"packet_hex\":\"",frame,turn->frame);
      for(auto b:packet)std::fprintf(log,"%02x",b);
      std::fprintf(log,"\",\"submitted\":true}\n");
    }
    turn=pending_turns.erase(turn);
  }
}
void __cdecl LegacyView::flush_turn() noexcept {
  if(!current_view)return;
  try{current_view->drain(current_view->frame_log,current_view->frame_number);}
  catch(const std::exception& e){
    current_view->flush_error=e.what();
    put<uint32_t>(current_view->address(0x654aa0),0);
  }
}
uint32_t LegacyView::address(uint32_t old) const {
  if(old<first || old>=last)throw std::runtime_error("Out of range legacy address");
  return reinterpret_cast<uint32_t>(memory.data())+old-first;
}
// True when a legacy handle names a unit Pluto believes it owns but the game would
// not let it command (an ally's unit shown as Pluto's own).
bool LegacyView::is_foreign_handle(uint16_t id) const {
  const unsigned index=id&0x7ff;if(!index || index>slots.size())return false;
  const auto& slot=slots[index-1];if(!slot.raw || slot.generation!=(id>>11))return false;
  return own_owners.hides(scr::read<uint8_t>(slot.raw+layout::Unit::player));
}
bool LegacyView::is_hidden(uint32_t raw) const {
  return raw && hidden_owners.hides(scr::read<uint8_t>(raw+layout::Unit::player));
}
// Follows a unit list link (prev=0, next=4) past units hidden from Pluto, so the
// copied list stays contiguous. Without hidden owners this returns raw unchanged.
uint32_t LegacyView::skip_hidden(uint32_t raw,unsigned link_offset) const {
  return skip_hidden_links(raw,raw_length,
    [this](uint32_t unit_raw){return is_hidden(unit_raw);},
    [link_offset](uint32_t unit_raw){return scr::read<uint32_t>(unit_raw+link_offset);});
}
uint32_t LegacyView::unit(uint32_t raw) const {
  auto found=lookup.find(raw);
  return found==lookup.end()?0:address(unit_base+found->second*336);
}
uint16_t LegacyView::handle_for_raw(uint32_t raw) const {
  auto found=lookup.find(raw);if(found==lookup.end())return 0;
  auto index=found->second;return static_cast<uint16_t>((index+1)|(slots[index].generation<<11));
}
uint32_t LegacyView::scr_handle(uint16_t id) const {
  unsigned index=id&0x7ff;if(!index || index>slots.size())return 0;--index;
  auto& slot=slots[index];if(!slot.raw || slot.generation!=(id>>11))return 0;
  return (slot.raw-raw_start)/336+1 | (uint32_t(slot.source_generation)<<(raw_length>1700?13:11));
}
void LegacyView::sprite(uint32_t dest,uint32_t raw) {
  std::memcpy(reinterpret_cast<void*>(dest),reinterpret_cast<void*>(raw),20);
  put<uint32_t>(dest,0);put<uint32_t>(dest+4,0);
  put<uint16_t>(dest+20,static_cast<uint16_t>(scr::read<uint32_t>(raw+20)));
  put<uint16_t>(dest+22,static_cast<uint16_t>(scr::read<uint32_t>(raw+24)));
  std::memcpy(reinterpret_cast<void*>(dest+24),reinterpret_cast<void*>(raw+28),12);
}
void LegacyView::update() {
  raw_start=scr::units();raw_length=scr::read<uint32_t>(scr::addr(0x10436f4));
  if(!raw_start || !raw_length || raw_length>8192)throw std::runtime_error("Invalid SCR unit vector");
  const AllyMask ally_mask=(hide_allies || ally_as_own) && scr::game()?make_ally_mask(read_session(scr::game())):AllyMask{};
  hidden_owners=hide_allies?ally_mask:AllyMask{};
  own_owners=ally_as_own?ally_mask:AllyMask{};
  std::unordered_set<uint32_t> present;
  for(unsigned i=0;i<raw_length;++i) {
    auto raw=raw_start+i*336;
    if(scr::read<uint32_t>(raw+12) && scr::read<uint16_t>(raw+100)<228 && !is_hidden(raw))present.insert(raw);
  }
  if(present.size()>slots.size())throw std::runtime_error("Pluto's legacy unit view exceeded 1700 simultaneous units");
  for(unsigned i=0;i<slots.size();++i)if(slots[i].raw && !present.count(slots[i].raw)) {
    lookup.erase(slots[i].raw);slots[i].raw=0;
    std::memset(reinterpret_cast<void*>(address(unit_base+i*336)),0,336);
  }
  for(auto raw:present) {
    const auto gen=scr::read<uint8_t>(raw+165);auto found=lookup.find(raw);
    if(found==lookup.end()) {
      auto empty=std::find_if(slots.begin(),slots.end(),[](auto& s){return s.raw==0;});
      auto index=static_cast<unsigned>(empty-slots.begin());
      empty->raw=raw;empty->source_generation=gen;empty->generation=(empty->generation+1)&31;lookup.emplace(raw,index);
    }else if(slots[found->second].source_generation!=gen) {
      auto& slot=slots[found->second];slot.source_generation=gen;slot.generation=(slot.generation+1)&31;
    }
  }
  std::memcpy(reinterpret_cast<void*>(address(0x57f0f0)),reinterpret_cast<void*>(scr::game()),layout::Game::size);
  if(team_stats && scr::game())
    aggregate_team_stats(reinterpret_cast<uint8_t*>(address(0x57f0f0)),scr::local_player_id(),make_ally_mask(read_session(scr::game())));
  std::memcpy(reinterpret_cast<void*>(address(0x57eee0)),reinterpret_cast<void*>(scr::players()),12*36);
  put<uint32_t>(address(0x512688),scr::local_player_id());
  put<uint32_t>(address(0x59688c),scr::is_multiplayer()?1u:0u);put<uint16_t>(address(0x596904),2);
  put<uint32_t>(address(0x628430),unit(skip_hidden(scr::first_active_unit(),4)));
  put<uint32_t>(address(0x6283ec),unit(skip_hidden(scr::first_hidden_unit(),4)));
  put<uint32_t>(address(0x6283f4),unit(skip_hidden(scr::read<uint32_t>(scr::addr(0x10436b8)),4)));
  put<uint32_t>(address(0x6d1260),scr::map_tile_flags());
  for(unsigned i=0;i<slots.size();++i)if(slots[i].raw) {
    const auto raw=slots[i].raw,dest=address(unit_base+i*336);
    std::memcpy(reinterpret_cast<void*>(dest),reinterpret_cast<void*>(raw),336);
    if(own_owners.hides(scr::read<uint8_t>(raw+layout::Unit::player))) {  // Allies shown as Pluto's own forces.
      put<uint8_t>(dest+layout::Unit::player,static_cast<uint8_t>(scr::local_player_id()));
      // Optional experiment: a unit in stasis is one the model knows it cannot command.
      if(ally_stasis && scr::read<uint8_t>(raw+layout::Unit::stasis_timer)==0)put<uint8_t>(dest+layout::Unit::stasis_timer,1);
    }
    put<uint32_t>(dest,unit(skip_hidden(scr::read<uint32_t>(raw),0)));
    put<uint32_t>(dest+4,unit(skip_hidden(scr::read<uint32_t>(raw+4),4)));
    for(unsigned offset:{20,92,104,108,112,124,128,236,240,244,252,284})put<uint32_t>(dest+offset,unit(scr::read<uint32_t>(raw+offset)));
    // Unit-specific unions contain both pointers and scalar values. Convert a
    // field only when it points exactly at a currently allocated unit slot.
    for(unsigned offset=192;offset<=216;offset+=4) {
      auto value=scr::read<uint32_t>(raw+offset);if(lookup.count(value))put<uint32_t>(dest+offset,unit(value));
    }
    auto raw_sprite=scr::read<uint32_t>(raw+12);sprite(reinterpret_cast<uint32_t>(sprites[i].data()),raw_sprite);
    put<uint32_t>(dest+12,reinterpret_cast<uint32_t>(sprites[i].data()));put<uint8_t>(dest+165,slots[i].generation);
    const auto high=raw_length>1700?scr::read<uint16_t>(raw+306):0;
    for(unsigned n=0;n<8;++n) {
      const uint32_t id=scr::read<uint16_t>(raw+176+n*2)|(((high>>(n*2))&3)<<16);
      const uint32_t index=id&(raw_length>1700?0x1fff:0x7ff);
      uint16_t legacy=0;
      if(index && index<=raw_length) {
        auto loaded=raw_start+(index-1)*336;
        if(scr::read<uint8_t>(loaded+165)==(id>>(raw_length>1700?13:11)))legacy=handle_for_raw(loaded);
      }
      put<uint16_t>(dest+176+n*2,legacy);
    }
  }
  std::vector<uint32_t> raw_bullets;
  std::unordered_set<uint32_t> visited_bullets;
  for(auto bullet=scr::read<uint32_t>(scr::addr(0x1034a78));bullet;bullet=scr::read<uint32_t>(bullet+4)) {
    if(raw_bullets.size()>=8192 || !visited_bullets.insert(bullet).second)throw std::runtime_error("Invalid SCR bullet list");
    raw_bullets.push_back(bullet);
  }
  bullets.resize(raw_bullets.size());bullet_sprites.resize(raw_bullets.size());
  const auto bullet_address=[this](size_t index){return reinterpret_cast<uint32_t>(bullets[index].data());};
  put<uint32_t>(address(0x64dec4),bullets.empty()?0:bullet_address(0));
  for(size_t count=0;count<raw_bullets.size();++count) {
    const auto bullet=raw_bullets[count],dest=bullet_address(count);
    std::memcpy(reinterpret_cast<void*>(dest),reinterpret_cast<void*>(bullet),112);
    put<uint32_t>(dest,count?bullet_address(count-1):0);
    put<uint32_t>(dest+4,count+1<bullets.size()?bullet_address(count+1):0);
    for(unsigned offset:{20,92,100,104})put<uint32_t>(dest+offset,unit(scr::read<uint32_t>(bullet+offset)));
    auto raw_sprite=scr::read<uint32_t>(bullet+12);
    if(raw_sprite){
      const auto sprite_address=reinterpret_cast<uint32_t>(bullet_sprites[count].data());
      sprite(sprite_address,raw_sprite);put<uint32_t>(dest+12,sprite_address);
    }
  }
}
void LegacyView::bind(HMODULE pluto) {
  const auto base=reinterpret_cast<uint32_t>(pluto);
  for(auto& binding:pluto_bindings)if(scr::read<uint32_t>(base+binding.rva)!=binding.expected)throw std::runtime_error("Pluto binding validation failed; unsupported binary");
  for(auto& binding:pluto_bindings) {
    const int32_t signed_address=static_cast<int32_t>(binding.expected);
    uint32_t value;
    if(binding.expected==0x485a40)value=reinterpret_cast<uint32_t>(&LegacyView::flush_turn);
    else if(signed_address<0)value=0u-address(static_cast<uint32_t>(-signed_address));
    else value=address(binding.expected);
    DWORD old;
    auto at=reinterpret_cast<void*>(base+binding.rva);
    if(!VirtualProtect(at,4,PAGE_EXECUTE_READWRITE,&old))throw std::runtime_error("Cannot bind Pluto binary");
    put<uint32_t>(base+binding.rva,value);
    DWORD unused;VirtualProtect(at,4,old,&unused);
  }
  FlushInstructionCache(GetCurrentProcess(),nullptr,0);
}
unsigned LegacyView::drain(FILE*,int frame) {
  if(!flush_error.empty())throw std::runtime_error(flush_error);
  const auto size=scr::read<uint32_t>(address(0x654aa0));
  if(size>512)throw std::runtime_error("Legacy command queue overflow");
  if(size){
    if(ally_as_own && !command_filter.foreign)command_filter.foreign=[this](uint16_t id){return is_foreign_handle(id);};
    auto packets=translate_commands(reinterpret_cast<const uint8_t*>(address(0x654880)),size,[this](uint16_t id){return scr_handle(id);},
      ally_as_own?&command_filter:nullptr);
    // SCR's offline turn queue applies commands one frame earlier than Pluto's
    // 1.16.1 training environment. Retain one frame to preserve the measured
    // four-frame observation-to-effect delay, including selection ordering.
    pending_turns.push_back({frame,std::move(packets)});
  }
  put<uint32_t>(address(0x654aa0),0);
  return sent;
}
