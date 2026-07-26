#pragma once

#include <Adafruit_NeoPixel.h>

#include "PocketGameConfig.h"
#include "PocketGameTypes.h"

namespace pocketgame {

class PocketLed {
public:
  PocketLed();

  void begin();
  void set(uint8_t red, uint8_t green, uint8_t blue);
  void set(RgbColor colour) { set(colour.red, colour.green, colour.blue); }
  void off() { set(0, 0, 0); }
  void setEnabled(bool enabled);
  bool enabled() const { return enabled_; }

  static uint8_t breatheValue(uint32_t now, uint16_t period,
                              uint8_t minimum, uint8_t maximum);
  static bool doubleBlink(uint32_t now, uint16_t period,
                          uint16_t firstOnMs = 95,
                          uint16_t secondStartMs = 175,
                          uint16_t secondEndMs = 285);

private:
  Adafruit_NeoPixel pixel_;
  bool enabled_ = config::RGB_LED_ENABLED;
  uint32_t lastColour_ = 0xFFFFFFFFU;
};

} // namespace pocketgame
