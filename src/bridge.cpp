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
bool team_resign=false;          // Allied play: refuse Pluto's resignation while any ally is still alive.
bool allow_resign=true;          // False ignores Pluto's own leave-game (auto-resign) requests.
bool resign_notice_shown=false;
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

bool living_ally_present() {
  const auto g=scr::game();if(!g)return false;
  std::array<uint8_t,8> victory{};
  for(unsigned i=0;i<8;++i)victory[i]=scr::read<uint8_t>(g+layout::Game::victory_state+i);
  return session_has_living_ally(read_session(g),victory);
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
    if(file_sha256(bot_path)!=pluto_sha256)throw std::runtime_error("Unsupported Pluto DLL SHA-256; expected CoG 2026 release");
    bot_module=LoadLibraryExW(bot_path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!bot_module)throw std::runtime_error("Cannot load configured Pluto DLL");
    legacy=std::make_unique<LegacyView>();legacy->hide_allies=allow_allies;legacy->update();legacy->bind(bot_module);
  }else{legacy->reset();legacy->update();}
  const auto init=reinterpret_cast<void (__cdecl*)(BWAPI::Game*)>(GetProcAddress(bot_module,"gameInit"));
  const auto create=reinterpret_cast<BWAPI::AIModule* (__cdecl*)()>(GetProcAddress(bot_module,"newAIModule"));
  if(!init || !create)throw std::runtime_error("Missing BWAPI exports");
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
  bot->onFrame();
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
    const bool leaving=c.type==BWAPIC::CommandType::LeaveGame;
    const bool block_always=leaving && !allow_resign;
    const bool block_for_ally=leaving && !block_always && team_resign && living_ally_present();
    if(block_always || block_for_ally) {
      if(!resign_notice_shown) {
        resign_notice_shown=true;
        std::fprintf(log_file,"{\"resign_ignored\":true,\"reason\":\"%s\",\"decision_frame\":%d}\n",
          block_always?"always":"ally_alive",d.frameCount);
        if(block_always)print_local({u8"플루토의 기권 요청을 무시했습니다 (자동 기권 막기).",
          u8"기권 결정 뒤 아무 행동도 하지 않을 수 있습니다."});
        else print_local({u8"아군이 아직 살아 있어 플루토의 기권 요청을 무시했습니다.",
          u8"아군이 모두 패배하면 기권을 허용합니다."});
      }
    }
    else if(c.type==BWAPIC::CommandType::LeaveGame) {
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
    last_frame=-1;match_ended=false;submitted_packets=0;resign_notice_shown=false;
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
    if(const auto reason=session_rejection(session,allow_multiplayer,challenge_mode,allow_allies)) {
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
    if(allow_allies)print_local({u8"플루토 팀전 개발판: 아군 유닛은 플루토에게 보이지 않습니다.",
      u8"플루토는 아군과 협력하지 못하고 혼자 판단합니다. 성능은 검증 전입니다."});
    if(allow_allies && team_resign)print_local({u8"팀 기권 규칙: 아군이 살아 있는 동안은 플루토의 기권을 막습니다."});
    else if(challenge_mode)print_local({u8"플루토 콘텐츠 실험 모드: 1 대 "+std::to_string(session_participant_count(session)-1)+u8" 경기입니다.",
      u8"1대1 모델을 사용하므로 다인전 전투력과 승률 예측은 검증 전입니다."});
  }
  if(!snapshot){snapshot=std::make_unique<Snapshot>();snapshot->hide_allies=allow_allies;}
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
  team_resign=allow_allies && GetPrivateProfileIntW(L"pluto",L"team_resign",0,(directory/L"bridge.ini").c_str())==1;
  allow_resign=GetPrivateProfileIntW(L"pluto",L"resign",1,(directory/L"bridge.ini").c_str())!=0;
  if(speed_ms>1000)speed_ms=1000;
  log_file=_wfsopen((directory/"bridge.log").c_str(),L"w",_SH_DENYNO);
  if(!log_file)return 1;
  std::fprintf(log_file,"{\"stage\":\"initializing\",\"pid\":%lu,\"mode\":\"%s\",\"challenge\":%s,\"allies\":%s,\"team_resign\":%s,\"resign\":%s}\n",GetCurrentProcessId(),
    allow_allies?"allies_experimental":(challenge_mode?"challenge_experimental":(allow_multiplayer?"multiplayer_experimental":"offline_pluto")),
    challenge_mode?"true":"false",allow_allies?"true":"false",team_resign?"true":"false",allow_resign?"true":"false");std::fflush(log_file);
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
  return result==MH_OK?0:3;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
  if(reason==DLL_PROCESS_ATTACH){bridge_module=module;DisableThreadLibraryCalls(module);HANDLE thread=CreateThread(nullptr,0,initialize,nullptr,0,nullptr);if(!thread)return FALSE;CloseHandle(thread);}
  return TRUE;
}
