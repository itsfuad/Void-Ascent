#include "PocketDisplay.h"

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

namespace pocketgame {

namespace {
static constexpr uint8_t BACKLIGHT_CHANNEL = 0;
}

PocketDisplay::PocketDisplay()
    : frame_(config::SCREEN_WIDTH, config::SCREEN_HEIGHT) {}

PocketDisplay::~PocketDisplay() {
  delete gfx_;
  delete bus_;
}

bool PocketDisplay::begin() {
  if (ready_) {
    return true;
  }

  bus_ = new Arduino_ESP32SPI(
      config::LCD_DC_PIN, config::LCD_CS_PIN, config::LCD_SCLK_PIN,
      config::LCD_MOSI_PIN, GFX_NOT_DEFINED);

  gfx_ = new Arduino_ST7789(
      bus_, config::LCD_RESET_PIN, config::LCD_ROTATION, true,
      config::LCD_NATIVE_WIDTH, config::LCD_NATIVE_HEIGHT, 34, 0, 34, 0);

  setBrightness(config::DISPLAY_BRIGHTNESS_PERCENT);
  ready_ = gfx_ != nullptr && gfx_->begin(config::LCD_SPI_HZ);

  if (ready_) {
    gfx_->fillScreen(0);
    frame_.setTextWrap(false);
  }

  return ready_;
}

void PocketDisplay::present() {
  if (!ready_) {
    return;
  }

  gfx_->draw16bitRGBBitmap(0, 0, frame_.getBuffer(), config::SCREEN_WIDTH,
                           config::SCREEN_HEIGHT);
}

void PocketDisplay::clear(uint16_t colour) { frame_.fillScreen(colour); }

uint8_t PocketDisplay::brightnessDuty(uint8_t percentage) {
  const uint16_t clamped = percentage > 100 ? 100 : percentage;
  if (clamped == 0) {
    return 0;
  }

  // Perceptual quadratic mapping makes 50% look closer to half brightness.
  const uint32_t squared = (uint32_t)clamped * (uint32_t)clamped;
  const uint8_t duty = (uint8_t)((squared * 255U + 5000U) / 10000U);
  return duty < 2 ? 2 : duty;
}

void PocketDisplay::setBrightness(uint8_t percentage) {
  const uint8_t duty = brightnessDuty(percentage);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(config::LCD_BACKLIGHT_PIN, 5000, 8);
  ledcWrite(config::LCD_BACKLIGHT_PIN, duty);
#else
  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
  ledcAttachPin(config::LCD_BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, duty);
#endif
}

} // namespace pocketgame
