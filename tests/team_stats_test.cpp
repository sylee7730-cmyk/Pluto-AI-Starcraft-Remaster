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

  // Mode "army": combat units and kills only; bases, workers, larvae and child units stay Pluto's own.
  const unsigned command_center=106,drone=41,larva=35,interceptor=73,siege_tank=5,zealot=65;
  std::vector<uint8_t> a(Game::size,0);
  set(a,Game::all_units_count,marine,0,20);set(a,Game::all_units_count,marine,1,40);
  set(a,Game::all_units_count,command_center,0,1);set(a,Game::all_units_count,command_center,1,3);
  set(a,Game::all_units_count,drone,2,30);set(a,Game::all_units_count,larva,2,9);set(a,Game::all_units_count,interceptor,1,16);
  set(a,Game::completed_units_count,siege_tank,1,6);set(a,Game::completed_units_count,zealot,2,8);
  set(a,Game::unit_kills,zergling,1,30);set(a,Game::unit_kills,zergling,0,5);
  std::vector<uint8_t> a_before=a;
  aggregate_team_stats(a.data(),0,allies,TeamStatsMode::army);
  require(at(a,Game::all_units_count,marine,0)==60);                                          // combat unit added
  require(at(a,Game::all_units_count,command_center,0)==1);                                   // ally bases not added
  require(at(a,Game::all_units_count,drone,0)==0 && at(a,Game::all_units_count,larva,0)==0);  // workers/larvae not added
  require(at(a,Game::all_units_count,interceptor,0)==0);                                      // child units not added
  require(at(a,Game::completed_units_count,siege_tank,0)==6 && at(a,Game::completed_units_count,zealot,0)==8);
  require(at(a,Game::unit_kills,zergling,0)==35);                                             // kills always added
  // Mode "kills": only the kill table changes.
  std::vector<uint8_t> k=a_before;aggregate_team_stats(k.data(),0,allies,TeamStatsMode::kills);
  require(at(k,Game::unit_kills,zergling,0)==35 && at(k,Game::all_units_count,marine,0)==20 && at(k,Game::completed_units_count,siege_tank,0)==0);
  // Mode "off": untouched. Mode "all": everything.
  std::vector<uint8_t> o=a_before;aggregate_team_stats(o.data(),0,allies,TeamStatsMode::off);require(o==a_before);
  std::vector<uint8_t> all=a_before;aggregate_team_stats(all.data(),0,allies,TeamStatsMode::all);
  require(at(all,Game::all_units_count,command_center,0)==4 && at(all,Game::all_units_count,drone,0)==30);
  // Classification spot checks.
  require(team_stats_counts_as_army(0) && team_stats_counts_as_army(5) && team_stats_counts_as_army(37) && team_stats_counts_as_army(65) && team_stats_counts_as_army(105));
  require(!team_stats_counts_as_army(7) && !team_stats_counts_as_army(41) && !team_stats_counts_as_army(64));
  require(!team_stats_counts_as_army(106) && !team_stats_counts_as_army(154) && !team_stats_counts_as_army(227));
  require(!team_stats_counts_as_army(13) && !team_stats_counts_as_army(36) && !team_stats_counts_as_army(97));
  require(parse_team_stats_mode(L"army")==TeamStatsMode::army && parse_team_stats_mode(L"kills")==TeamStatsMode::kills);
  require(parse_team_stats_mode(L"all")==TeamStatsMode::all && parse_team_stats_mode(L"off")==TeamStatsMode::off);
  require(parse_team_stats_mode(L"1")==TeamStatsMode::army && parse_team_stats_mode(L"0")==TeamStatsMode::off && parse_team_stats_mode(nullptr)==TeamStatsMode::off);
  std::puts("team stats aggregation: all/army/kills/off modes, classification, resources/supplies/deaths untouched");
}
