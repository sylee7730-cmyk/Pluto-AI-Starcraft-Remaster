#include "session.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
static void require(bool ok){if(!ok)throw std::runtime_error("Session admission test failed");}
int main(){
  Session s;s.self=3;s.game_type=2;s.players[3]=2;s.players[6]=1;s.custom_singleplayer=true;
  require(supported_session(s,false));
  s.multiplayer=true;s.custom_singleplayer=false;s.players[6]=2;
  require(!supported_session(s,false));require(supported_session(s,true));
  s.replay=true;require(!supported_session(s,true));s.replay=false;
  s.self=9;require(!supported_session(s,true));s.self=3;
  s.game_type=3;require(!supported_session(s,true));s.game_type=2;
  s.players[1]=2;require(!supported_session(s,true));s.players[1]=0;
  s.players[3]=1;require(!supported_session(s,true));s.players[3]=2;
  s.players[6]=0;require(!supported_session(s,true));
  // Failed user games were UMS (10), FFA (3), and Top vs Bottom (15),
  // each with exactly two human players. The diagnostic must name the cause.
  s.players[6]=2;
  for(auto type:{10,3,15}) {
    s.game_type=static_cast<uint16_t>(type);
    require(std::strcmp(session_rejection(s,true),"unsupported_game_type")==0);
    require(!supported_session(s,true));
  }
  s.game_type=2;require(session_rejection(s,true)==nullptr);
  require(std::strcmp(session_rejection(s,false),"multiplayer_not_enabled")==0);
  // Every local slot and 1..7 opponents, including non-contiguous slots. People
  // may ally with each other; any alliance with the bot is unsupported.
  for(unsigned self=0;self<8;++self)for(unsigned opponents=1;opponents<=7;++opponents) {
    Session many;many.self=self;many.players[self]=2;many.multiplayer=true;
    for(unsigned n=1;n<=opponents;++n)many.players[(self+n)%8]=2;
    require(session_participant_count(many)==opponents+1);
    for(auto type:{2,3,15}) {
      many.game_type=static_cast<uint16_t>(type);
      require(supported_session(many,true,true));
      require(!supported_session(many,false,true));
      if(opponents>1)require(!supported_session(many,true,false));
    }
    if(opponents>1) {
      const unsigned a=(self+1)%8,b=(self+2)%8;
      many.alliances[a][b]=many.alliances[b][a]=1;
      require(supported_session(many,true,true));
    }
    const unsigned opponent=(self+1)%8;
    many.alliances[self][opponent]=1;
    require(std::strcmp(session_rejection(many,true,true),"local_allies_not_supported")==0);
    many.alliances[self][opponent]=0;many.alliances[opponent][self]=1;
    require(!supported_session(many,true,true));
    many.alliances[opponent][self]=0;
    many.replay=true;require(!supported_session(many,true,true));many.replay=false;
    for(auto type:{10,11,12,13}) {many.game_type=static_cast<uint16_t>(type);require(!supported_session(many,true,true));}
    many.game_type=2;many.multiplayer=false;many.custom_singleplayer=true;
    require(!supported_session(many,false,true));
    for(unsigned i=0;i<8;++i)if(i!=self && many.players[i]==2)many.players[i]=1;
    require(supported_session(many,false,true));
  }
  Session alone;alone.game_type=15;alone.self=5;alone.players[5]=2;alone.multiplayer=true;
  require(std::strcmp(session_rejection(alone,true,true),"requires_opponent")==0);
  alone.players[1]=3;alone.alliances[5][1]=1;
  require(!session_has_local_ally(alone)); // Nonparticipant does not block play.
  std::puts("default 1v1 regressions and challenge 1v1..1v7, every slot, enemy teams, allied bot, UMS, shared-control teams, offline and multiplayer checks passed");
}
