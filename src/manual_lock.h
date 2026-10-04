#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <vector>

// Manual control lock.
//
// Pluto controls the same player the human does and re-issues orders every few frames, so a
// human order is overwritten within a fraction of a second. The bridge watches the command
// packets the game itself queues (its own packets are excluded), follows which units the human
// has selected, and locks those units when an order for them goes out. Pluto's selections then
// skip locked units until their work is done (see update), so only the human drives them.
enum class UnitActivity { gone, busy, idle };
// Defined in snapshot.cpp: classifies an SCR unit struct.
UnitActivity unit_activity(uint32_t raw_unit);

class ManualLock {
public:
  struct Stats { unsigned packets = 0, selections = 0, orders = 0, hotkeys = 0, locks_created = 0, units_trimmed = 0; };

  static constexpr size_t max_selection = 12;   // an SCR selection holds at most 12 units
  static constexpr int min_lock_frames = 30;    // an order needs time to reach the unit before "idle" means "done"
  static constexpr int idle_release_frames = 12;  // idle this long after that = the work is finished
  int timeout_frames = 24 * 20;                 // longest a unit stays locked (gathering never "finishes")
  Stats stats;

  // Orders that act on the current selection: right click, targeted order, stop, hold position.
  static bool is_unit_order(uint8_t id) { return id == 0x60 || id == 0x61 || id == 0x1a || id == 0x2b; }
  static bool is_selection(uint8_t id) { return id >= 0x63 && id <= 0x65; }

  void on_human_packet(const uint8_t* data, size_t size, int frame) {
    if (!data || !size) return;
    ++stats.packets;
    const uint8_t id = data[0];
    if (is_selection(id)) select(id, data, size);
    else if (id == 0x13) hotkey(data, size);
    else if (is_unit_order(id)) order(frame);
  }

  bool locked(uint32_t tag) const { return locks.find(tag) != locks.end(); }
  size_t locked_count() const { return locks.size(); }
  const std::vector<uint32_t>& selection() const { return current; }

  // Releases units whose work is finished. activity(tag) returns UnitActivity.
  template<class Activity> void update(int frame, Activity&& activity) {
    for (auto it = locks.begin(); it != locks.end();) {
      bool release = false;
      if (frame - it->second.since >= timeout_frames) release = true;
      else switch (activity(it->first)) {
        case UnitActivity::gone: release = true; break;
        case UnitActivity::busy: it->second.idle_since = -1; break;
        case UnitActivity::idle:
          if (it->second.idle_since < 0) it->second.idle_since = frame;
          release = frame - it->second.since >= min_lock_frames && frame - it->second.idle_since >= idle_release_frames;
          break;
      }
      it = release ? locks.erase(it) : std::next(it);
    }
  }

  void clear() {
    current.clear();
    for (auto& group : groups) group.clear();
    locks.clear();
    stats = Stats{};
  }

private:
  struct Lock { int since = 0; int idle_since = -1; };
  std::vector<uint32_t> current;                // the human's selection as last seen on the wire
  std::array<std::vector<uint32_t>, 10> groups; // control groups the human assigned
  std::unordered_map<uint32_t, Lock> locks;

  static bool read_tags(const uint8_t* data, size_t size, std::vector<uint32_t>& out) {
    if (size < 2) return false;
    const size_t count = data[1];
    if (count > max_selection || size < 2 + count * 4) return false;
    out.clear();
    for (size_t i = 0; i < count; ++i) {
      const uint8_t* p = data + 2 + i * 4;
      const uint32_t tag = p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
      if (tag) out.push_back(tag);
    }
    return true;
  }
  static void add_unique(std::vector<uint32_t>& list, uint32_t tag) {
    if (std::find(list.begin(), list.end(), tag) == list.end()) list.push_back(tag);
  }
  void select(uint8_t id, const uint8_t* data, size_t size) {
    std::vector<uint32_t> tags;
    if (!read_tags(data, size, tags)) return;
    ++stats.selections;
    if (id == 0x63) current = std::move(tags);                 // replace
    else if (id == 0x64) for (auto tag : tags) add_unique(current, tag);  // shift-add
    else current.erase(std::remove_if(current.begin(), current.end(), [&](uint32_t tag) {
      return std::find(tags.begin(), tags.end(), tag) != tags.end(); }), current.end());  // shift-deselect
  }
  // Control groups: [0x13][type][group]; 0 = assign, 1 = recall, 2 = add to group.
  void hotkey(const uint8_t* data, size_t size) {
    if (size < 3 || data[2] >= groups.size()) return;
    ++stats.hotkeys;
    auto& group = groups[data[2]];
    if (data[1] == 0) group = current;
    else if (data[1] == 1) current = group;
    else if (data[1] == 2) for (auto tag : current) add_unique(group, tag);
  }
  void order(int frame) {
    if (current.empty()) return;
    ++stats.orders;
    for (auto tag : current) {
      auto result = locks.try_emplace(tag);
      result.first->second = Lock{frame, -1};  // a new order restarts the lock
      if (result.second) ++stats.locks_created;
    }
  }
};
