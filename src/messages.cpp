#include "messages.h"
#include <cstdlib>
#include <cstdio>
#include <regex>

namespace {
using Lines=std::vector<std::string>;

Lines latency_explanation() {
  return {u8"방의 턴 레이트 / 지연 설정을 확인하세요.",
    u8"경기는 계속하지만 세밀한 조작이 어긋날 수 있습니다."};
}
Lines slow_explanation(const std::string& text) {
  static const std::regex late(R"(^decisions ~([0-9]+\.[0-9]+) frames late\) - gameplay may be degraded$)");
  static const std::regex stall(R"(^stalling the game ~([0-9]+) ms per decision\) - sorry about the pace$)");
  std::smatch match;
  if(std::regex_match(text,match,late))
    return {u8"판단이 약 "+match[1].str()+u8"프레임 늦어져 플레이 성능이 떨어질 수 있습니다."};
  if(std::regex_match(text,match,stall))
    return {u8"판단마다 게임이 약 "+match[1].str()+u8"ms 대기하여 진행이 느려질 수 있습니다."};
  return {};
}
Lines translate(const std::string& text) {
  static const std::regex online(R"(^Pluto online \(([^\r\n]*)\)\. gl hf!$)");
  // Pluto wraps warnings before sending BWAPI text commands. Accept both the
  // complete format and the exact fragments seen in the pinned binary's logs.
  static const std::regex latency(R"(^Pluto: action latency is ([0-9]+) frames here, trained for ([0-9]+) \(lobby( turn rate / latency setting\) - playing on, micro will be off)?$)");
  static const std::regex slow(R"(^Pluto: this machine can't keep up \(inference ~([0-9]+) ms/step, *(.*)$)");
  std::smatch match;
  if(std::regex_match(text,match,online))
    return {u8"플루토: 준비 완료. 즐거운 경기 되세요!",u8"모델: "+match[1].str()};
  if(std::regex_match(text,match,latency)) {
    Lines lines{u8"플루토: 명령 적용 지연 "+match[1].str()+u8"프레임 (학습 기준 "+match[2].str()+u8"프레임)."};
    if(match[3].matched) {
      const auto more=latency_explanation();lines.insert(lines.end(),more.begin(),more.end());
    }
    return lines;
  }
  if(text=="turn rate / latency setting) - playing on, micro will be off")return latency_explanation();
  if(std::regex_match(text,match,slow)) {
    auto more=slow_explanation(match[2].str());
    if(match[2].length()==0 || !more.empty()) {
      Lines lines{u8"플루토: 추론 속도가 부족합니다 (계산당 약 "+match[1].str()+u8"ms)."};
      lines.insert(lines.end(),more.begin(),more.end());return lines;
    }
  }
  if(auto lines=slow_explanation(text);!lines.empty())return lines;
  if(text=="Pluto: quitting in 10 seconds")return {u8"플루토: 10초 후 게임을 종료합니다."};
  if(text=="gg")return {u8"플루토: 수고하셨습니다."};
  constexpr std::string_view startup_error="Pluto: engine failed to start: ";
  if(text.compare(0,startup_error.size(),startup_error)==0)
    return {u8"플루토: 추론 엔진을 시작하지 못했습니다.",text.substr(startup_error.size())};
  return {};
}
std::string game_type_ko(uint16_t type) {
  switch(type) {
    case 2:return u8"밀리 (Melee)";
    case 3:return u8"자유 대전 (FFA)";
    case 4:return u8"일대일 (One on One)";
    case 10:return u8"유즈맵 (Use Map Settings)";
    case 15:return u8"탑 대 바텀 (Top vs Bottom)";
    default:return u8"기타 ("+std::to_string(type)+")";
  }
}
}

std::vector<std::string> pluto_messages_ko(std::string_view text) {
  // Retain BWAPI's leading color controls on every translated display line.
  size_t first=0;
  while(first<text.size() && static_cast<unsigned char>(text[first])<32 &&
    text[first]!='\n' && text[first]!='\r' && text[first]!='\t')++first;
  const std::string prefix(text.substr(0,first));
  std::string body(text.substr(first));
  while(!body.empty() && (body.back()==' ' || body.back()=='\r' || body.back()=='\n'))body.pop_back();
  auto lines=translate(body);
  if(lines.empty())return {std::string(text)};
  for(auto& line:lines)line.insert(0,prefix);
  return lines;
}

