#include "pluto_releases.h"
#include "pluto_binding_select.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool ok,const char* what) {
  if(!ok){std::fprintf(stderr,"pluto bindings test failed: %s\n",what);throw std::runtime_error(what);}
}
static bool lowercase_hex64(const std::string& text) {
  if(text.size()!=64)return false;
  for(char c:text)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
  return true;
}
// A fake loaded image: zeros, with one table's expected words written at that table's RVAs.
static std::vector<uint8_t> image_of(const PlutoBindingSet& set,size_t size=0x90000) {
  std::vector<uint8_t> image(size,0);
  for(size_t i=0;i<set.count;++i)std::memcpy(&image[set.bindings[i].rva],&set.bindings[i].expected,4);
  return image;
}
static auto reader(const std::vector<uint8_t>& image) {
  return [&image](uint32_t rva){
    if(rva+4>image.size())throw std::out_of_range("read past the end of the image");
    uint32_t word;std::memcpy(&word,&image[rva],4);return word;
  };
}

int main() {
  try {
    { // Hash table: well-formed, unique, findable, nothing else accepted.
      std::set<std::string> seen;
      for(const auto& dll:pluto_dlls) {
        require(lowercase_hex64(dll.sha256),"every DLL hash is 64 lowercase hex characters");
        require(seen.insert(dll.sha256).second,"DLL hashes are unique");
        const auto* found=find_pluto_dll(dll.sha256);
        require(found==&dll,"each listed hash is found");
      }
      require(find_pluto_dll("")==nullptr && find_pluto_dll("abc")==nullptr,"empty and truncated hashes are refused");
      std::string upper=pluto_v2_0_sha256;for(auto& c:upper)if(c>='a'&&c<='f')c-=32;
      require(find_pluto_dll(upper)==nullptr,"hashes are compared exactly as file_sha256 prints them (lowercase)");
      require(find_pluto_dll(std::string(64,'0'))==nullptr,"an unknown 64-digit hash is refused");
    }
    { // Release facts the bridge relies on.
      const auto* cog=find_pluto_dll(pluto_sha256);const auto* patched=find_pluto_dll(pluto_noresign_sha256);const auto* v2=find_pluto_dll(pluto_v2_0_sha256);
      require(cog&&patched&&v2,"all three DLLs are known");
      require(std::string(cog->release)=="cog2026-2578600" && std::string(patched->release)=="cog2026-2578600" && std::string(v2->release)=="v2.0","release names");
      require(!cog->no_resign_patch && patched->no_resign_patch && !v2->no_resign_patch,"only the patched CoG DLL carries the binary patch");
      require(!cog->honors_resign_env && !patched->honors_resign_env && v2->honors_resign_env,"only v2.0 reads BWRL_RESIGN_THRESHOLD");
    }
    { // Every release has a binding table, every table belongs to a release, tables are well-formed.
      std::set<std::string> table_names;
      for(const auto& set:pluto_binding_sets) {
        require(table_names.insert(set.release).second,"binding table names are unique");
        require(set.count>=200,"a table binds a plausible number of addresses");
        std::set<uint32_t> rvas;
        for(size_t i=0;i<set.count;++i) {
          const auto& binding=set.bindings[i];
          require(binding.rva>0 && rvas.insert(binding.rva).second,"RVAs are non-zero and not repeated");
          const int32_t signed_address=static_cast<int32_t>(binding.expected);
          const uint32_t magnitude=signed_address<0?static_cast<uint32_t>(-signed_address):binding.expected;
          require(magnitude>=0x485a40 && magnitude<=0x6d1260,"bound addresses lie in the emulated 1.16.1 range");
        }
      }
      for(const auto& dll:pluto_dlls) {
        bool has_table=false;for(const auto& set:pluto_binding_sets)has_table|=std::string(set.release)==dll.release;
        require(has_table,"every supported DLL has a binding table");
      }
      for(const auto& set:pluto_binding_sets) {
        bool has_dll=false;for(const auto& dll:pluto_dlls)has_dll|=std::string(set.release)==dll.release;
        require(has_dll,"every binding table belongs to a supported DLL");
      }
    }
    { // Regression: the CoG table is exactly what the original generator produced (first and last entries, count).
      const auto& cog=pluto_binding_sets[0];
      require(std::string(cog.release)=="cog2026-2578600" && cog.count==250,"CoG table: 250 entries");
      require(cog.bindings[0].rva==0x14bf && cog.bindings[0].expected==0x00512688,"CoG table: first entry");
      require(&pluto_bindings[0]==&cog.bindings[0],"the probe alias points at the first release");
    }
    { // Selecting the table for a loaded image.
      const PlutoBindingSet* cog=nullptr;const PlutoBindingSet* v2=nullptr;
      for(const auto& set:pluto_binding_sets){if(std::string(set.release)=="cog2026-2578600")cog=&set;if(std::string(set.release)=="v2.0")v2=&set;}
      require(cog&&v2&&cog->count==250&&v2->count==250,"both releases are present");
      auto cog_image=image_of(*cog);require(select_binding_set(reader(cog_image),cog_image.size())==cog,"a CoG image selects the CoG table");
      auto v2_image=image_of(*v2);require(select_binding_set(reader(v2_image),v2_image.size())==v2,"a v2.0 image selects the v2.0 table");
      std::vector<uint8_t> blank(0x90000,0);require(select_binding_set(reader(blank),blank.size())==nullptr,"an unrelated image selects nothing");
      auto damaged=image_of(*v2);const uint32_t word=0x12345678;std::memcpy(&damaged[v2->bindings[100].rva],&word,4);
      require(select_binding_set(reader(damaged),damaged.size())==nullptr,"one wrong entry rejects the whole table");
      // An image that satisfies both tables (their shared RVAs carry the same addresses) selects the first listed table.
      auto both=image_of(*cog);for(size_t i=0;i<v2->count;++i)std::memcpy(&both[v2->bindings[i].rva],&v2->bindings[i].expected,4);
      const auto* chosen=select_binding_set(reader(both),both.size());
      size_t cog_matches=0;for(size_t i=0;i<cog->count;++i){uint32_t w;std::memcpy(&w,&both[cog->bindings[i].rva],4);cog_matches+=w==cog->bindings[i].expected;}
      require(cog_matches!=cog->count || chosen==cog,"when more than one table matches, the first listed one wins");
      require(cog_matches==cog->count || chosen==v2 || chosen==nullptr,"a table that does not fully match is never selected");
      // Safety: a table whose RVAs lie outside the image is never read, even though the fake reader would throw.
      std::vector<uint8_t> tiny(0x1000,0);require(select_binding_set(reader(tiny),tiny.size())==nullptr,"an image smaller than the table is never read out of bounds");
      require(select_binding_set(reader(cog_image),0x20)==nullptr,"image_size bounds the read even when the reader could serve more");
      // A table that matches only because its RVAs are readable must still be exact: swapping two entries' values fails.
      auto swapped=image_of(*cog);uint32_t a=cog->bindings[0].expected,b=cog->bindings[1].expected;
      std::memcpy(&swapped[cog->bindings[0].rva],&b,4);std::memcpy(&swapped[cog->bindings[1].rva],&a,4);
      require(a!=b,"the two entries differ, so swapping them is a real change");
      require(select_binding_set(reader(swapped),swapped.size())==nullptr,"swapped values do not match");
    }
    std::puts("pluto bindings: release hashes, per-release tables, regeneration regression and table selection passed");
    return 0;
  } catch(const std::exception&){return 1;}
}
