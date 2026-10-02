#pragma once
#include "scr_profile_13515_x86.h"
#include "scr_layout.h"
#include "session.h"

// Reads the running game's session description. Shared by the bridge, the BWAPI
// snapshot and Pluto's legacy memory view so all of them agree on who is allied.
inline Session read_session(uint32_t g) {
  Session session;
  session.multiplayer=scr::is_multiplayer()!=0;session.replay=scr::is_replay()!=0;
  session.custom_singleplayer=scr::read<uint8_t>(g+layout::Game::custom_singleplayer)==1;
  session.game_type=scr::read<uint16_t>(scr::addr(0x1240e58)+40);
  session.self=scr::local_player_id();
  for(unsigned i=0;i<8;++i) {
    session.players[i]=scr::read<uint8_t>(scr::players()+i*layout::Player::size+layout::Player::player_type);
    for(unsigned j=0;j<8;++j)session.alliances[i][j]=scr::read<uint8_t>(g+layout::Game::alliances+i*12+j);
  }
  return session;
}
