#include "PocketControls.h"

namespace pocketgame {

void SingleButtonControlSource::begin() {
  pinMode(pin_, INPUT_PULLUP);
  stableDown_ = digitalRead(pin_) == LOW;
  rawDown_ = stableDown_;
  changedAt_ = millis();
}

void SingleButtonControlSource::update(uint32_t now, ControlEvents &events) {
  const bool reading = digitalRead(pin_) == LOW;

  if (reading != rawDown_) {
    rawDown_ = reading;
    changedAt_ = now;
  }

  if (reading != stableDown_ && now - changedAt_ >= debounceMs_) {
    stableDown_ = reading;

    if (stableDown_) {
      pressedAt_ = now;
      holdSent_ = false;
      events.pressed = true;
      events.actionPressed = true;
    } else {
      events.released = true;
      events.actionReleased = true;

      if (!holdSent_ && now - pressedAt_ < holdMs_) {
        events.tapped = true;
        events.select = true;
      }
    }
  }

  if (stableDown_ && !holdSent_ && now - pressedAt_ >= holdMs_) {
    holdSent_ = true;
    events.held = true;
    events.cycle = true;
  }
}

void PocketControls::begin() {
  events_.clear();
  source_.begin();
}

void PocketControls::update(uint32_t now) {
  events_.clear();
  source_.update(now, events_);
}

} // namespace pocketgame
