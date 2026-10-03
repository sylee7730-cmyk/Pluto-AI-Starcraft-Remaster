#pragma once
#include <string>
namespace BWAPI { struct GameData; }
// Optional, click-through spectator display for Pluto's BWAPI drawing calls.
void update_overlay(const BWAPI::GameData& data);
void clear_overlay();
// Short diagnostic: whether the spectator window thread started and found the game window.
std::string overlay_status();
