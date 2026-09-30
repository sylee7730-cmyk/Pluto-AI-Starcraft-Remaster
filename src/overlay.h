#pragma once
namespace BWAPI { struct GameData; }
// Optional, click-through spectator display for Pluto's BWAPI drawing calls.
void update_overlay(const BWAPI::GameData& data);
void clear_overlay();
