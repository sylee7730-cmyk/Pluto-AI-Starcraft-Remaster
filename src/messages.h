#pragma once
#include "session.h"
#include <string>
#include <string_view>
#include <vector>

// UTF-8 for SCR's local print_text entry point. Unknown bot text is preserved.
std::vector<std::string> pluto_messages_ko(std::string_view text);
std::vector<std::string> session_messages_ko(const Session& session,std::string_view reason,bool challenge=false);
std::string pluto_overlay_text_ko(std::string_view text);
