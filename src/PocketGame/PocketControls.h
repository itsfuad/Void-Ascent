#pragma once

#include "PocketGameConfig.h"
#include "PocketGameTypes.h"

namespace pocketgame {

// Hardware-independent control source. A future multi-button board, joystick,
// touch controller, or serial controller only needs a new implementation.
class IControlSource {
public:
  virtual ~IControlSource() = default;
  virtual void begin() = 0;
  virtual void update(uint32_t now, ControlEvents &events) = 0;
};

// Default mapping for the board's single active-low button:
//   tap       -> select
//   hold      -> cycle
//   press edge-> actionPressed (used by timing games)
class SingleButtonControlSource final : public IControlSource {
public:
  explicit SingleButtonControlSource(
      int8_t pin = config::PRIMARY_BUTTON_PIN,
      uint16_t debounceMs = config::BUTTON_DEBOUNCE_MS,
      uint16_t holdMs = config::BUTTON_HOLD_MS)
      : pin_(pin), debounceMs_(debounceMs), holdMs_(holdMs) {}

  void begin() override;
  void update(uint32_t now, ControlEvents &events) override;

private:
  int8_t pin_;
  uint16_t debounceMs_;
  uint16_t holdMs_;

  bool stableDown_ = false;
  bool rawDown_ = false;
  bool holdSent_ = false;
  uint32_t changedAt_ = 0;
  uint32_t pressedAt_ = 0;
};

class PocketControls {
public:
  explicit PocketControls(IControlSource &source) : source_(source) {}

  void begin();
  void update(uint32_t now);
  const ControlEvents &events() const { return events_; }

  // Clears edge events after the active game or system menu has processed one
  // loop iteration. This prevents a press from being handled repeatedly.
  void consume() { events_.clear(); }

private:
  IControlSource &source_;
  ControlEvents events_;
};

} // namespace pocketgame
