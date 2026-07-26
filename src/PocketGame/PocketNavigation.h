#pragma once

#include <Arduino.h>

#include "PocketGameTypes.h"

namespace pocketgame {

enum class MenuAction : uint8_t { NONE, CYCLED, SELECTED };

// Reusable one-dimensional menu state. It owns index bounds and the
// cycle/select policy, while each game remains responsible only for drawing.
class MenuNavigator {
public:
  MenuNavigator() = default;
  explicit MenuNavigator(uint8_t itemCount, uint8_t selected = 0) {
    reset(itemCount, selected);
  }

  void reset(uint8_t itemCount, uint8_t selected = 0);
  void setItemCount(uint8_t itemCount);
  void setSelected(uint8_t selected);
  uint8_t selected() const { return selected_; }
  uint8_t itemCount() const { return itemCount_; }

  bool cycle(int8_t direction = 1);
  MenuAction update(const ControlEvents &events);

private:
  uint8_t itemCount_ = 0;
  uint8_t selected_ = 0;
};

// Generic screen transition state for game-owned screen enums.
template <typename Screen> class ScreenNavigator {
public:
  explicit ScreenNavigator(Screen initial) : current_(initial) {}

  Screen current() const { return current_; }
  uint32_t changedAt() const { return changedAt_; }
  uint32_t elapsed(uint32_t now) const { return now - changedAt_; }

  bool is(Screen screen) const { return current_ == screen; }

  void goTo(Screen screen, uint32_t now) {
    current_ = screen;
    changedAt_ = now;
  }

  // Marks the current screen as freshly entered without changing it. Useful
  // when cycling a preview or page within one screen.
  void refresh(uint32_t now) { changedAt_ = now; }

private:
  Screen current_;
  uint32_t changedAt_ = 0;
};

} // namespace pocketgame
