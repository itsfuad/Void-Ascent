#include "PocketLed.h"

#include <math.h>

namespace pocketgame {

PocketLed::PocketLed()
    : pixel_(1, config::RGB_LED_PIN, NEO_GRB + NEO_KHZ800) {}

void PocketLed::begin() {
  pixel_.begin();
  pixel_.setBrightness(config::RGB_LED_BRIGHTNESS);
  pixel_.clear();
  pixel_.show();
  lastColour_ = 0xFFFFFFFFU;
}

void PocketLed::setEnabled(bool enabled) {
  enabled_ = enabled;
  if (!enabled_) {
    const bool previous = enabled_;
    enabled_ = true;
    set(0, 0, 0);
    enabled_ = previous;
  } else {
    lastColour_ = 0xFFFFFFFFU;
  }
}

void PocketLed::set(uint8_t red, uint8_t green, uint8_t blue) {
  if (!enabled_) {
    red = 0;
    green = 0;
    blue = 0;
  }

  const uint32_t colour = pixel_.Color(red, green, blue);
  if (colour == lastColour_) {
    return;
  }

  lastColour_ = colour;
  pixel_.setPixelColor(0, colour);
  pixel_.show();
}

uint8_t PocketLed::breatheValue(uint32_t now, uint16_t period,
                                uint8_t minimum, uint8_t maximum) {
  if (period == 0 || maximum <= minimum) {
    return minimum;
  }

  const float phase = (now % period) / (float)period * 6.2831853f;
  const float amount = sinf(phase) * 0.5f + 0.5f;
  return minimum + (uint8_t)((maximum - minimum) * amount);
}

bool PocketLed::doubleBlink(uint32_t now, uint16_t period,
                            uint16_t firstOnMs, uint16_t secondStartMs,
                            uint16_t secondEndMs) {
  if (period == 0) {
    return false;
  }

  const uint16_t phase = now % period;
  return phase < firstOnMs ||
         (phase >= secondStartMs && phase < secondEndMs);
}

} // namespace pocketgame
