#include "manual_lock.h"
#include "commands.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

// Test double for the game-side activity lookup (the real one lives in snapshot.cpp).
UnitActivity unit_activity(uint32_t) { return UnitActivity::busy; }

static void require(bool condition, const char* what) {
  if (!condition) { std::fprintf(stderr, "manual lock test failed: %s\n", what); throw std::runtime_error(what); }
}
static std::vector<uint8_t> select_packet(uint8_t id, std::initializer_list<uint32_t> tags) {
  std::vector<uint8_t> packet{id, static_cast<uint8_t>(tags.size())};
  for (auto tag : tags) for (unsigned i = 0; i < 4; ++i) packet.push_back(static_cast<uint8_t>(tag >> (8 * i)));
  return packet;
}
static void feed(ManualLock& lock, const std::vector<uint8_t>& packet, int frame) { lock.on_human_packet(packet.data(), packet.size(), frame); }
static const std::vector<uint8_t> move_order{0x60, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0};  // right click
static const std::vector<uint8_t> stop_order{0x1a, 0};

int main() {
  try {
    { // A selection followed by an order locks exactly the selected units.
      ManualLock lock;
      feed(lock, select_packet(0x63, {0x101, 0x102}), 10);
      require(!lock.locked(0x101), "selecting alone must not lock");
      feed(lock, move_order, 12);
      require(lock.locked(0x101) && lock.locked(0x102) && !lock.locked(0x103), "order locks the selection");
      require(lock.stats.locks_created == 2 && lock.stats.orders == 1 && lock.stats.selections == 1, "stats");
    }
    { // Replace, shift-add and shift-deselect follow the wire semantics.
      ManualLock lock;
      feed(lock, select_packet(0x63, {0x101}), 1);
      feed(lock, select_packet(0x64, {0x102, 0x101}), 2);          // adding an existing unit must not duplicate it
      require(lock.selection() == std::vector<uint32_t>({0x101, 0x102}), "shift add");
      feed(lock, select_packet(0x65, {0x101}), 3);
      require(lock.selection() == std::vector<uint32_t>({0x102}), "shift deselect");
      feed(lock, select_packet(0x63, {0x105}), 4);
      require(lock.selection() == std::vector<uint32_t>({0x105}), "replace");
      feed(lock, stop_order, 5);
      require(lock.locked(0x105) && !lock.locked(0x102), "stop is an order for the current selection only");
    }
    { // Control groups: assign, recall, add. Recalling restores the units without any select packet.
      ManualLock lock;
      feed(lock, select_packet(0x63, {0x101, 0x102}), 1);
      feed(lock, {0x13, 0, 1}, 2);                                  // assign group 1
      feed(lock, select_packet(0x63, {0x103}), 3);
      feed(lock, {0x13, 1, 1}, 4);                                  // recall group 1
      require(lock.selection() == std::vector<uint32_t>({0x101, 0x102}), "recall restores the group");
      feed(lock, move_order, 5);
      require(lock.locked(0x101) && lock.locked(0x102) && !lock.locked(0x103), "order after recall");
      feed(lock, select_packet(0x63, {0x104}), 6);
      feed(lock, {0x13, 2, 1}, 7);                                  // add the selection to group 1
      feed(lock, select_packet(0x63, {0x200}), 8);
      feed(lock, {0x13, 1, 1}, 9);
      require(lock.selection() == std::vector<uint32_t>({0x101, 0x102, 0x104}), "add to group");
      feed(lock, {0x13, 1, 7}, 10);                                 // recalling an empty group selects nothing
      require(lock.selection().empty(), "empty group");
      feed(lock, {0x13, 1, 99}, 11);                                // group number out of range: ignored
      feed(lock, {0x13}, 12);                                       // truncated: ignored
    }
    { // Malformed or irrelevant packets never change anything.
      ManualLock lock;
      feed(lock, select_packet(0x63, {0x101}), 1);
      lock.on_human_packet(nullptr, 5, 2); lock.on_human_packet(move_order.data(), 0, 2);
      feed(lock, {0x63, 3, 1, 0, 0, 0}, 3);                         // claims 3 units, carries 1
      feed(lock, {0x63}, 3);
      std::vector<uint8_t> too_many{0x63, 13}; too_many.resize(2 + 13 * 4, 1);
      feed(lock, too_many, 3);
      require(lock.selection() == std::vector<uint32_t>({0x101}), "bad selections are ignored");
      feed(lock, {0x5c, 1, 2, 3}, 4);                               // chat
      feed(lock, {0x1f, 0, 0}, 4);                                  // training a unit
      feed(lock, {0x0c, 0x1e, 10, 0, 20, 0, 111, 0}, 4);            // building placement
      require(lock.locked_count() == 0, "only unit orders lock");
      ManualLock empty;
      feed(empty, move_order, 5);
      require(empty.locked_count() == 0 && empty.stats.orders == 0, "an order with nothing selected locks nothing");
    }
    { // Release rules.
      auto frames_until_release = [](UnitActivity activity, int order_frame) {
        ManualLock lock;
        feed(lock, select_packet(0x63, {0x101}), order_frame);
        feed(lock, move_order, order_frame);
        for (int frame = order_frame + 1; frame < order_frame + 2000; ++frame) {
          lock.update(frame, [&](uint32_t) { return activity; });
          if (!lock.locked(0x101)) return frame - order_frame;
        }
        return -1;
      };
      const int idle_release = frames_until_release(UnitActivity::idle, 100);
      require(idle_release >= ManualLock::min_lock_frames, "idle must not release before the order reached the unit");
      require(idle_release <= ManualLock::min_lock_frames + ManualLock::idle_release_frames + 2, "idle releases soon after that");
      require(frames_until_release(UnitActivity::gone, 100) == 1, "a dead unit is released at once");
      require(frames_until_release(UnitActivity::busy, 100) == ManualLock{}.timeout_frames, "busy units unlock at the timeout");
      { // Going busy again resets the idle timer: without it this unit (idle since frame 1) would be released at frame 30.
        ManualLock lock;
        feed(lock, select_packet(0x63, {0x101}), 0); feed(lock, move_order, 0);
        UnitActivity state = UnitActivity::idle;
        auto activity = [&](uint32_t) { return state; };
        for (int frame = 1; frame <= 33; ++frame) { state = frame == 21 ? UnitActivity::busy : UnitActivity::idle; lock.update(frame, activity); }
        require(lock.locked(0x101), "a unit that moved again keeps its lock");        // idle again only since frame 22
        state = UnitActivity::idle; lock.update(34, activity);
        require(!lock.locked(0x101), "and is released once it stays idle long enough");
      }
      { // A fresh order restarts the clock.
        ManualLock lock;
        lock.timeout_frames = 100;
        feed(lock, select_packet(0x63, {0x101}), 0); feed(lock, move_order, 0);
        feed(lock, move_order, 90);
        lock.update(120, [](uint32_t) { return UnitActivity::busy; });
        require(lock.locked(0x101), "re-ordering extends the timeout");
        lock.update(190, [](uint32_t) { return UnitActivity::busy; });
        require(!lock.locked(0x101), "and the timeout still applies");
      }
      ManualLock configured; configured.timeout_frames = 50;
      feed(configured, select_packet(0x63, {0x101}), 0); feed(configured, move_order, 0);
      configured.update(49, [](uint32_t) { return UnitActivity::busy; });
      require(configured.locked(0x101), "configured timeout: still locked");
      configured.update(50, [](uint32_t) { return UnitActivity::busy; });
      require(!configured.locked(0x101), "configured timeout: released");
      configured.clear();
      require(configured.locked_count() == 0 && configured.selection().empty() && configured.stats.packets == 0, "clear resets everything");
    }
    { // Integration with the command filter: Pluto's selections skip locked units.
      auto handles = [](uint16_t id) { return id == 0x806 ? 0x12345u : id == 0x834 ? 0x23456u : 0u; };
      ManualLock lock;
      feed(lock, select_packet(0x63, {0x23456}), 1); feed(lock, move_order, 2);     // human drives the unit behind handle 0x834
      CommandFilter filter;
      filter.foreign = [&](uint16_t id) { return lock.locked(handles(id)); };
      const std::vector<uint8_t> mixed{9, 2, 6, 8, 0x34, 8, 0x1a, 0};               // Pluto: select both, stop
      auto out = translate_commands(mixed.data(), mixed.size(), handles, &filter);
      require(out == std::vector<Packet>({{0x63, 1, 0x45, 0x23, 1, 0}, {0x1a, 0}}), "locked unit leaves Pluto's selection");
      const std::vector<uint8_t> only_locked{9, 1, 0x34, 8, 0x1a, 0};               // Pluto: select the locked unit, stop
      out = translate_commands(only_locked.data(), only_locked.size(), handles, &filter);
      require(out.empty() && filter.selection_blocked, "an all-locked selection drops the orders that follow");
      feed(lock, select_packet(0x63, {0x23456}), 3);
      lock.update(2000, [](uint32_t) { return UnitActivity::gone; });                // unit finished or died
      require(!lock.locked(0x23456), "released");
      out = translate_commands(mixed.data(), mixed.size(), handles, &filter);
      require(out.size() == 2 && out[0][1] == 2 && !filter.selection_blocked, "after release Pluto selects both again");
    }
    std::puts("manual lock: selection tracking, control groups, malformed packets, release rules and command-filter integration passed");
    return 0;
  } catch (const std::exception&) { return 1; }
}
