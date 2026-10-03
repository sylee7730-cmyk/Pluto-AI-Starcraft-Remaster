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
  // Allied play (opt-in): 3v3 Top vs Bottom with Pluto on the top team. The default
  // path must still reject it; opting in must keep every other restriction.
  Session team;team.self=0;team.game_type=15;team.multiplayer=true;
  for(unsigned i=0;i<6;++i)team.players[i]=2;
  for(unsigned a:{0u,1u,2u})for(unsigned b:{0u,1u,2u})team.alliances[a][b]=1;
  for(unsigned a:{3u,4u,5u})for(unsigned b:{3u,4u,5u})team.alliances[a][b]=1;
  require(session_has_local_ally(team));
  require(std::strcmp(session_rejection(team,true,true),"local_allies_not_supported")==0);
  require(std::strcmp(session_rejection(team,true,false,true),"unsupported_game_type")==0); // allies alone do not open Top vs Bottom
  require(session_rejection(team,true,true,true)==nullptr);
  require(!supported_session(team,false,true,true));                  // multiplayer still opt-in
  const AllyMask mask=make_ally_mask(team);
  require(!mask.hides(0));                                            // Pluto's own slot
  require(mask.hides(1) && mask.hides(2));                            // allies are withheld
  require(!mask.hides(3) && !mask.hides(4) && !mask.hides(5));        // enemies stay visible
  require(!mask.hides(6) && !mask.hides(7) && !mask.hides(8));        // empty slots / out of range
  for(auto type:{10,11,12,13}) {team.game_type=static_cast<uint16_t>(type);require(!supported_session(team,true,true,true));}
  team.game_type=15;team.replay=true;require(!supported_session(team,true,true,true));team.replay=false;
  team.self=9;require(!supported_session(team,true,true,true));require(!make_ally_mask(team).hides(1));team.self=0;
  // One-directional alliances count, matching the existing local-ally rule.
  Session oneway;oneway.self=2;oneway.players[2]=2;oneway.players[4]=2;oneway.alliances[4][2]=1;
  require(make_ally_mask(oneway).hides(4) && !make_ally_mask(oneway).hides(2));
  // A nonparticipant slot is never hidden even when its alliance flag is set.
  Session closed;closed.self=1;closed.players[1]=2;closed.players[5]=3;closed.alliances[1][5]=1;
  require(!make_ally_mask(closed).hides(5));
  // Allies mask is empty when nobody is allied (default games behave as before).
  Session plain;plain.self=3;plain.players[3]=2;plain.players[6]=2;
  for(unsigned i=0;i<8;++i)require(!make_ally_mask(plain).hides(i));
  std::puts("default 1v1 regressions and challenge 1v1..1v7, every slot, enemy teams, allied bot, UMS, shared-control teams, offline and multiplayer checks passed");
  std::puts("opt-in allied play: 3v3 admission, ally mask, one-way alliances, nonparticipants and unchanged restrictions passed");
}
