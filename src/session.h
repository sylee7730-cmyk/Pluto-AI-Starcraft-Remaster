#pragma once
#include <array>
#include <cstdint>

struct Session {
  bool multiplayer=false, replay=false, custom_singleplayer=false;
  uint16_t game_type=0;
  unsigned self=8;
  std::array<uint8_t,8> players{};
  std::array<std::array<uint8_t,8>,8> alliances{};
};
inline bool session_participant(uint8_t type) {return type==1 || type==2;}
inline unsigned session_participant_count(const Session& s) {
  unsigned count=0;for(auto type:s.players)count+=session_participant(type);return count;
}
inline bool session_is_local_ally(const Session& s,unsigned owner) {
  return s.self<8 && owner<8 && owner!=s.self && session_participant(s.players[owner]) &&
    (s.alliances[s.self][owner] || s.alliances[owner][s.self]);
}
inline bool session_has_local_ally(const Session& s) {
  for(unsigned i=0;i<8;++i)if(session_is_local_ally(s,i))return true;
  return false;
}
// Allied play (opt-in, experimental). The pinned observation function classifies
// every non-self owner as an enemy, so units owned by Pluto's allies are withheld
// from Pluto entirely. It then plays as if its allies did not exist.
struct AllyMask {
  std::array<bool,8> hidden{};
  bool hides(unsigned owner) const {return owner<8 && hidden[owner];}
};
inline AllyMask make_ally_mask(const Session& s) {
  AllyMask mask;
  for(unsigned i=0;i<8;++i)mask.hidden[i]=session_is_local_ally(s,i);
  return mask;
}
// Team-aware resignation (opt-in). The model's win estimate cannot include hidden allies,
// so Pluto's own resignation is refused while any ally is still in the game.
// victory_state values: 0 playing, 1 defeated, 3 victorious.
inline bool session_has_living_ally(const Session& s,const std::array<uint8_t,8>& victory_state) {
  for(unsigned i=0;i<8;++i)if(session_is_local_ally(s,i) && victory_state[i]!=1)return true;
  return false;
}
inline const char* session_rejection(const Session& s,bool allow_multiplayer,bool challenge=false,bool allow_allies=false) {
  if(s.replay)return "replay";
  if(s.self>=8)return "local_observer";
  if(s.game_type!=2 && !(challenge && (s.game_type==3 || s.game_type==15)))return "unsupported_game_type";
  if(s.players[s.self]!=2)return "local_slot_not_player";
  unsigned participants=0,computers=0;
  for(auto type:s.players){participants+=session_participant(type);computers+=(type==1);}
  if(challenge ? participants<2 : participants!=2)return challenge?"requires_opponent":"requires_two_players";
  // The pinned observation builder labels every other active player as enemy;
  // human allies of Pluto would therefore be misclassified. Opponents may ally
  // with one another (e.g. the other side in Top vs Bottom). Allied play is only
  // accepted when explicitly enabled; allied units are then hidden from Pluto.
  if(!allow_allies && session_has_local_ally(s))return "local_allies_not_supported";
  if(s.multiplayer)return allow_multiplayer?nullptr:"multiplayer_not_enabled";
  if(!s.custom_singleplayer)return "requires_custom_game";
  if(computers!=(challenge?participants-1:1))return challenge?"offline_requires_computers":"offline_requires_one_computer";
  return nullptr;
}
inline bool supported_session(const Session& s,bool allow_multiplayer,bool challenge=false,bool allow_allies=false) {
  return session_rejection(s,allow_multiplayer,challenge,allow_allies)==nullptr;
}
inline const char* session_game_type(uint16_t type) {
  switch(type) {
    case 2:return "Melee";
    case 3:return "Free For All";
    case 4:return "One on One";
    case 10:return "Use Map Settings";
    case 15:return "Top vs Bottom";
    default:return "Other";
  }
}
