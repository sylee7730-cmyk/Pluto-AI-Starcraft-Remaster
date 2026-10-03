#include "snapshot.h"
#include "scr_profile_13515_x86.h"
#include "scr_layout.h"
#include "session.h"
#include "session_reader.h"
#include <BWAPI.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

using scr::read;
namespace G = layout::Game;
namespace U = layout::Unit;
namespace P = layout::Player;
namespace {
uint64_t identity(uint32_t p) { return (uint64_t(p) << 8) | read<uint8_t>(p + U::minor_unique_index); }
template<size_t N> void text(char (&dest)[N], uint32_t p, size_t length) {
  std::memcpy(dest, reinterpret_cast<const void*>(p), std::min(length,N-1)); dest[std::min(length,N-1)] = 0;
}
}
int Snapshot::id_for(uint32_t p) {
  auto key = identity(p); auto found = ids.find(key);
  if (found != ids.end()) return found->second;
  if (entries.size() == 10000) throw std::runtime_error("BWAPI unit identity limit reached");
  int id = static_cast<int>(entries.size());
  entries.push_back({p,read<uint8_t>(p + U::minor_unique_index)}); ids.emplace(key,id); return id;
}
int Snapshot::known_id(uint32_t p) const {
  if (!p) return -1;
  auto found=ids.find(identity(p)); return found == ids.end() ? -1 : found->second;
}
uint32_t Snapshot::raw_unit(int id) const {
  return id >= 0 && size_t(id)<entries.size() && entries[id].seen ? entries[id].pointer : 0;
}
uint32_t Snapshot::unit_handle(int id) const {
  uint32_t p=raw_unit(id); if (!p) return 0;
  auto start=scr::units(); auto length=read<uint32_t>(scr::addr(0x10436f4));
  const auto end=start+length*U::size;
  if (p<start || p>=end || (p-start)%U::size) return 0;
  return static_cast<uint32_t>((p-start)/U::size+1) | (uint32_t(read<uint8_t>(p+U::minor_unique_index)) << (length>1700 ? 13:11));
}
void Snapshot::event(BWAPI::EventType::Enum type,int id) {
  if (data->eventCount >= data->MAX_EVENTS) throw std::runtime_error("BWAPI event capacity exceeded");
  auto& e=data->events[data->eventCount++]; e.type=type; e.v1=id; e.v2=0;
}
bool Snapshot::update(bool first) {
  const auto g=scr::game();
  if (!g || scr::is_replay()) return false;
  auto width=read<uint16_t>(g+G::map_width_tiles),height=read<uint16_t>(g+G::map_height_tiles);
  auto self=scr::local_player_id();
  if (!width || width>256 || !height || height>256 || self>=8) return false;
  const AllyMask ally_mask=(hide_allies || ally_as_own)?make_ally_mask(read_session(g)):AllyMask{};
  const AllyMask allies=hide_allies?ally_mask:AllyMask{};
  remap_owners=ally_as_own?ally_mask:AllyMask{};
  team_allies=team_stats?make_ally_mask(read_session(g)):AllyMask{};
  data->eventCount=data->eventStringCount=0;
  data->self=self; data->neutral=11; data->enemy=-1;
  data->client_version=BWAPI::CLIENT_VERSION;
  data->isInGame=true; data->isPaused=scr::is_paused()!=0;
  data->isMultiplayer=scr::is_multiplayer()!=0;
  data->frameCount=read<uint32_t>(g+G::frame_count);
  data->elapsedTime=read<uint32_t>(g+G::elapsed_seconds);
  data->mapWidth=width; data->mapHeight=height;
  data->gameType=read<uint16_t>(scr::addr(0x1240e58)+40);
  data->fps=24; data->averageFPS=24.; data->hasGUI=true;
  // Commands use the game's turn queue. Timing has to be measured before enabling bots.
  data->latencyFrames=2; data->remainingLatencyFrames=2; data->latencyTime=84;
  data->remainingLatencyTime=84; data->hasLatCom=false;
  if(data->isMultiplayer) {
    const auto rate=read<uint32_t>(scr::addr(0x1240e58)+44);
    const auto user_delay=std::min(read<uint32_t>(scr::addr(0x1241288)),2u);
    const auto turns=2u+user_delay;
    // Native SCR's configured turn rate and user delay provide a latency
    // estimate. The command queue remains owned and paced by the game.
    const auto turn_rate=(rate>=8 && rate<=24)?rate:24u;
    data->latencyFrames=static_cast<int>((24u*turns+turn_rate-1)/turn_rate);
    data->latencyTime=static_cast<int>((1000u*turns+turn_rate-1)/turn_rate);
    data->remainingLatencyFrames=data->latencyFrames;
    data->remainingLatencyTime=data->latencyTime;
  }
  data->screenX=scr::screen_x(); data->screenY=scr::screen_y();
  update_players(g); update_map(g,first);
  for (auto& e:entries) e.seen=false;
  std::vector<int> current;
  for (uint32_t head : {scr::first_active_unit(),scr::first_hidden_unit()}) {
    std::unordered_set<uint32_t> traversed;
    for (uint32_t p=head;p;p=read<uint32_t>(p+4)) {
      if (traversed.size()>=10000 || !traversed.insert(p).second) throw std::runtime_error("Invalid SCR unit list");
      const auto sprite=read<uint32_t>(p+12);
      const auto type=read<uint16_t>(p+U::unit_id);
      const auto owner=read<uint8_t>(p+U::player);
      if (!sprite || type>=228 || owner>=12) continue;
      if (allies.hides(owner)) continue;
      if (read<uint8_t>(p+U::order)==0 && read<uint8_t>(p+U::order_state)==1) continue;
      // Own hidden units (e.g. loaded passengers and larva) remain accessible.
      auto visibility=read<uint8_t>(sprite+layout::Sprite::visibility_mask);
      const auto effective_owner=remap_owners.hides(owner)?self:owner;
      if (effective_owner!=self && owner!=11 && !(visibility & (1u<<self))) continue;
      int id=id_for(p); entries[id].seen=true; current.push_back(id);
    }
  }
  for (int id:current) {
    auto before=data->units[id]; update_unit(id);
    auto& e=entries[id]; auto& after=data->units[id];
    if (!e.accessible) { event(BWAPI::EventType::UnitDiscover,id); event(BWAPI::EventType::UnitShow,id); }
    else {
      if (before.type!=after.type) event(BWAPI::EventType::UnitMorph,id);
      if (before.player!=after.player) event(BWAPI::EventType::UnitRenegade,id);
      if (!before.isCompleted && after.isCompleted) event(BWAPI::EventType::UnitComplete,id);
    }
    e.accessible=true;
    if (after.player!=data->self) {
      ++data->players[after.player].visibleUnitCount[after.type];
    }
  }
  for (size_t id=0;id<entries.size();++id) if(entries[id].accessible && !entries[id].seen) {
    event(BWAPI::EventType::UnitHide,static_cast<int>(id)); event(BWAPI::EventType::UnitEvade,static_cast<int>(id));
    data->units[id].exists=false; entries[id].accessible=false;
  }
  if (first) data->initialUnitCount=static_cast<int>(entries.size());
  event(first ? BWAPI::EventType::MatchStart:BWAPI::EventType::MatchFrame,0);
  return true;
}
void Snapshot::update_players(uint32_t g) {
  data->playerCount=12; data->forceCount=data->gameType==BWAPI::GameTypes::Top_vs_Bottom?3:1;
  if(data->forceCount==3) {
    std::strcpy(data->forces[1].name,"Top");std::strcpy(data->forces[2].name,"Bottom");
  }
  for (unsigned i=0;i<12;++i) {
    auto& p=data->players[i]; const auto raw=scr::players()+i*P::size;
    p={}; text(p.name,raw+P::name,25);
    p.race=read<uint8_t>(raw+P::race); p.type=read<uint8_t>(raw+P::player_type); p.isNeutral=i==11;
    p.isParticipating=i<8 && (p.type==BWAPI::PlayerTypes::Player || p.type==BWAPI::PlayerTypes::Computer);
    if(p.isParticipating && data->forceCount==3) {
      const auto team=read<uint8_t>(raw+P::team);p.force=team<=2?team:0;
    }
    p.color=i; p.startLocationX=p.startLocationY=-1;
    for (unsigned j=0;j<12;++j) {
      const bool other_participates=j<8 && session_participant(read<uint8_t>(scr::players()+j*P::size+P::player_type));
      p.isAlly[j]=i==j || (p.isParticipating && other_participates && read<uint8_t>(g+G::alliances+i*12+j)!=0);
      p.isEnemy[j]=p.isParticipating && other_participates && !p.isAlly[j];
    }
    if (data->enemy<0 && p.isParticipating && i!=unsigned(data->self) && p.isEnemy[data->self]) data->enemy=i;
    if (i<8) {
      auto x=read<uint16_t>(g+G::start_position+i*4),y=read<uint16_t>(g+G::start_position+i*4+2);
      if (i==unsigned(data->self)) { p.startLocationX=x/32-2; p.startLocationY=y/32-1; }
      auto victory=read<uint8_t>(g+G::victory_state+i);
      p.isDefeated=victory==1; p.isVictorious=victory==3;
    }
    if (i!=unsigned(data->self)) continue;
    p.minerals=read<uint32_t>(g+G::minerals+i*4); p.gas=read<uint32_t>(g+G::gas+i*4);
    p.gatheredMinerals=read<uint32_t>(g+96+i*4); p.gatheredGas=read<uint32_t>(g+144+i*4);
    for(unsigned race=0;race<3;++race) {
      auto s=g+G::supplies+race*layout::Supplies::size;
      p.supplyTotal[race]=std::min(read<uint32_t>(s+i*4),read<uint32_t>(s+96+i*4));
      p.supplyUsed[race]=read<uint32_t>(s+48+i*4);
    }
    for(unsigned type=0;type<228;++type) {
      p.allUnitCount[type]=read<uint32_t>(g+G::all_units_count+(type*12+i)*4);
      p.completedUnitCount[type]=read<uint32_t>(g+G::completed_units_count+(type*12+i)*4);
      p.deadUnitCount[type]=read<uint32_t>(g+G::deaths+(type*12+i)*4);
      p.killedUnitCount[type]=read<uint32_t>(g+G::unit_kills+(type*12+i)*4);
      for(unsigned ally=0;ally<8;++ally) if(team_allies.hides(ally)) {  // Team strength experiment.
        p.allUnitCount[type]+=read<uint32_t>(g+G::all_units_count+(type*12+ally)*4);
        p.completedUnitCount[type]+=read<uint32_t>(g+G::completed_units_count+(type*12+ally)*4);
        p.killedUnitCount[type]+=read<uint32_t>(g+G::unit_kills+(type*12+ally)*4);
      }
      p.visibleUnitCount[type]=p.allUnitCount[type];
      p.isUnitAvailable[type]=read<uint8_t>(g+G::unit_availability+i*228+type)!=0;
    }
    for(unsigned tech=0;tech<44;++tech) {
      auto offset=tech<24 ? G::tech_level_sc+i*24+tech:G::tech_level_bw+i*20+tech-24;
      auto available=tech<24 ? G::tech_availability_sc+i*24+tech:G::tech_availability_bw+i*20+tech-24;
      p.hasResearched[tech]=read<uint8_t>(g+offset)!=0; p.isResearchAvailable[tech]=read<uint8_t>(g+available)!=0;
      p.isResearching[tech]=(read<uint8_t>(g+G::tech_in_progress+i*6+tech/8) & (1u<<(tech%8)))!=0;
    }
    for(unsigned upgrade=0;upgrade<61;++upgrade) {
      auto level=upgrade<46 ? G::upgrade_level_sc+i*46+upgrade:G::upgrade_level_bw+i*15+upgrade-46;
      auto limit=upgrade<46 ? G::upgrade_limit_sc+i*46+upgrade:G::upgrade_limit_bw+i*15+upgrade-46;
      p.upgradeLevel[upgrade]=read<uint8_t>(g+level); p.maxUpgradeLevel[upgrade]=read<uint8_t>(g+limit);
      p.isUpgrading[upgrade]=(read<uint8_t>(g+G::upgrade_in_progress+i*8+upgrade/8)&(1u<<(upgrade%8)))!=0;
    }
  }
}
void Snapshot::update_map(uint32_t g,bool first) {
  if(first) {
    text(data->mapName,g+G::map_title,32); text(data->mapPathName,g+G::map_path,260);
    std::strcpy(data->mapFileName,data->mapPathName);
    for (unsigned i=0;i<8;++i) {
      auto x=read<uint16_t>(g+G::start_position+i*4),y=read<uint16_t>(g+G::start_position+i*4+2);
      if (!x || !y) continue;
      auto& s=data->startLocations[data->startLocationCount++]; s.x=x/32-2; s.y=y/32-1;
    }
  }
  auto flags=scr::map_tile_flags(),tiles=scr::tileset_indexed_map_tiles(),cv5=scr::tileset_cv5(),vf4=scr::minitile_data();
  if (!flags || !tiles || !cv5 || !vf4) throw std::runtime_error("Missing SCR terrain bindings");
  for(int y=0;y<data->mapHeight;++y) for(int x=0;x<data->mapWidth;++x) {
    auto f=read<uint32_t>(flags+4*(y*data->mapWidth+x));
    data->isVisible[x][y]=(f&(1u<<data->self))==0;
    data->isExplored[x][y]=(f&(1u<<(data->self+8)))==0;
    data->hasCreep[x][y]=data->isVisible[x][y] && (f&0x40400000u)!=0;
    data->isOccupied[x][y]=data->isVisible[x][y] && (f&0x08000000u)!=0;
    if (!first) continue;
    data->isBuildable[x][y]=(f&0x00840000u)==0;
    data->getGroundHeight[x][y]=(f>>24)&7;
    auto tile=read<uint16_t>(tiles+2*(y*data->mapWidth+x));
    auto mega=read<uint16_t>(cv5+(tile>>4)*52+20+(tile&15)*2);
    for(int my=0;my<4;++my) for(int mx=0;mx<4;++mx) {
      int wx=x*4+mx,wy=y*4+my;
      bool edge=wy>=data->mapHeight*4-4 || (wy>=data->mapHeight*4-8 && (wx<20 || wx>=data->mapWidth*4-20));
      data->isWalkable[wx][wy]=!edge && (read<uint16_t>(vf4+mega*32+(my*4+mx)*2)&1)!=0;
    }
  }
}
void Snapshot::update_unit(int id) {
  auto p=entries[id].pointer; auto& d=data->units[id]; const auto old=d; d={}; d.id=id;
  auto byte=[p](size_t o){return read<uint8_t>(p+o);}; auto word=[p](size_t o){return read<uint16_t>(p+o);};
  auto ptr=[p](size_t o){return read<uint32_t>(p+o);}; auto relation=[&](size_t o){return known_id(ptr(o));};
  auto flags=ptr(U::flags); auto sprite=ptr(12); auto visibility=read<uint8_t>(sprite+12);
  const bool remapped_ally=remap_owners.hides(byte(U::player));
  d.player=remapped_ally?data->self:byte(U::player); d.type=word(U::unit_id);
  if(d.type>=176 && d.type<=178) d.type=BWAPI::UnitTypes::Resource_Mineral_Field;
  BWAPI::UnitType type(d.type);
  d.positionX=word(40); d.positionY=word(42); d.hitPoints=(read<int32_t>(p+8)+255)/256;
  d.lastHitPoints=old.hitPoints; d.shields=(ptr(U::shields)+255)/256;
  d.exists=true; d.isCompleted=(flags&1)!=0;
  d.isBurrowed=(flags&0x10)!=0; d.isCloaked=(flags&0x200)!=0 && !d.isBurrowed;
  d.isDetected=d.player==data->self || !(flags&0x100) || (ptr(U::detection_status)&(1u<<data->self));
  for(int i=0;i<8;++i) d.isVisible[i]=d.player==i || ((visibility&(1u<<i))!=0);
  d.angle=((int(byte(33))-64+256)%256)*3.14159265358979323846/128.0;
  d.velocityX=read<int32_t>(p+64)/256.; d.velocityY=read<int32_t>(p+68)/256.;
  d.order=byte(U::order); d.secondaryOrder=byte(U::secondary_order);
  d.target=relation(20); d.targetPositionX=word(16); d.targetPositionY=word(18);
  d.orderTarget=relation(U::order_target+4); d.orderTargetPositionX=word(U::order_target); d.orderTargetPositionY=word(U::order_target+2);
  d.buildUnit=relation(236); d.addon=type.isBuilding()?relation(192):-1;
  d.nydusExit=d.type==BWAPI::UnitTypes::Zerg_Nydus_Canal?relation(208):-1;
  d.transport=(flags&0x60)?relation(U::related):-1;
  d.hatchery=d.type==BWAPI::UnitTypes::Zerg_Larva?relation(U::related):-1;
  d.carrier=(d.type==BWAPI::UnitTypes::Protoss_Interceptor || d.type==BWAPI::UnitTypes::Protoss_Scarab)?relation(192):-1;
  d.powerUp=type.isWorker()?relation(192):-1; d.rallyUnit=-1;
  d.isAccelerating=(byte(32)&2)!=0; d.isBraking=(byte(32)&4)!=0; d.isMoving=(byte(32)&3)!=0 || d.order==BWAPI::Orders::Move;
  d.isLifted=(flags&4)!=0 && type.isBuilding(); d.isInterruptible=(flags&0x1000)==0;
  d.isInvincible=(flags&0x4000000)!=0; d.isHallucination=(flags&0x40000000)!=0;
  d.isPowered=!(type.getRace()==BWAPI::Races::Protoss && type.isBuilding() && (flags&0x400));
  d.isUnderStorm=byte(U::is_under_storm)!=0; d.isParasited=byte(U::parasited_by_players)!=0;
  d.isBlind=byte(U::is_blind)!=0; d.isGathering=type.isWorker() && (flags&0x800000)!=0;
  d.carryResourceType=type.isWorker()?byte(U::carried_powerup_flags):0;
  d.isBeingGathered=type.isResourceContainer() && (byte(211)!=0 || ptr(212)!=0);
  d.resources=type.isResourceContainer()?word(208):0; d.resourceGroup=type.isResourceContainer()?byte(216):0;
  d.groundWeaponCooldown=byte(U::ground_cooldown); d.airWeaponCooldown=byte(U::air_cooldown); d.spellCooldown=byte(U::spell_cooldown);
  d.killCount=byte(U::kills); d.acidSporeCount=byte(U::acid_spore_count); d.spiderMineCount=d.type==BWAPI::UnitTypes::Terran_Vulture?byte(192):0;
  d.defenseMatrixPoints=word(U::defensive_matrix_dmg)/256;
  d.defenseMatrixTimer=byte(U::matrix_timer); d.ensnareTimer=byte(U::ensnare_timer); d.irradiateTimer=byte(U::irradiate_timer);
  d.lockdownTimer=byte(U::lockdown_timer); d.maelstromTimer=byte(U::maelstrom_timer); d.orderTimer=byte(U::order_timer);
  d.plagueTimer=byte(U::plague_timer); d.removeTimer=word(U::death_timer); d.stasisTimer=byte(U::stasis_timer); d.stimTimer=byte(U::stim_timer);
  if(remapped_ally && ally_stasis && d.stasisTimer==0)d.stasisTimer=1;
  d.isMorphing=d.order==BWAPI::Orders::ZergBirth || d.order==BWAPI::Orders::ZergBuildingMorph || d.order==BWAPI::Orders::ZergUnitMorph || d.order==BWAPI::Orders::Enum::IncompleteMorphing;
  if(d.isMorphing) d.isCompleted=false;
  d.buildType=BWAPI::UnitTypes::None; d.tech=BWAPI::TechTypes::None; d.upgrade=BWAPI::UpgradeTypes::None;
  if(d.player==data->self) {
    d.energy=type.isSpellcaster()?(word(U::energy)+255)/256:0;
    unsigned slot=byte(U::current_build_slot)%5;
    for(unsigned n=0;n<5;++n) { int queued=word(U::build_queue+((slot+n)%5)*2); if(queued>=228)break; d.trainingQueue[d.trainingQueueCount++]=queued; }
    d.isTraining=type.canProduce() && !(type.getRace()==BWAPI::Races::Zerg && type.isResourceDepot()) && d.trainingQueueCount>0;
    if(!d.isCompleted) { d.buildType=d.isMorphing && d.trainingQueueCount ? d.trainingQueue[0]:d.type; d.remainingBuildTime=word(U::remaining_build_time); }
    if(d.isMorphing || d.order==BWAPI::Orders::PlaceBuilding || d.order==BWAPI::Orders::Enum::PlaceProtossBuilding || d.order==BWAPI::Orders::Enum::DroneLand)
      d.buildType=d.trainingQueueCount?d.trainingQueue[0]:d.type;
    if(d.buildUnit>=0) d.remainingTrainTime=read<uint16_t>(raw_unit(d.buildUnit)+U::remaining_build_time);
    if(d.order==BWAPI::Orders::ConstructingBuilding && d.buildUnit>=0) d.buildType=read<uint16_t>(raw_unit(d.buildUnit)+U::unit_id);
    if(d.order==BWAPI::Orders::ResearchTech) { d.tech=byte(200); d.remainingResearchTime=word(198); }
    if(d.order==BWAPI::Orders::Upgrade) { d.upgrade=byte(201); d.remainingUpgradeTime=word(198); }
    d.rallyPositionX=word(248); d.rallyPositionY=word(250); d.rallyUnit=relation(252);
    if(type.producesLarva()) d.remainingTrainTime=byte(202)*9+(byte(U::order_wait)+8)%9;
  }
  d.isConstructing=d.isMorphing || d.order==BWAPI::Orders::ConstructingBuilding || d.order==BWAPI::Orders::PlaceBuilding ||
    d.order==BWAPI::Orders::Enum::DroneBuild || d.order==BWAPI::Orders::Enum::DroneStartBuild || d.order==BWAPI::Orders::Enum::DroneLand ||
    d.order==BWAPI::Orders::Enum::PlaceProtossBuilding || d.order==BWAPI::Orders::Enum::CreateProtossBuilding ||
    d.order==BWAPI::Orders::Enum::IncompleteBuilding || d.order==BWAPI::Orders::Enum::IncompleteWarping ||
    d.order==BWAPI::Orders::BuildNydusExit || d.order==BWAPI::Orders::BuildAddon || d.secondaryOrder==BWAPI::Orders::BuildAddon;
  d.isIdle=!d.isTraining && !d.isConstructing && (d.order==BWAPI::Orders::PlayerGuard || d.order==BWAPI::Orders::Guard ||
    d.order==BWAPI::Orders::Stop || d.order==BWAPI::Orders::PickupIdle || d.order==BWAPI::Orders::Nothing ||
    d.order==BWAPI::Orders::Medic || d.order==BWAPI::Orders::Carrier || d.order==BWAPI::Orders::Reaver ||
    d.order==BWAPI::Orders::Critter || d.order==BWAPI::Orders::Neutral || d.order==BWAPI::Orders::TowerGuard ||
    d.order==BWAPI::Orders::Burrowed || d.order==BWAPI::Orders::NukeTrain || d.order==BWAPI::Orders::Larva);
  d.isAttacking=d.order==BWAPI::Orders::Enum::Attack1 || d.order==BWAPI::Orders::AttackUnit || d.order==BWAPI::Orders::Enum::TowerAttack;
  d.isStartingAttack=d.groundWeaponCooldown>old.groundWeaponCooldown || d.airWeaponCooldown>old.airWeaponCooldown;
  d.isAttackFrame=d.isStartingAttack; d.buttonset=word(U::buttons);
}
