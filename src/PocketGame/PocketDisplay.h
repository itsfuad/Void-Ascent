#pragma once

#include <Adafruit_GFX.h>
#include <Arduino_GFX_Library.h>

#include "PocketGameConfig.h"

namespace pocketgame {

class PocketDisplay {
public:
  PocketDisplay();
  ~PocketDisplay();

  PocketDisplay(const PocketDisplay &) = delete;
  PocketDisplay &operator=(const PocketDisplay &) = delete;

  bool begin();
  void present();
  void clear(uint16_t colour = 0);
  void setBrightness(uint8_t percentage);

  GFXcanvas16 &canvas() { return frame_; }
  const GFXcanvas16 &canvas() const { return frame_; }

  int16_t width() const { return config::SCREEN_WIDTH; }
  int16_t height() const { return config::SCREEN_HEIGHT; }
  bool ready() const { return ready_; }

private:
  Arduino_DataBus *bus_ = nullptr;
  Arduino_GFX *gfx_ = nullptr;
  GFXcanvas16 frame_;
  bool ready_ = false;

  static uint8_t brightnessDuty(uint8_t percentage);
};

} // namespace pocketgame
