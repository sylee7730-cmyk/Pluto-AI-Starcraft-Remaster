#pragma once
#include <BWAPI/Client/GameData.h>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// Work in progress: translates SCR's simulation state to BWAPI's client layout.
class Snapshot {
public:
  std::unique_ptr<BWAPI::GameData> data = std::make_unique<BWAPI::GameData>();
  bool update(bool first);
  uint32_t raw_unit(int id) const;
  uint32_t unit_handle(int id) const;
private:
  struct Entry { uint32_t pointer; uint8_t generation; bool accessible = false; bool seen = false; };
  std::vector<Entry> entries;
  std::unordered_map<uint64_t,int> ids;
  int id_for(uint32_t pointer);
  int known_id(uint32_t pointer) const;
  void event(BWAPI::EventType::Enum type, int id);
  void update_players(uint32_t game);
  void update_map(uint32_t game, bool first);
  void update_unit(int id);
};
