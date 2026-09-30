#include "commands.h"
#include <stdexcept>
#include <string>
namespace {
uint16_t word(const uint8_t* p){return p[0]|uint16_t(p[1])<<8;}
void dword(Packet& packet,uint32_t v){for(unsigned i=0;i<4;++i)packet.push_back(static_cast<uint8_t>(v>>(8*i)));}
unsigned fixed_length(uint8_t id) {
  switch(id) {
    case 0x0c:return 8;
    case 0x14:return 10;case 0x15:return 11;
    case 0x18:case 0x19:case 0x1b:case 0x1c:case 0x27:case 0x2a:case 0x2e:case 0x31:case 0x33:case 0x34:case 0x36:case 0x5a:return 1;
    case 0x1a:case 0x1e:case 0x21:case 0x22:case 0x25:case 0x26:case 0x28:case 0x2b:case 0x2c:case 0x2d:case 0x30:case 0x32:return 2;
    case 0x1f:case 0x20:case 0x23:case 0x29:case 0x35:return 3;
    case 0x2f:return 5;
    default:throw std::runtime_error("Unsupported legacy gameplay command: "+std::to_string(id));
  }
}
}
std::vector<Packet> translate_commands(const uint8_t* bytes,size_t length,const std::function<uint32_t(uint16_t)>& unit) {
  std::vector<Packet> output;
  auto convert=[&](uint16_t id){if(!id)return 0u;auto result=unit(id);if(!result)throw std::runtime_error("Command references an expired unit handle");return result;};
  for(size_t offset=0;offset<length;) {
    auto p=bytes+offset;auto id=p[0];size_t size;
    if(id>=9 && id<=11){if(length-offset<2 || p[1]>12)throw std::runtime_error("Invalid legacy selection");size=2+p[1]*2;}
    else size=fixed_length(id);
    if(size>length-offset)throw std::runtime_error("Truncated legacy command");
    Packet result;
    if(id>=9 && id<=11) {
      result={static_cast<uint8_t>(0x63+id-9),p[1]};
      for(unsigned i=0;i<p[1];++i){auto old=word(p+2+i*2);if(!old)throw std::runtime_error("Null selection handle");dword(result,convert(old));}
    }else if(id==0x14 || id==0x15) {
      result={static_cast<uint8_t>(id==0x14?0x60:0x61)};
      result.insert(result.end(),p+1,p+5);dword(result,convert(word(p+5)));result.insert(result.end(),p+7,p+size);
    }else if(id==0x29){result={0x62};dword(result,convert(word(p+1)));}
    else result.assign(p,p+size);
    output.push_back(std::move(result));offset+=size;
  }
  return output;
}
