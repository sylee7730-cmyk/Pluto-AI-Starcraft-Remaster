#include "messages.h"
#include <Windows.h>
#include <cstdio>
#include <stdexcept>

using Lines=std::vector<std::string>;
static void require(bool ok,const char* label) {
  if(!ok)throw std::runtime_error(label);
}
static void check_utf8(const Lines& lines) {
  for(const auto& line:lines) {
    require(!line.empty(),"Empty display line");
    require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,line.data(),
      static_cast<int>(line.size()),nullptr,0)>0,"Invalid UTF-8 display text");
  }
}
static void compare_wrapped(const std::string& first,const std::string& second) {
  const auto full=pluto_messages_ko(first+second);
  auto wrapped=pluto_messages_ko(first);
  const auto tail=pluto_messages_ko(second);wrapped.insert(wrapped.end(),tail.begin(),tail.end());
  require(full==wrapped,"Wrapped and complete warnings differ");check_utf8(full);
  for(const auto& line:full)require(line.find(u8"플루토")!=std::string::npos ||
    line.find("ms")!=std::string::npos || line.find(u8"프레임")!=std::string::npos ||
    line.find(u8"경기")!=std::string::npos || line.find(u8"방의")!=std::string::npos,
    "Untranslated warning fragment");
}
int main() {
  // Values and line boundaries captured from actual offline and Battle.net logs.
  for(const int ms:{86,89,90,109,122,127,141,300,812}) {
    const auto first="Pluto: this machine can't keep up (inference ~"+std::to_string(ms)+" ms/step, ";
    const auto result=pluto_messages_ko(first);
    require(result==Lines{u8"플루토: 추론 속도가 부족합니다 (계산당 약 "+std::to_string(ms)+u8"ms)."},
      "Inference time missing or mistranslated");
    for(const auto frames:{"2.0","2.1","2.2"})
      compare_wrapped(first,std::string("decisions ~")+frames+" frames late) - gameplay may be degraded");
    for(const int stall:{43,48,51,80,258})
      compare_wrapped(first,"stalling the game ~"+std::to_string(stall)+" ms per decision) - sorry about the pace");
  }
  require(pluto_messages_ko("decisions ~2.1 frames late) - gameplay may be degraded")==
    Lines{u8"판단이 약 2.1프레임 늦어져 플레이 성능이 떨어질 수 있습니다."},"Lost decimal frames");
  require(pluto_messages_ko("stalling the game ~258 ms per decision) - sorry about the pace")==
    Lines{u8"판단마다 게임이 약 258ms 대기하여 진행이 느려질 수 있습니다."},"Lost stall time");
  for(const int delay:{3,5,6}) {
    const auto first="Pluto: action latency is "+std::to_string(delay)+" frames here, trained for 4 (lobby ";
    compare_wrapped(first,"turn rate / latency setting) - playing on, micro will be off");
    require(pluto_messages_ko(first)==Lines{u8"플루토: 명령 적용 지연 "+std::to_string(delay)+u8"프레임 (학습 기준 4프레임)."},
      "Measured or training latency lost");
  }
  const auto online=pluto_messages_ko("Pluto online (md07x02_cog2026_2578600_int8mv). gl hf!");
  require(online==Lines{u8"플루토: 준비 완료. 즐거운 경기 되세요!",u8"모델: md07x02_cog2026_2578600_int8mv"},"Model ID changed");
  check_utf8(online);
  const auto colored=pluto_messages_ko("\x03Pluto online (model). gl hf!");
  require(colored.size()==2 && colored[0][0]=='\x03' && colored[1][0]=='\x03',"Color control lost");
  for(const auto text:{"Unknown future message 17ms  ","",u8"이미 한글인 안내", "Pluto: this machine can't keep up (inference ~12 ms/step, new detail"})
    require(pluto_messages_ko(text)==Lines{text},"Unknown text was changed or lost");
  require(pluto_messages_ko("Pluto: quitting in 10 seconds")==Lines{u8"플루토: 10초 후 게임을 종료합니다."},"Shutdown countdown changed");
  require(pluto_messages_ko("Pluto: engine failed to start: error 123 C:/model.bin")==
    Lines{u8"플루토: 추론 엔진을 시작하지 못했습니다.","error 123 C:/model.bin"},"Engine diagnostic lost");

  Session s;s.self=0;s.players[0]=2;s.players[1]=2;s.multiplayer=true;
  for(const uint16_t type:{10,3,15}) {
    s.game_type=type;
    const auto lines=session_messages_ko(s,session_rejection(s,true));
    require(lines.size()==2 && lines[1]==u8"방을 밀리 (Melee), 플레이어 2명으로 만들어 주세요.","Missing game mode remedy");
    check_utf8(lines);
  }
  s.game_type=10;
  require(session_messages_ko(s,"unsupported_game_type")[0].find(u8"유즈맵")!=std::string::npos,"Wrong UMS name");
  for(const auto reason:{"requires_two_players","multiplayer_not_enabled","replay","local_observer",
    "local_slot_not_player","requires_custom_game","offline_requires_one_computer","future_reason"})
    check_utf8(session_messages_ko(s,reason));
  require(session_messages_ko(s,"multiplayer_not_enabled")[1].find("Start Pluto Multiplayer.cmd")!=std::string::npos,
    "Missing launcher name");
  check_utf8(session_messages_ko(s,"unsupported_game_type",true));
  require(session_messages_ko(s,"unsupported_game_type",true)[1].find(u8"탑 대 바텀")!=std::string::npos,
    "Missing challenge game mode remedy");
  for(const auto reason:{"local_allies_not_supported","requires_opponent","offline_requires_computers"})
    check_utf8(session_messages_ko(s,reason,true));

  require(pluto_overlay_text_ko("F1500 win=0.731")==u8"F1500 승률=0.731","Win estimate changed");
  require(pluto_overlay_text_ko("F0 win=n/a")==u8"F0 승률=계산 중","Unavailable win estimate misrepresented");
  require(pluto_overlay_text_ko("Other label")=="Other label","Unknown overlay text changed");
  check_utf8({pluto_overlay_text_ko("F1500 win=0.731")});
  std::puts("Korean UTF-8, observed split warnings, numeric values, model IDs, session remedies and unknown text checks passed");
}
