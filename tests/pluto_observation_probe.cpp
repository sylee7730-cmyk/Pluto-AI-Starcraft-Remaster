// Optional pinned-binary probe. No game process, inference engine, or network is
// involved. All legacy memory is an isolated fixture owned by this executable.
#include "file_hash.h"
#include "pluto_bindings.h"
#include <cstring>
#include <cstdio>
#include <vector>

template<class T> void put(uint32_t p,T value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));}
template<class T> T get(uint32_t p){T value;std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return value;}
static void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
int probe(const wchar_t* path) {
  require(file_sha256(path)==pluto_sha256,"Unsupported Pluto binary");
  auto module=LoadLibraryExW(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
  require(module!=nullptr,"Cannot load Pluto");
  const auto base=reinterpret_cast<uint32_t>(module);
  std::vector<uint8_t> memory(0x6d2000-0x512000);
  const auto address=[&](uint32_t old){require(old>=0x512000 && old<0x6d2000,"Fixture address outside arena");
    return reinterpret_cast<uint32_t>(memory.data())+old-0x512000;};
  for(const auto& binding:pluto_bindings)
    require(get<uint32_t>(base+binding.rva)==binding.expected,"Binding bytes changed");
  for(const auto& binding:pluto_bindings) {
    if(binding.expected==0x485a40)continue; // Command submission is never called.
    const auto signed_address=static_cast<int32_t>(binding.expected);
    const auto value=signed_address<0?0u-address(static_cast<uint32_t>(-signed_address)):address(binding.expected);
    DWORD previous=0,unused=0;
    require(VirtualProtect(reinterpret_cast<void*>(base+binding.rva),4,PAGE_EXECUTE_READWRITE,&previous)!=0,"Cannot bind fixture");
    put<uint32_t>(base+binding.rva,value);
    VirtualProtect(reinterpret_cast<void*>(base+binding.rva),4,previous,&unused);
  }
  FlushInstructionCache(GetCurrentProcess(),nullptr,0);
  // CoG 2026 unit-observation builder: ECX points to context, 24-byte records
  // append to vector at +20. Its native classification is own=0/enemy=1/neutral=2.
  const auto observe=reinterpret_cast<void (__thiscall*)(void*)>(base+0x33f70);
  for(uint32_t self=0;self<8;++self) {
    std::array<std::array<uint8_t,36>,5> sprites{};
    const uint8_t owners[]={static_cast<uint8_t>(self),static_cast<uint8_t>((self+1)%8),
      static_cast<uint8_t>((self+2)%8),11,static_cast<uint8_t>((self+3)%8)};
    const uint16_t types[]={0,37,65,176,0};
    for(unsigned i=0;i<5;++i) {
      const auto unit=address(0x59cca8+i*336);
      std::memset(reinterpret_cast<void*>(unit),0,336);
      put<uint32_t>(unit+4,i+1<5?address(0x59cca8+(i+1)*336):0);
      put<uint32_t>(unit+12,reinterpret_cast<uint32_t>(sprites[i].data()));
      put<uint8_t>(unit+76,owners[i]);put<uint8_t>(unit+77,3);
      put<uint16_t>(unit+100,types[i]);put<uint8_t>(unit+165,1);
      sprites[i][12]=i==4?0:static_cast<uint8_t>(1u<<self);
    }
    put<uint32_t>(address(0x628430),address(0x59cca8));
    std::array<uint32_t,8> context{};
    std::array<uint8_t,24*16> records{};
    const auto start=reinterpret_cast<uint32_t>(records.data());
    context[0]=self;context[1]=1;context[3]=context[4]=128;
    context[5]=context[6]=start;context[7]=start+static_cast<uint32_t>(records.size());
    observe(context.data());
    require(context[6]==start+4*24,"Visible multi-opponent unit count differs");
    for(unsigned i=0;i<4;++i) {
      const auto record=start+i*24;
      require(get<uint32_t>(record)==address(0x59cca8+i*336),"Unit identity or visibility changed");
      require(get<uint32_t>(record+12)==(i==0?0u:(i==3?2u:1u)),"Incorrect ownership class");
      require(get<uint32_t>(record+8)==((i+1)|(1u<<11)),"Incorrect unit handle");
    }
  }
  std::puts("Pinned Pluto observation probe: all 8 self slots, two visible opponents of different races, neutral units and hidden-enemy exclusion passed.");
  // The process owns this module and fixture until exit; no bot object created.
  return 0;
}
int guarded_probe(const wchar_t* path) {
  __try{return probe(path);}
  __except(EXCEPTION_EXECUTE_HANDLER){std::fputs("Observation probe raised a native exception.\n",stderr);return 3;}
}
int wmain(int argc,wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  if(argc!=2){std::fputs("Usage: pluto-observation-probe <pinned pluto.dll>\n",stderr);return 2;}
  try{return guarded_probe(argv[1]);}
  catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
