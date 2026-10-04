#include <atomic>
#include <chrono>
#include <Windows.h>
#include <MinHook.h>
#include <BWAPI.h>
#include <BWAPI/Client/GameImpl.h>
#include "scr_profile_13515_x86.h"
#include "scr_layout.h"
#include "snapshot.h"
#include "legacy_view.h"
#include "file_hash.h"
#include "overlay.h"
#include "session.h"
#include "session_reader.h"
#include "messages.h"
#include <cstdio>
#include <share.h>
#include <intrin.h>
#include <filesystem>
#include <memory>
#include <exception>
#include <stdexcept>

namespace {
HMODULE bridge_module;
FILE* log_file;
DWORD (WINAPI* original_tick)();
void (__cdecl* original_send)(const uint8_t*,size_t)=nullptr;
std::unique_ptr<Snapshot> snapshot;
std::unique_ptr<LegacyView> legacy;
std::unique_ptr<BWAPI::GameImpl> api_game;
HMODULE bot_module;
BWAPI::AIModule* bot;
wchar_t bot_path[32768]{};
int last_frame=-1;
bool failed=false;
bool match_ended=false;
bool speed_changed=false;
bool unsupported_session_reported=false;
bool allow_multiplayer=false;
bool challenge_mode=false;
bool allow_allies=false;
bool ally_as_own=false;          // Allied play: show allies as Pluto's own forces (default) instead of hiding them.
bool ally_stasis=false;          // With ally_as_own: present allied units as held in stasis.
TeamStatsMode team_stats=TeamStatsMode::off;  // Allied play: Pluto's statistics include (part of) its allies'.
// Win-graph diagnostics: how often Pluto handed shapes to the bridge in this match.
unsigned graph_frames=0,graph_shapes=0;
// Shown in bridge.log ("version") and at the start of every match, so it is clear which build runs.
constexpr char bridge_version[]="v18";
bool pluto_noresign=false;        // The loaded pluto.dll carries the no-resign patch (never sends gg/leave).
// Manual control: a hotkey toggles whether Pluto's commands reach the game. Pluto controls the
// same player the human does and issues orders every 6 frames, so manual orders are otherwise
// overwritten within a fraction of a second. RegisterHotKey runs on its own thread; the game
// thread notices the press count change and acts on it.
UINT pause_vk=VK_F9;  // bridge.ini pause_key=<virtual-key code>; 0 disables the hotkey.
std::atomic<unsigned> pause_presses{0};
std::atomic<bool> pause_hotkey_failed{false};
unsigned pause_seen=0;
bool manual_pause=false,pause_thread_started=false;
bool any_game=false;  // bridge.ini any_game=1: play whatever game starts, regardless of the selected mode.
bool mode_multiplayer=false,mode_challenge=false,mode_allies=false;  // The mode as selected, before any_game widens it.
// Frame timing, logged every 240 frames while a match runs: gap = time between two game frames as
// the bridge sees them, bot = time spent inside Pluto's onFrame. Separates a slow game from slow
// command execution, and shows whether pausing Pluto changes either.
struct FrameStats {
  std::chrono::steady_clock::time_point last{};bool have_last=false;
  double gap_sum=0,gap_max=0,bot_sum=0,bot_max=0;unsigned count=0;
};
FrameStats frame_stats;
bool latency_reported=false;
// Manual-control lock (bridge.ini manual_lock_seconds, 0 = off): units the human orders are withheld
// from Pluto. It needs a hook on the game's own command queue to tell the human's packets from Pluto's.
int manual_lock_seconds=20;
bool manual_lock_hooked=false,manual_lock_announced=false;
// Online, Pluto's commands are sent the moment it issues them. The one-frame hold is calibrated for offline play
// (see LegacyView::drain); online the room's own delay is added on top, which pushes the delay Pluto measures from
// its trained 4 frames to 5. bridge.ini latency_hold_online=1 restores the hold.
bool latency_hold_online=false;
// Decided when a session is accepted, applied once load_bot() has created the LegacyView: the view
// does not exist yet at that point on the first match, so it must not be touched there.
bool match_hold_one_frame=true;  // Once per match: the lobby's turn rate and user delay, shown on screen and logged.
// Name shown to the player for the pause hotkey.
std::string pause_key_name() {
  if(pause_vk>=VK_F1 && pause_vk<=VK_F12)return "F"+std::to_string(pause_vk-VK_F1+1);
  if(pause_vk==VK_SCROLL)return "Scroll Lock";
  return u8"설정한 단축키";
}
DWORD WINAPI pause_hotkey_thread(LPVOID) {
  if(!RegisterHotKey(nullptr,1,MOD_NOREPEAT,pause_vk)){pause_hotkey_failed=true;return 1;}
  MSG message;
  while(GetMessageW(&message,nullptr,0,0)>0)if(message.message==WM_HOTKEY && message.wParam==1)++pause_presses;
  UnregisterHotKey(nullptr,1);return 0;
}
unsigned submitted_packets=0;
DWORD game_thread=0;
int speed_ms=0;
std::array<uint32_t,7> saved_speed{},saved_alt_speed{};
thread_local bool in_callback=false;

void print_local(const std::vector<std::string>& lines) {
  // SCR consumes UTF-8 here. Display only on this client, without network chat.
  for(const auto& line:lines)
    reinterpret_cast<void (__cdecl*)(const char*,uint32_t,uint32_t)>(scr::print_text())(line.c_str(),16,0);
}

void set_speed(int value) {
  if(value<0 || scr::is_multiplayer())return;
  if(!speed_changed) {
    std::memcpy(saved_speed.data(),reinterpret_cast<void*>(scr::addr(0xfc4d7c)),28);
    std::memcpy(saved_alt_speed.data(),reinterpret_cast<void*>(scr::addr(0xfc4dac)),28);
    speed_changed=true;
  }
  for(unsigned n=0;n<7;++n) {
    *reinterpret_cast<uint32_t*>(scr::addr(0xfc4d7c)+n*4)=static_cast<uint32_t>(value);
    *reinterpret_cast<uint32_t*>(scr::addr(0xfc4dac)+n*4)=static_cast<uint32_t>(value)*3;
  }
}
void restore_speed() {
  if(!speed_changed)return;
  std::memcpy(reinterpret_cast<void*>(scr::addr(0xfc4d7c)),saved_speed.data(),28);
  std::memcpy(reinterpret_cast<void*>(scr::addr(0xfc4dac)),saved_alt_speed.data(),28);
  speed_changed=false;
}
void finish_match() {
  if(last_frame<0 || match_ended)return;
  match_ended=true;
  std::fprintf(log_file,"{\"graph_diag\":true,\"stage\":\"match_end\",\"frames_with_shapes\":%u,\"shapes\":%u,\"overlay\":\"%s\"}\n",
    graph_frames,graph_shapes,overlay_status().c_str());
  graph_frames=graph_shapes=0;
  const bool won=scr::game() && scr::read<uint8_t>(scr::game()+58896+snapshot->data->self)==3;
  if(bot)bot->onEnd(won);
  clear_overlay();
  restore_speed();
  std::fprintf(log_file,"{\"stage\":\"match_end\",\"frame\":%d,\"winner\":%s,\"packets_submitted\":%u}\n",last_frame,won?"true":"false",submitted_packets);
  std::fflush(log_file);
}
void load_bot() {
  if(!bot_path[0])return;
  BWAPI::BWAPIClient.data=snapshot->data.get();
  api_game=std::make_unique<BWAPI::GameImpl>(snapshot->data.get());
  BWAPI::BroodwarPtr=api_game.get();
  api_game->onMatchStart();
  if(!bot_module) {
    const auto bot_hash=file_sha256(bot_path);
    if(bot_hash!=pluto_sha256 && bot_hash!=pluto_noresign_sha256)throw std::runtime_error("Unsupported Pluto DLL SHA-256; expected CoG 2026 release");
    pluto_noresign=bot_hash==pluto_noresign_sha256;
    std::fprintf(log_file,"{\"stage\":\"pluto_dll\",\"variant\":\"%s\"}\n",pluto_noresign?"no_resign_patch":"original");
    bot_module=LoadLibraryExW(bot_path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!bot_module)throw std::runtime_error("Cannot load configured Pluto DLL");
    legacy=std::make_unique<LegacyView>();legacy->configure_manual_lock(manual_lock_hooked && manual_lock_seconds>0,manual_lock_seconds*24);legacy->hide_allies=allow_allies && !ally_as_own;legacy->ally_as_own=ally_as_own;legacy->ally_stasis=ally_stasis;legacy->team_stats=allow_allies?team_stats:TeamStatsMode::off;legacy->update();legacy->bind(bot_module);
  }else{legacy->reset();legacy->update();}
  legacy->hold_one_frame=match_hold_one_frame;
  const auto init=reinterpret_cast<void (__cdecl*)(BWAPI::Game*)>(GetProcAddress(bot_module,"gameInit"));
  const auto create=reinterpret_cast<BWAPI::AIModule* (__cdecl*)()>(GetProcAddress(bot_module,"newAIModule"));
  if(!init || !create)throw std::runtime_error("Missing BWAPI exports");
  if(pause_vk && !pause_thread_started) {
    if(auto thread=CreateThread(nullptr,0,pause_hotkey_thread,nullptr,0,nullptr)){pause_thread_started=true;CloseHandle(thread);}
  }
  init(api_game.get());bot=create();
  if(!bot)throw std::runtime_error("Pluto returned null");
  std::fprintf(log_file,"{\"stage\":\"bot_on_start_begin\"}\n");std::fflush(log_file);
  legacy->begin_frame(log_file,0);bot->onStart();
  set_speed(speed_ms);
  std::fprintf(log_file,"{\"stage\":\"bot_on_start_complete\"}\n");std::fflush(log_file);
}
void run_bot(bool first) {
  if(!bot)return;
  auto& d=*snapshot->data;
  if(pause_hotkey_failed.exchange(false))
    print_local({u8"일시정지 단축키를 등록하지 못했습니다 (다른 프로그램이 같은 키를 쓰는 중일 수 있습니다)."});
  if(const unsigned presses=pause_presses.load();presses!=pause_seen) {
    if((presses-pause_seen)%2==1)manual_pause=!manual_pause;  // Two quick presses cancel out.
    pause_seen=presses;
    std::fprintf(log_file,"{\"stage\":\"manual_pause\",\"on\":%s,\"frame\":%d}\n",manual_pause?"true":"false",d.frameCount);
    const std::string key=pause_key_name();
    if(manual_pause)print_local({u8"플루토 일시정지: 지금부터 직접 조종할 수 있습니다.",key+u8" 키로 다시 플루토에게 맡깁니다."});
    else print_local({u8"플루토 재개: 다시 플루토가 조종합니다."});
  }
  if(!latency_reported && latency_info.valid) {
    latency_reported=true;
    std::fprintf(log_file,"{\"stage\":\"latency\",\"turn_rate\":%u,\"user_delay\":%u,\"latency_frames\":%d,\"frame\":%d}\n",
      latency_info.turn_rate,latency_info.user_delay,latency_info.frames,d.frameCount);
    print_local({u8"현재 방 설정: 턴 레이트 "+std::to_string(latency_info.turn_rate)+u8", 지연 설정값 "+std::to_string(latency_info.user_delay)+
      u8" → 추정 지연 "+std::to_string(latency_info.frames)+u8"프레임 (플루토 학습 기준 4프레임, 플루토가 실제로 잰 값은 따로 경고로 알려줍니다)"});
  }
  legacy->hold_commands=manual_pause;
  legacy->update();
  legacy->begin_frame(log_file,d.frameCount);
  if(!first)api_game->onMatchFrame();
  if(!first)for(const auto& e:api_game->getEvents()) {
    switch(e.getType()) {
      case BWAPI::EventType::UnitDiscover:bot->onUnitDiscover(e.getUnit());break;
      case BWAPI::EventType::UnitEvade:bot->onUnitEvade(e.getUnit());break;
      case BWAPI::EventType::UnitShow:bot->onUnitShow(e.getUnit());break;
      case BWAPI::EventType::UnitHide:bot->onUnitHide(e.getUnit());break;
      case BWAPI::EventType::UnitCreate:bot->onUnitCreate(e.getUnit());break;
      case BWAPI::EventType::UnitDestroy:bot->onUnitDestroy(e.getUnit());break;
      case BWAPI::EventType::UnitMorph:bot->onUnitMorph(e.getUnit());break;
      case BWAPI::EventType::UnitRenegade:bot->onUnitRenegade(e.getUnit());break;
      case BWAPI::EventType::UnitComplete:bot->onUnitComplete(e.getUnit());break;
      default:break;
    }
  }
  const auto lock_report=legacy->manual_lock_report();
  if(manual_lock_hooked && !manual_lock_announced && lock_report.locks_created>0) {
    manual_lock_announced=true;
    print_local({u8"수동 조종 감지: 내가 명령한 유닛은 일이 끝날 때까지 플루토가 건드리지 않습니다."});
  }
  const auto bot_started=std::chrono::steady_clock::now();
  bot->onFrame();
  {
    const auto bot_finished=std::chrono::steady_clock::now();
    using Millis=std::chrono::duration<double,std::milli>;
    const double bot_ms=Millis(bot_finished-bot_started).count();
    frame_stats.bot_sum+=bot_ms;frame_stats.bot_max=std::max(frame_stats.bot_max,bot_ms);
    if(frame_stats.have_last) {
      const double gap_ms=Millis(bot_started-frame_stats.last).count();
      frame_stats.gap_sum+=gap_ms;frame_stats.gap_max=std::max(frame_stats.gap_max,gap_ms);
    }
    frame_stats.last=bot_started;frame_stats.have_last=true;
    if(++frame_stats.count>=240) {
      std::fprintf(log_file,"{\"stage\":\"frame_stats\",\"frame\":%d,\"paused\":%s,\"gap_avg_ms\":%.1f,\"gap_max_ms\":%.1f,\"bot_avg_ms\":%.1f,\"bot_max_ms\":%.1f,\"human_packets\":%u,\"human_orders\":%u,\"locks_created\":%u,\"locked_now\":%u,\"trimmed\":%u}\n",
        d.frameCount,manual_pause?"true":"false",frame_stats.gap_sum/frame_stats.count,frame_stats.gap_max,
        frame_stats.bot_sum/frame_stats.count,frame_stats.bot_max,
        lock_report.packets,lock_report.orders,lock_report.locks_created,lock_report.locked_now,lock_report.units_trimmed);
      std::fflush(log_file);
      frame_stats=FrameStats{};
    }
  }
  submitted_packets+=legacy->drain(log_file,d.frameCount);
  for(int i=0;i<d.commandCount;++i) {
    auto& c=d.commands[i];
    if((c.type==BWAPIC::CommandType::Printf || c.type==BWAPIC::CommandType::SendText) && c.value1>=0 && c.value1<d.stringCount) {
      std::fprintf(log_file,"bot_text: %s\n",d.strings[c.value1]);
      print_local(pluto_messages_ko(d.strings[c.value1]));
    }
    else std::fprintf(log_file,"{\"game_command\":%d,\"v1\":%d,\"v2\":%d}\n",c.type,c.value1,c.value2);
    if(c.type==BWAPIC::CommandType::EnableFlag && c.value1>=0 && c.value1<BWAPI::Flag::Max)d.flags[c.value1]=true;
    // The launcher's explicit speed policy takes priority over Pluto's onStart
    // request to uncap. A negative setting leaves the game's own speed intact.
    if(c.type==BWAPIC::CommandType::SetLocalSpeed)set_speed(speed_ms);
    if(c.type==BWAPIC::CommandType::LeaveGame) {
      // Multiplayer departure must use the game's normal loop teardown. Do not
      // change simulation victory state locally, which peers would not observe.
      if(!scr::is_multiplayer())*reinterpret_cast<uint8_t*>(scr::game()+58896+d.self)=1;
      *reinterpret_cast<uint8_t*>(scr::addr(0x121758d))=0;
      finish_match();
    }
  }
  for(int i=0;i<d.unitCommandCount;++i) {
    auto& c=d.unitCommands[i];
    std::fprintf(log_file,"{\"decision_frame\":%d,\"command\":%d,\"unit\":%d,\"target\":%d,\"x\":%d,\"y\":%d,\"extra\":%d,\"executed\":false}\n",d.frameCount,c.type.getID(),c.unitIndex,c.targetIndex,c.x,c.y,c.extra);
  }
  if(d.shapeCount>0){++graph_frames;graph_shapes+=static_cast<unsigned>(d.shapeCount);}
  if(d.frameCount>0 && d.frameCount%2400==0)
    std::fprintf(log_file,"{\"graph_diag\":true,\"frame\":%d,\"frames_with_shapes\":%u,\"shapes\":%u,\"overlay\":\"%s\",\"chart\":\"%s\"}\n",
      d.frameCount,graph_frames,graph_shapes,overlay_status().c_str(),overlay_chart_summary().c_str());
  update_overlay(d);
  d.commandCount=d.unitCommandCount=d.shapeCount=d.stringCount=0;
  std::fflush(log_file);
}

void update_frame() {
  if((scr::is_multiplayer() && !allow_multiplayer) || scr::is_replay())return;
  const auto g=scr::game();
  if(!g || scr::local_player_id()>=8)return;
  const auto frame=static_cast<int>(scr::read<uint32_t>(g+332));
  if(frame<last_frame || (match_ended && frame==0)) {
    finish_match();
    if(api_game)api_game->onMatchEnd();
    BWAPI::BroodwarPtr=nullptr;BWAPI::BWAPIClient.data=nullptr;
    api_game.reset();snapshot.reset();bot=nullptr;
    last_frame=-1;match_ended=false;submitted_packets=0;manual_pause=false;latency_reported=false;frame_stats=FrameStats{};manual_lock_announced=false;
  }
  if(match_ended)return;
  if(frame==last_frame)return;
  const auto session=read_session(g);
  if(last_frame>=0 && !allow_allies && session_has_local_ally(session)) {
    // A new alliance cannot be represented by the original model. Stop this
    // match's AI and discard commands queued before the alliance changed.
    if(legacy)legacy->discard_pending_turns();
    print_local({u8"플루토 조종 중단: 플루토와 동맹인 플레이어가 생겼습니다.",
      u8"다음 경기는 플루토를 혼자 한 팀에 배치해 주세요."});
    std::fprintf(log_file,"{\"stage\":\"ai_stopped\",\"reason\":\"local_alliance_changed\",\"frame\":%d}\n",frame);
    finish_match();return;
  }
  if(last_frame<0) {
    if(const auto reason=session_rejection(session,allow_multiplayer,challenge_mode,allow_allies,any_game)) {
      if(!unsupported_session_reported) {
        std::fprintf(log_file,"{\"stage\":\"session_skipped\",\"reason\":\"%s\",\"game_type\":%u,\"game_type_name\":\"%s\",\"self\":%u,\"multiplayer\":%s,\"multiplayer_enabled\":%s,\"player_types\":[",
          reason,session.game_type,session_game_type(session.game_type),session.self,
          session.multiplayer?"true":"false",allow_multiplayer?"true":"false");
        for(unsigned i=0;i<8;++i)std::fprintf(log_file,"%s%u",i?",":"",session.players[i]);
        std::fprintf(log_file,"]}\n");
        std::fflush(log_file);unsupported_session_reported=true;
        print_local(session_messages_ko(session,reason,challenge_mode));
      }
      return;
    }
    unsupported_session_reported=false;
    std::fprintf(log_file,"{\"stage\":\"session_accepted\",\"multiplayer\":%s,\"self\":%u,\"game_type\":%u,\"participants\":%u,\"challenge\":%s}\n",
      session.multiplayer?"true":"false",session.self,session.game_type,session_participant_count(session),challenge_mode?"true":"false");
    // The one-frame hold stays offline (calibrated there); online it is dropped unless bridge.ini asks for it.
    match_hold_one_frame=!session.multiplayer || latency_hold_online;
    std::fprintf(log_file,"{\"stage\":\"command_hold\",\"online\":%s,\"hold_one_frame\":%s}\n",
      session.multiplayer?"true":"false",match_hold_one_frame?"true":"false");
    if(any_game) {
      if(const auto differs=session_rejection(session,mode_multiplayer,mode_challenge,mode_allies)) {
        std::fprintf(log_file,"{\"stage\":\"session_forced\",\"reason\":\"%s\"}\n",differs);std::fflush(log_file);
        print_local({u8"선택한 모드와 게임 구성이 달라도 플루토를 시작합니다.",u8"달랐던 점: "+session_reason_ko(differs)});
      }
    }
    if(pluto_noresign)print_local({u8"기권 코드 제거 패치가 적용된 Pluto입니다: 승률이 낮아도 gg를 치지 않고 끝까지 싸웁니다."});
    print_local({u8"플루토 팀전 브리지 "+std::string(bridge_version)});
    if(manual_lock_seconds>0) {
      if(manual_lock_hooked)print_local({u8"수동 명령 유닛 잠금 켜짐: 내가 명령한 유닛은 일이 끝날 때까지(최대 "+std::to_string(manual_lock_seconds)+u8"초) 플루토가 건드리지 않습니다."});
      else print_local({u8"수동 명령 유닛 잠금을 켜지 못했습니다 (게임 명령 훅 설치 실패). bridge.log 의 manual_lock_hook 줄을 확인하세요."});
    }
    if(pause_vk)print_local({u8"직접 조종하려면: "+pause_key_name()+u8" 키로 플루토를 일시정지/재개합니다."});
    // Mode notices describe the selected mode, or a game that really has allies / several opponents.
    // With "play anyway" the options are widened for every game, so a plain 1v1 stays quiet.
    const bool show_ally_notices=allow_allies && (mode_allies || session_has_local_ally(session));
    const bool show_many_notice=challenge_mode && (mode_challenge || session_participant_count(session)>2);
    if(show_ally_notices && team_stats==TeamStatsMode::army)print_local({u8"팀 전력 합산(전투 유닛): 플루토가 읽는 전투 유닛 수·킬 수에 아군 것을 더합니다 (실험)."});
    else if(show_ally_notices && team_stats==TeamStatsMode::kills)print_local({u8"팀 전력 합산(킬 수만): 플루토가 읽는 킬 수에 아군 것을 더합니다 (실험)."});
    else if(show_ally_notices && team_stats==TeamStatsMode::all)print_local({u8"팀 전력 합산(전부): 플루토가 읽는 모든 유닛·건물 수와 킬 수에 아군 것을 더합니다 (실험)."});
    if(show_ally_notices && ally_as_own && ally_stasis)print_local({u8"플루토 팀전 개발판: 아군 유닛을 플루토의 병력(행동 불가 상태)으로 인식합니다.",
      u8"아군에게는 명령이 전달되지 않습니다. 성능은 검증 전입니다."});
    else if(show_ally_notices && ally_as_own)print_local({u8"플루토 팀전 개발판: 아군 유닛을 플루토의 병력으로 인식합니다.",
      u8"아군에게는 명령이 전달되지 않습니다. 성능은 검증 전입니다."});
    else if(show_ally_notices)print_local({u8"플루토 팀전 개발판: 아군 유닛은 플루토에게 보이지 않습니다.",
      u8"플루토는 아군과 협력하지 못하고 혼자 판단합니다. 성능은 검증 전입니다."});
    else if(show_many_notice)print_local({u8"플루토 콘텐츠 실험 모드: 1 대 "+std::to_string(session_participant_count(session)-1)+u8" 경기입니다.",
      u8"1대1 모델을 사용하므로 다인전 전투력과 승률 예측은 검증 전입니다."});
  }
  if(!snapshot){snapshot=std::make_unique<Snapshot>();snapshot->hide_allies=allow_allies && !ally_as_own;snapshot->ally_as_own=ally_as_own;snapshot->ally_stasis=ally_stasis;snapshot->team_stats=allow_allies?team_stats:TeamStatsMode::off;}
  if(!snapshot->update(last_frame<0))return;
  if(last_frame<0)load_bot();
  run_bot(last_frame<0);
  auto& d=*snapshot->data;
  if(frame<3 || frame%24==0) {
    std::fprintf(log_file,"{\"frame\":%d,\"delta\":%d,\"map_width\":%d,\"map_height\":%d,\"self\":%d,\"race\":%d,\"minerals\":%d,\"initial_units\":%d,\"events\":%d,\"packets_submitted\":%u}\n",frame,frame-last_frame,d.mapWidth,d.mapHeight,d.self,d.players[d.self].race,d.players[d.self].minerals,d.initialUnitCount,d.eventCount,submitted_packets);
    std::fflush(log_file);
  }
  last_frame=frame;
}
void guarded_frame_cpp(bool frame) {
  try {
    if(last_frame>=0 && !scr::read<uint8_t>(scr::addr(0x121758d)))finish_match();
    if(frame)update_frame();
  }
  catch(const std::exception& e){failed=true;restore_speed();clear_overlay();std::fprintf(log_file,"C++ failure: %s\n",e.what());std::fflush(log_file);}
}
LONG diagnose(EXCEPTION_POINTERS* p) {
  failed=true;
  restore_speed();
  std::fprintf(log_file,"{\"failure\":\"bridge_exception\",\"code\":%lu,\"address\":\"%p\",\"pluto_rva\":%lu,\"access\":%lu,\"target\":\"%08lx\"}\n",p->ExceptionRecord->ExceptionCode,p->ExceptionRecord->ExceptionAddress,p->ContextRecord->Eip-reinterpret_cast<DWORD>(bot_module),p->ExceptionRecord->ExceptionInformation[0],p->ExceptionRecord->ExceptionInformation[1]);
  std::fflush(log_file);return EXCEPTION_EXECUTE_HANDLER;
}
void guarded_frame(bool frame) {
  __try {guarded_frame_cpp(frame);}
  __except(diagnose(GetExceptionInformation())){}
}
// Every command the game queues passes here; packets the bridge itself sends are told apart inside.
void __cdecl send_hook(const uint8_t* data,size_t size) {
  LegacyView::observe_human_packet(data,size);
  original_send(data,size);
}
DWORD WINAPI tick_hook() {
  const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
  // Verified call sites in step_game_logic: before its first frame and after each
  // completed simulation frame. Restrict interception to those two callers.
  const bool frame=caller==scr::addr(0x76e02f) || caller==scr::addr(0x77d112);
  if(!failed && !in_callback && (frame || (game_thread && game_thread==GetCurrentThreadId()))) {
    if(frame)game_thread=GetCurrentThreadId();
    in_callback=true;guarded_frame(frame);in_callback=false;
  }
  return original_tick();
}
DWORD WINAPI initialize(void*) {
  wchar_t own_path[32768]{};
  GetModuleFileNameW(bridge_module,own_path,32768);
  const auto directory=std::filesystem::path(own_path).parent_path();
  GetPrivateProfileStringW(L"pluto",L"module",L"",bot_path,32768,(directory/L"bridge.ini").c_str());
  speed_ms=GetPrivateProfileIntW(L"pluto",L"speed_ms",0,(directory/L"bridge.ini").c_str());
  allow_multiplayer=GetPrivateProfileIntW(L"pluto",L"multiplayer",0,(directory/L"bridge.ini").c_str())==1;
  challenge_mode=GetPrivateProfileIntW(L"pluto",L"challenge",0,(directory/L"bridge.ini").c_str())==1;
  // Allied play builds on the 1-vs-many admission rules, so it requires challenge mode.
  allow_allies=challenge_mode && GetPrivateProfileIntW(L"pluto",L"allies",0,(directory/L"bridge.ini").c_str())==1;
  // Play anyway: the selected mode is only a default. Keep it for the "differs from your mode" notice,
  // then accept online games, many-vs-many and allies whatever the mode was.
  mode_multiplayer=allow_multiplayer;mode_challenge=challenge_mode;mode_allies=allow_allies;
  any_game=GetPrivateProfileIntW(L"pluto",L"any_game",0,(directory/L"bridge.ini").c_str())==1;
  if(any_game)allow_multiplayer=challenge_mode=allow_allies=true;
  // ally_view: own (default) | stasis | hide
  wchar_t ally_view[32]{};
  GetPrivateProfileStringW(L"pluto",L"ally_view",L"own",ally_view,32,(directory/L"bridge.ini").c_str());
  ally_as_own=allow_allies && std::wcscmp(ally_view,L"hide")!=0;
  ally_stasis=ally_as_own && std::wcscmp(ally_view,L"stasis")==0;
  wchar_t team_stats_text[32]{};
  GetPrivateProfileStringW(L"pluto",L"team_stats",L"off",team_stats_text,32,(directory/L"bridge.ini").c_str());
  team_stats=allow_allies?parse_team_stats_mode(team_stats_text):TeamStatsMode::off;
  pause_vk=GetPrivateProfileIntW(L"pluto",L"pause_key",VK_F9,(directory/L"bridge.ini").c_str());
  if(pause_vk>255)pause_vk=0;
  latency_hold_online=GetPrivateProfileIntW(L"pluto",L"latency_hold_online",0,(directory/L"bridge.ini").c_str())==1;
  manual_lock_seconds=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"pluto",L"manual_lock_seconds",20,(directory/L"bridge.ini").c_str())),0,600);
  if(speed_ms>1000)speed_ms=1000;
  log_file=_wfsopen((directory/"bridge.log").c_str(),L"w",_SH_DENYNO);
  if(!log_file)return 1;
  std::fprintf(log_file,"{\"stage\":\"initializing\",\"version\":\"%s\",\"pid\":%lu,\"mode\":\"%s\",\"challenge\":%s,\"allies\":%s,\"ally_view\":\"%s\",\"team_stats\":\"%s\",\"any_game\":%s}\n",bridge_version,GetCurrentProcessId(),
    allow_allies?"allies_experimental":(challenge_mode?"challenge_experimental":(allow_multiplayer?"multiplayer_experimental":"offline_pluto")),
    challenge_mode?"true":"false",allow_allies?"true":"false",!allow_allies?"n/a":(!ally_as_own?"hide":(ally_stasis?"stasis":"own")),team_stats_mode_name(team_stats),any_game?"true":"false");std::fflush(log_file);
  try {
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    if(file_sha256(executable)!=scr_sha256)throw std::runtime_error("Unsupported StarCraft executable SHA-256");
  }catch(const std::exception& e){std::fprintf(log_file,"Initialization rejected: %s\n",e.what());std::fflush(log_file);return 2;}
  const auto target=scr::read<uintptr_t>(scr::addr(0xdd01c4));
  const auto expected=reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetTickCount"));
  std::fprintf(log_file,"{\"timing_import\":\"%p\",\"winapi\":\"%p\"}\n",reinterpret_cast<void*>(target),reinterpret_cast<void*>(expected));
  auto result=MH_Initialize();
  if(result==MH_OK)result=MH_CreateHook(reinterpret_cast<void*>(expected),&tick_hook,reinterpret_cast<void**>(&original_tick));
  if(result==MH_OK)result=MH_EnableHook(reinterpret_cast<void*>(expected));
  std::fprintf(log_file,"{\"stage\":\"hook_install\",\"result\":\"%s\"}\n",MH_StatusToString(result));std::fflush(log_file);
  if(result==MH_OK && manual_lock_seconds>0) {
    // A failure here only turns the manual-control lock off; the bridge itself keeps working.
    void* send_target=reinterpret_cast<void*>(scr::send_command());
    const auto created=MH_CreateHook(send_target,&send_hook,reinterpret_cast<void**>(&original_send));
    const auto hooked=created==MH_OK?MH_EnableHook(send_target):created;
    manual_lock_hooked=hooked==MH_OK;
    std::fprintf(log_file,"{\"stage\":\"manual_lock_hook\",\"result\":\"%s\",\"create\":\"%s\",\"seconds\":%d}\n",
      MH_StatusToString(hooked),MH_StatusToString(created),manual_lock_seconds);
    if(!manual_lock_hooked) {
      // Why was the game's code page refused? Logged so the next step rests on facts, not guesses.
      MEMORY_BASIC_INFORMATION page{};
      if(VirtualQuery(send_target,&page,sizeof(page))) {
        DWORD previous=0;
        const BOOL writable=VirtualProtect(send_target,16,PAGE_EXECUTE_READWRITE,&previous);
        const DWORD error=writable?0:GetLastError();
        if(writable){DWORD ignored=0;VirtualProtect(send_target,16,previous,&ignored);}
        std::fprintf(log_file,"{\"stage\":\"manual_lock_hook_diag\",\"target\":\"%p\",\"state\":%lu,\"protect\":%lu,\"alloc_protect\":%lu,\"type\":%lu,\"virtual_protect_ok\":%s,\"error\":%lu}\n",
          send_target,static_cast<unsigned long>(page.State),static_cast<unsigned long>(page.Protect),static_cast<unsigned long>(page.AllocationProtect),
          static_cast<unsigned long>(page.Type),writable?"true":"false",static_cast<unsigned long>(error));
      }
    }
    std::fflush(log_file);
  }
  return result==MH_OK?0:3;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
  if(reason==DLL_PROCESS_ATTACH){bridge_module=module;DisableThreadLibraryCalls(module);HANDLE thread=CreateThread(nullptr,0,initialize,nullptr,0,nullptr);if(!thread)return FALSE;CloseHandle(thread);}
  return TRUE;
}
