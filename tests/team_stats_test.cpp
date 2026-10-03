#include "team_stats.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
static void require(bool ok){if(!ok)throw std::runtime_error("Team stats test failed");}
static uint32_t at(const std::vector<uint8_t>& g,size_t base,unsigned type,unsigned owner){uint32_t v;std::memcpy(&v,g.data()+base+(type*12+owner)*4,4);return v;}
static void set(std::vector<uint8_t>& g,size_t base,unsigned type,unsigned owner,uint32_t v){std::memcpy(g.data()+base+(type*12+owner)*4,&v,4);}
int main(){
  using namespace layout;
  // Pluto = slot 0, allies = 1 and 2, enemies = 3 and 4.
  Session s;s.self=0;for(unsigned i=0;i<5;++i)s.players[i]=2;
  s.alliances[0][1]=s.alliances[1][0]=s.alliances[0][2]=s.alliances[2][0]=1;
  const AllyMask allies=make_ally_mask(s);
  std::vector<uint8_t> g(Game::size,0);
  const unsigned marine=0,scv=7,zergling=37;
  set(g,Game::all_units_count,marine,0,20);set(g,Game::all_units_count,marine,1,40);set(g,Game::all_units_count,marine,2,5);
  set(g,Game::all_units_count,marine,3,99);set(g,Game::all_units_count,marine,4,7);        // enemies must not count
  set(g,Game::completed_units_count,scv,0,12);set(g,Game::completed_units_count,scv,2,9);
  set(g,Game::unit_kills,zergling,1,30);
  set(g,Game::deaths,marine,1,15);                                                            // deaths are not aggregated
  for(unsigned i=0;i<12;++i){std::memcpy(g.data()+Game::minerals+i*4,"\x10\x00\x00\x00",4);}
  uint32_t ally_supply=180;std::memcpy(g.data()+Game::supplies+Supplies::used+1*4,&ally_supply,4);
  std::vector<uint8_t> before=g;
  aggregate_team_stats(g.data(),0,allies);
  require(at(g,Game::all_units_count,marine,0)==65);                                          // 20+40+5
  require(at(g,Game::completed_units_count,scv,0)==21);                                       // 12+9
  require(at(g,Game::unit_kills,zergling,0)==30);
  require(at(g,Game::deaths,marine,0)==0);                                                    // untouched
  for(unsigned owner=1;owner<12;++owner)                                                      // allies' and enemies' own rows untouched
    require(at(g,Game::all_units_count,marine,owner)==at(before,Game::all_units_count,marine,owner));
  require(std::memcmp(g.data()+Game::minerals,before.data()+Game::minerals,48)==0);          // resources untouched
  require(std::memcmp(g.data()+Game::supplies,before.data()+Game::supplies,3*Supplies::size)==0); // supplies untouched
  // Nothing else in the structure changed.
  std::vector<uint8_t> expected=before;
  set(expected,Game::all_units_count,marine,0,65);set(expected,Game::completed_units_count,scv,0,21);set(expected,Game::unit_kills,zergling,0,30);
  require(g==expected);
  // No allies: identical output. Invalid self: untouched.
  std::vector<uint8_t> plain=before;aggregate_team_stats(plain.data(),0,AllyMask{});require(plain==before);
  std::vector<uint8_t> bad=before;aggregate_team_stats(bad.data(),9,allies);require(bad==before);
  // Repeated application keeps adding (caller must apply once per fresh copy).
  aggregate_team_stats(g.data(),0,allies);require(at(g,Game::all_units_count,marine,0)==110);
  std::puts("team stats aggregation: counts and kills summed for allies only, resources/supplies/deaths untouched");
}