std::vector<std::string> session_messages_ko(const Session& session,std::string_view reason,bool challenge) {
  if(reason=="unsupported_game_type")
    return {u8"플루토 시작 불가: 현재 게임 방식은 "+game_type_ko(session.game_type)+u8"입니다.",
      challenge?u8"밀리 / 자유 대전 / 탑 대 바텀에서 플루토를 혼자 한 팀에 두세요.":
        u8"방을 밀리 (Melee), 플레이어 2명으로 만들어 주세요."};
  if(reason=="requires_two_players")
    return {u8"플루토 시작 불가: 플레이어가 정확히 2명인 밀리 경기만 지원합니다."};
  if(reason=="multiplayer_not_enabled")
    return {u8"플루토 시작 불가: 멀티플레이 기능이 꺼져 있습니다.",
      u8"게임 종료 후 Start Pluto Multiplayer.cmd로 실행하세요."};
  if(reason=="replay")return {u8"플루토: 리플레이에서는 유닛을 조종하지 않습니다."};
  if(reason=="local_observer" || reason=="local_slot_not_player")
    return {u8"플루토 시작 불가: 내 자리가 플레이어 슬롯이어야 합니다."};
  if(reason=="requires_custom_game")
    return {u8"플루토 시작 불가: 사용자 지정 밀리 (Melee) 경기를 선택하세요."};
  if(reason=="offline_requires_one_computer")
    return {u8"플루토 시작 불가: 오프라인 상대는 컴퓨터 1명이어야 합니다."};
  if(reason=="requires_opponent")return {u8"플루토 시작 불가: 상대 플레이어가 1명 이상 필요합니다."};
  if(reason=="local_allies_not_supported")
    return {u8"플루토 시작 불가: 플루토는 혼자 한 팀이어야 합니다.",
      u8"사람들은 반대 팀에 배치하세요. 플루토와의 동맹은 지원하지 않습니다."};
  if(reason=="offline_requires_computers")
    return {u8"플루토 시작 불가: 오프라인 상대들은 모두 컴퓨터여야 합니다."};
  return {u8"플루토 시작 불가: 지원하지 않는 경기 설정입니다.",u8"진단 코드: "+std::string(reason)};
}

// Pluto's win value runs from -1 (certain loss) to +1 (certain win); its own resign rule
// fires at -0.95, which a 0..1 scale could never reach. Probability = (value + 1) / 2.
std::string session_reason_ko(std::string_view reason) {
  struct Entry { const char* key; const char* text; };
  static const Entry table[]={
    {"replay",u8"리플레이"},{"local_observer",u8"관전 중"},{"local_slot_not_player",u8"내 슬롯이 플레이어가 아님"},
    {"unsupported_game_type",u8"게임 유형이 모드와 다름 (예: 팀 멜리)"},{"requires_two_players",u8"1대1이 아닌 구성"},
    {"requires_opponent",u8"상대가 없음"},{"local_allies_not_supported",u8"플루토와 같은 편 플레이어가 있음"},
    {"multiplayer_not_enabled",u8"온라인 게임인데 오프라인 모드로 실행"},{"requires_custom_game",u8"커스텀 게임이 아님"},
    {"offline_requires_one_computer",u8"컴퓨터 상대가 1명이 아님"},{"offline_requires_computers",u8"컴퓨터 상대 구성이 모드와 다름"},
  };
  for(const auto& entry:table)if(reason==entry.key)return entry.text;
  return u8"모드와 다른 구성";
}
double win_probability_percent(double value) {
  if(value<-1.0)value=-1.0;
  if(value>1.0)value=1.0;
  return (value+1.0)*50.0;
}
// "0.5" -> "75.0%", "-0.5" -> "25.0%", "n/a" -> "계산 중".
std::string win_percent_ko(std::string_view value) {
  if(value=="n/a")return u8"계산 중";
  char buffer[16];
  std::snprintf(buffer,sizeof(buffer),"%.1f%%",win_probability_percent(std::atof(std::string(value).c_str())));
  return buffer;
}
bool parse_win_text(std::string_view text,int& frame,double& value,bool& unavailable) {
  static const std::regex win(R"(^F([0-9]+) win=(-?[0-9]+\.[0-9]+|n/a)$)");
  std::smatch match;const std::string source(text);
  if(!std::regex_match(source,match,win))return false;
  frame=std::atoi(match[1].str().c_str());
  unavailable=match[2].str()=="n/a";
  value=unavailable?0.0:std::atof(match[2].str().c_str());
  return true;
}
std::string pluto_overlay_text_ko(std::string_view text) {
  static const std::regex win(R"(^F([0-9]+) win=(-?[0-9]+\.[0-9]+|n/a)$)");
  std::smatch match;const std::string value(text);
  if(!std::regex_match(value,match,win))return value;
  return "F"+match[1].str()+u8" 승률 "+win_percent_ko(match[2].str());
}
