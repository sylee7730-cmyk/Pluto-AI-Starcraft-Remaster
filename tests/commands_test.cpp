#include "commands.h"
#include <cstdio>
#include <stdexcept>
#include <string>
static void require(bool condition){if(!condition)throw std::runtime_error("Command test failed");}
int main(){
 try {
  const uint8_t input[]={9,1,6,8,0x14,0xa0,0x0f,0x30,9,0x34,8,0xb0,0,0,0x15,1,0,2,0,0,0,0xe4,0,6,0,0x29,6,8};
  auto map=[](uint16_t id){return id==0x806?0x12345u:id==0x834?0x23456u:0u;};
  auto packets=translate_commands(input,sizeof(input),map);
  require(packets.size()==4);
  require(packets[0]==Packet({0x63,1,0x45,0x23,1,0}));
  require(packets[1]==Packet({0x60,0xa0,0x0f,0x30,9,0x56,0x34,2,0,0xb0,0,0}));
  require(packets[2]==Packet({0x61,1,0,2,0,0,0,0,0,0xe4,0,6,0}));
  require(packets[3]==Packet({0x62,0x45,0x23,1,0}));
  // Selection changes must retain sequence and expand every handle, including
  // generation bits outside the original 16-bit representation.
  const uint8_t selection[]={10,2,6,8,0x34,8,11,1,6,8};
  auto changes=translate_commands(selection,sizeof(selection),map);
  require(changes==std::vector<Packet>({{0x64,2,0x45,0x23,1,0,0x56,0x34,2,0},{0x65,1,0x45,0x23,1,0}}));
  // Fixed-format commands retain their wire bytes: building placement, unit
  // training, research, upgrade, siege, stim and cancellation in one buffer.
  const std::vector<Packet> fixed={{0x0c,0x1e,10,0,20,0,111,0},{0x1f,0,0},{0x30,0},
    {0x32,7},{0x26,0},{0x36},{0x18},{0x20,3,0},{0x2f,100,0,200,0}};
  Packet combined;for(const auto& packet:fixed)combined.insert(combined.end(),packet.begin(),packet.end());
  require(translate_commands(combined.data(),combined.size(),map)==fixed);
  require(translate_commands(nullptr,0,map).empty());
  for(Packet bad:{Packet{9,13},Packet{9,1,6},Packet{9,1,7,8},Packet{0x14,1,2},Packet{0xff}}){bool rejected=false;try{translate_commands(bad.data(),bad.size(),map);}catch(const std::runtime_error&){rejected=true;}require(rejected);}
  std::puts("selection, right-click, targeted-order, unload, fixed commands and invalid-buffer checks passed");
  // "Allies as own forces": allied handles are never selected and selection-bound
  // orders are dropped while the requested selection was uncontrollable.
  auto handles=[](uint16_t id){return id==0x806?0x12345u:id==0x834?0x23456u:id==0x899?0x34567u:0u;};
  const auto is_ally=[](uint16_t id){return id==0x834 || id==0x899;};
  const Packet stop={0x1a,0},own_only={9,1,6,8},ally_only={9,1,0x34,8};
  auto join=[](std::initializer_list<Packet> list){Packet all;for(const auto& item:list)all.insert(all.end(),item.begin(),item.end());return all;};
  { // Mixed selection keeps only controllable handles; the order that follows is kept.
    CommandFilter filter;filter.foreign=is_ally;
    const auto input_mixed=join({Packet{9,2,6,8,0x34,8},stop});
    require(translate_commands(input_mixed.data(),input_mixed.size(),handles,&filter)==std::vector<Packet>({{0x63,1,0x45,0x23,1,0},stop}));
    require(!filter.selection_blocked);
  }
  { // A selection of allies only blocks later orders (right-click, stop, build) until a valid selection.
    CommandFilter filter;filter.foreign=is_ally;
    const Packet right_click={0x14,0xa0,0x0f,0x30,9,0,0,0xb0,0,0},build={0x0c,0x1e,10,0,20,0,111,0};
    const auto input_blocked=join({ally_only,stop,right_click,build});
    require(translate_commands(input_blocked.data(),input_blocked.size(),handles,&filter).empty());
    require(filter.selection_blocked);
    // The block persists across separate calls, like the game's selection does.
    require(translate_commands(stop.data(),stop.size(),handles,&filter).empty());
    // Adding controllable units to a blocked selection must rebuild the selection from scratch.
    const auto input_add=join({Packet{10,1,6,8},stop});
    require(translate_commands(input_add.data(),input_add.size(),handles,&filter)==std::vector<Packet>({{0x63,1,0x45,0x23,1,0},stop}));
    require(!filter.selection_blocked);
    // A new replace-selection clears the block as well.
    translate_commands(ally_only.data(),ally_only.size(),handles,&filter);require(filter.selection_blocked);
    const auto input_replace=join({own_only,stop});
    require(translate_commands(input_replace.data(),input_replace.size(),handles,&filter)==std::vector<Packet>({{0x63,1,0x45,0x23,1,0},stop}));
    require(!filter.selection_blocked);
  }
  { // Shift-add / deselect of allies only are dropped without disturbing the current selection.
    CommandFilter filter;filter.foreign=is_ally;
    const auto input_noop=join({Packet{10,1,0x34,8},Packet{11,2,0x34,8,0x99,8},stop});
    require(translate_commands(input_noop.data(),input_noop.size(),handles,&filter)==std::vector<Packet>({stop}));
    require(!filter.selection_blocked);
    const auto input_deselect=join({Packet{11,2,6,8,0x34,8}});
    require(translate_commands(input_deselect.data(),input_deselect.size(),handles,&filter)==std::vector<Packet>({{0x65,1,0x45,0x23,1,0}}));
  }
  { // Targeting an ally with an order stays legal (heal, repair, follow); an empty selection is unchanged.
    CommandFilter filter;filter.foreign=is_ally;
    const Packet to_ally={0x14,0xa0,0x0f,0x30,9,0x34,8,0xb0,0,0};
    const auto input_target=join({own_only,to_ally,Packet{9,0}});
    const auto output_target=translate_commands(input_target.data(),input_target.size(),handles,&filter);
    require(output_target.size()==3 && output_target[1][0]==0x60 && output_target[2]==Packet({0x63,0}));
    require(!filter.selection_blocked);
  }
  { // Without a filter (or without a predicate) behaviour is identical to before.
    const auto input_plain=join({Packet{9,2,6,8,0x34,8},stop});
    require(translate_commands(input_plain.data(),input_plain.size(),handles)==translate_commands(input_plain.data(),input_plain.size(),handles,nullptr));
    CommandFilter empty;require(translate_commands(input_plain.data(),input_plain.size(),handles,&empty).size()==2);
    // Unknown handles are still rejected, even when filtering.
    CommandFilter filter;filter.foreign=is_ally;
    bool rejected=false;try{const Packet unknown={9,1,0x77,8};translate_commands(unknown.data(),unknown.size(),handles,&filter);}catch(const std::runtime_error&){rejected=true;}
    require(rejected);
  }
  std::puts("allied-unit command filter: mixed/only-allied selections, persistence, rebuild, no-op shift/deselect and unchanged defaults passed");
  return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
