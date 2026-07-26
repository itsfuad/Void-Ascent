#pragma once

#include <Arduino.h>

namespace pocketgame {

struct RgbColor {
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
};

struct ControlEvents {
  // Semantic controls consumed by games and menus.
  bool cycle = false;
  bool select = false;
  bool actionPressed = false;
  bool actionReleased = false;

  // Raw one-button-compatible events are retained for games that need them.
  bool pressed = false;
  bool released = false;
  bool tapped = false;
  bool held = false;

  bool any() const {
    return cycle || select || actionPressed || actionReleased || pressed ||
           released || tapped || held;
  }

  void clear() { *this = ControlEvents{}; }
};

} // namespace pocketgame
