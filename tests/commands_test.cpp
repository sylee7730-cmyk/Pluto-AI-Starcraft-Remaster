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
  std::puts("selection, right-click, targeted-order, unload, fixed commands and invalid-buffer checks passed");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
