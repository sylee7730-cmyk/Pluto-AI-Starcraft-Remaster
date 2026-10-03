#include <Windows.h>
#include <BWAPI.h>
#include <BWAPI/Client/GameData.h>
#include "overlay.h"
#include "messages.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace {
struct Item { BWAPIC::Shape shape; std::wstring text; };
// What the bridge itself reads off Pluto's drawing: the chart's outer box and the latest
// win value, so the percentage can be shown next to the chart wherever Pluto puts its text.
struct Chart { bool have_box=false;int x1=0,y1=0,x2=0,y2=0;bool have_win=false,unavailable=false;double win=0;int text_shapes=0; };
std::mutex items_mutex;
std::vector<Item> items;
Chart chart;
bool started=false;
ULONGLONG last_update=0;
HWND game_window=nullptr;
const char* last_hide_reason="never_shown";
constexpr COLORREF transparent=RGB(255,0,255);

// A top-level, visible, reasonably large window of this process: the game's
// render window, or one of its siblings (online play creates several).
bool looks_like_game_window(HWND window) {
  if(!window)return false;
  DWORD pid=0;GetWindowThreadProcessId(window,&pid);
  RECT rect{};GetClientRect(window,&rect);
  return pid==GetCurrentProcessId() && IsWindowVisible(window) && !GetWindow(window,GW_OWNER) &&
     !(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW) && rect.right>=640 && rect.bottom>=480;
}
BOOL CALLBACK find_game(HWND window,LPARAM) {
  if(looks_like_game_window(window)){game_window=window;return FALSE;}
  return TRUE;
}
bool foreground_is_ours() {
  DWORD pid=0;const HWND foreground=GetForegroundWindow();
  if(foreground)GetWindowThreadProcessId(foreground,&pid);
  return foreground && pid==GetCurrentProcessId();
}
void paint(HDC target,const RECT& area) {
  std::vector<Item> copy;Chart info;
  {std::lock_guard<std::mutex> lock(items_mutex);copy=items;info=chart;}
  HDC dc=CreateCompatibleDC(target);
  HBITMAP bitmap=CreateCompatibleBitmap(target,640,480);
  auto old_bitmap=SelectObject(dc,bitmap);
  RECT logical{0,0,640,480};
  auto background=CreateSolidBrush(transparent);FillRect(dc,&logical,background);DeleteObject(background);
  SetBkMode(dc,TRANSPARENT);
  auto font=CreateFontW(-10,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
    CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Malgun Gothic");
  auto old_font=SelectObject(dc,font);
  for(const auto& item:copy) {
    const auto& s=item.shape;
    if(s.type==BWAPIC::ShapeType::Text) {
      SetTextColor(dc,RGB(245,245,245));
      RECT text_rect{s.x1,s.y1,640,480};
      DrawTextW(dc,item.text.c_str(),static_cast<int>(item.text.size()),&text_rect,DT_LEFT|DT_TOP|DT_NOPREFIX);
      continue;
    }
    BWAPI::Color color(s.color);const auto rgb=RGB(color.red(),color.green(),color.blue());
    auto pen=CreatePen(PS_SOLID,1,rgb);auto old_pen=SelectObject(dc,pen);
    auto brush=s.isSolid?CreateSolidBrush(rgb):static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    auto old_brush=SelectObject(dc,brush);
    switch(s.type) {
      case BWAPIC::ShapeType::Box:Rectangle(dc,s.x1,s.y1,s.x2,s.y2);break;
      case BWAPIC::ShapeType::Line:MoveToEx(dc,s.x1,s.y1,nullptr);LineTo(dc,s.x2,s.y2);break;
      case BWAPIC::ShapeType::Dot:SetPixelV(dc,s.x1,s.y1,rgb);break;
      case BWAPIC::ShapeType::Circle:Ellipse(dc,s.x1-s.extra1,s.y1-s.extra1,s.x1+s.extra1,s.y1+s.extra1);break;
      case BWAPIC::ShapeType::Ellipse:Ellipse(dc,s.x1-s.extra1,s.y1-s.extra2,s.x1+s.extra1,s.y1+s.extra2);break;
      case BWAPIC::ShapeType::Triangle:{POINT points[]={{s.x1,s.y1},{s.x2,s.y2},{s.extra1,s.extra2}};Polygon(dc,points,3);break;}
      default:break;
    }
    SelectObject(dc,old_brush);if(s.isSolid)DeleteObject(brush);
    SelectObject(dc,old_pen);DeleteObject(pen);
  }
  if(info.have_win) {
    const std::wstring label=info.unavailable?L"승률 계산 중":L"승률 "+[&]{
      const auto text=win_percent_ko(std::to_string(info.win));
      const int count=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
      std::wstring wide(count>0?count:0,L' ');
      if(count>0)MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),wide.data(),count);
      return wide;}();
    auto big=CreateFontW(-14,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Malgun Gothic");
    auto previous=SelectObject(dc,big);
    // Just above the chart's top-left corner; inside it when the chart touches the screen top.
    int x=info.have_box?info.x1:8,y=info.have_box?info.y1-17:8;
    if(y<0)y=info.y1+3;
    RECT shadow{x+1,y+1,640,480},label_rect{x,y,640,480};
    SetTextColor(dc,RGB(0,0,0));DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&shadow,DT_LEFT|DT_TOP|DT_NOPREFIX);
    SetTextColor(dc,RGB(255,255,100));DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&label_rect,DT_LEFT|DT_TOP|DT_NOPREFIX);
    SelectObject(dc,previous);DeleteObject(big);
  }
  SetStretchBltMode(target,COLORONCOLOR);
  StretchBlt(target,0,0,area.right,area.bottom,dc,0,0,640,480,SRCCOPY);
  SelectObject(dc,old_font);DeleteObject(font);
  SelectObject(dc,old_bitmap);DeleteObject(bitmap);DeleteDC(dc);
}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
  switch(message) {
    case WM_NCHITTEST:return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_TIMER:{
      bool empty;
      {std::lock_guard<std::mutex> lock(items_mutex);empty=items.empty();}
      // Online play gives the game process more than one large top-level window, so
      // the window the player is actually looking at may not be the first one found.
      // Follow whichever of this process's game-like windows is in the foreground.
      const HWND foreground=GetForegroundWindow();
      if(foreground!=game_window && looks_like_game_window(foreground))game_window=foreground;
      if(!IsWindow(game_window))EnumWindows(find_game,0);
      const bool ours_in_front=foreground_is_ours();
      if(empty || !game_window || !ours_in_front || IsIconic(game_window)) {
        last_hide_reason=empty?"no_shapes":!game_window?"no_game_window":IsIconic(game_window)?"minimized":"not_foreground_other_app";
        ShowWindow(window,SW_HIDE);return 0;
      }
      last_hide_reason="shown";
      RECT client{};GetClientRect(game_window,&client);POINT origin{};ClientToScreen(game_window,&origin);
      const int height=std::min(client.bottom,client.right*3/4),width=height*4/3;
      SetWindowPos(window,HWND_TOPMOST,origin.x+(client.right-width)/2,origin.y+(client.bottom-height)/2,
        width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
      InvalidateRect(window,nullptr,FALSE);return 0;
    }
    case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(window,&ps);RECT rect{};GetClientRect(window,&rect);paint(dc,rect);EndPaint(window,&ps);return 0;}
    case WM_DESTROY:PostQuitMessage(0);return 0;
    default:return DefWindowProcW(window,message,wparam,lparam);
  }
}
DWORD WINAPI overlay_thread(void*) {
  WNDCLASSW cls{};cls.lpfnWndProc=window_proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"PlutoSpectatorGraph";
  if(!RegisterClassW(&cls))return 1;
  auto window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
    cls.lpszClassName,L"플루토 승률",WS_POPUP,0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
  if(!window)return 2;
  SetLayeredWindowAttributes(window,transparent,255,LWA_COLORKEY);
  SetTimer(window,1,100,nullptr);
  MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
  return 0;
}
}
void update_overlay(const BWAPI::GameData& data) {
  if(data.shapeCount<=0)return;
  const auto now=GetTickCount64();if(now-last_update<100)return;last_update=now;
  std::vector<Item> next;next.reserve(data.shapeCount);
  Chart seen;long long best_area=0;
  for(int i=0;i<data.shapeCount;++i) {
    auto shape=data.shapes[i];
    if(shape.type==BWAPIC::ShapeType::Text && shape.extra1>=0 && shape.extra1<data.stringCount) {
      ++seen.text_shapes;
      std::string raw;
      for(const unsigned char c:std::string(data.strings[shape.extra1]))
        if(c>=32 || c=='\n' || c=='\t')raw.push_back(static_cast<char>(c));
      int frame=0;double value=0;bool unavailable=false;
      // Any coordinate type: only the text matters here, not where Pluto drew it.
      if(parse_win_text(raw,frame,value,unavailable)){seen.have_win=true;seen.unavailable=unavailable;seen.win=value;}
    }
    // Pluto's win-probability chart uses screen coordinates exclusively.
    if(shape.ctype!=BWAPI::CoordinateType::Screen)continue;
    if(shape.type==BWAPIC::ShapeType::Box) {  // The chart's frame is the largest screen box.
      const long long area=static_cast<long long>(std::abs(shape.x2-shape.x1))*std::abs(shape.y2-shape.y1);
      if(area>best_area){best_area=area;seen.have_box=true;seen.x1=std::min(shape.x1,shape.x2);seen.y1=std::min(shape.y1,shape.y2);
        seen.x2=std::max(shape.x1,shape.x2);seen.y2=std::max(shape.y1,shape.y2);}
    }
    Item item{shape,{}};
    if(shape.type==BWAPIC::ShapeType::Text && shape.extra1>=0 && shape.extra1<data.stringCount) {
      std::string clean;
      for(const unsigned char c:std::string(data.strings[shape.extra1]))
        if(c>=32 || c=='\n' || c=='\t')clean.push_back(static_cast<char>(c));
      const auto localized=pluto_overlay_text_ko(clean);
      const int count=MultiByteToWideChar(CP_UTF8,0,localized.data(),static_cast<int>(localized.size()),nullptr,0);
      if(count>0) {
        item.text.resize(count);
        MultiByteToWideChar(CP_UTF8,0,localized.data(),static_cast<int>(localized.size()),item.text.data(),count);
      }
    }
    next.push_back(std::move(item));
  }
  {std::lock_guard<std::mutex> lock(items_mutex);items=std::move(next);chart=seen;}
  if(!started) {auto thread=CreateThread(nullptr,0,overlay_thread,nullptr,0,nullptr);if(thread){started=true;CloseHandle(thread);}}
}
void clear_overlay() {std::lock_guard<std::mutex> lock(items_mutex);items.clear();chart=Chart{};last_update=0;}
std::string overlay_chart_summary() {
  std::lock_guard<std::mutex> lock(items_mutex);
  char buffer[160];
  std::snprintf(buffer,sizeof(buffer),"text_shapes=%d,win_text=%s,win=%.3f,box=%s(%d,%d,%d,%d)",chart.text_shapes,
    chart.have_win?(chart.unavailable?"n/a":"yes"):"no",chart.win,chart.have_box?"yes":"no",chart.x1,chart.y1,chart.x2,chart.y2);
  return buffer;
}
std::string overlay_status() {
  return std::string(started?"thread_started":"thread_not_started")+","+(game_window?"game_window_found":"game_window_missing")+","+last_hide_reason;
}
