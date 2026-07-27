#pragma once

#include <Arduino.h>

// Central hardware profile for the Waveshare ESP32-C6 1.47-inch LCD board.
// Games never include board pins directly. Change this file when the physical
// controls, display, LED, or storage wiring changes.
namespace pocketgame::config {

static constexpr int8_t LCD_MOSI_PIN = 6;
static constexpr int8_t LCD_SCLK_PIN = 7;
static constexpr int8_t LCD_CS_PIN = 14;
static constexpr int8_t LCD_DC_PIN = 15;
static constexpr int8_t LCD_RESET_PIN = 21;
static constexpr int8_t LCD_BACKLIGHT_PIN = 22;

// The onboard TF card shares MOSI/SCLK with the LCD and uses its own CS.
// All SD access is serialized by PocketMediaCard so the display and card can
// safely share the SPI bus.
static constexpr int8_t SD_MISO_PIN = 5;
static constexpr int8_t SD_MOSI_PIN = 6;
static constexpr int8_t SD_SCLK_PIN = 7;
static constexpr int8_t SD_CS_PIN = 4;
static constexpr uint32_t SD_SPI_HZ = 10000000UL;

static constexpr int8_t RGB_LED_PIN = 8;
static constexpr int8_t PRIMARY_BUTTON_PIN = 9;

static constexpr int16_t SCREEN_WIDTH = 172;
static constexpr int16_t SCREEN_HEIGHT = 320;
static constexpr int16_t LCD_NATIVE_WIDTH = 172;
static constexpr int16_t LCD_NATIVE_HEIGHT = 320;

static constexpr uint8_t LCD_ROTATION = 2;
static constexpr uint32_t LCD_SPI_HZ = 40000000UL;

// User-facing brightness is always 5..100. The display driver maps 100 to
// only 60% of the electrical PWM range to protect the small LCD backlight.
static constexpr uint8_t DISPLAY_DEFAULT_BRIGHTNESS = 50;
static constexpr uint8_t DISPLAY_MIN_BRIGHTNESS = 5;
static constexpr uint8_t DISPLAY_MAX_BRIGHTNESS = 100;
static constexpr uint8_t DISPLAY_HARDWARE_LIMIT_PERCENT = 60;
static constexpr uint8_t DISPLAY_BRIGHTNESS_STEP = 5;

static constexpr uint8_t RGB_LED_BRIGHTNESS = 45;
static constexpr bool RGB_LED_ENABLED = true;

static constexpr uint16_t BUTTON_DEBOUNCE_MS = 22;
static constexpr uint16_t BUTTON_HOLD_MS = 580;

static constexpr uint8_t MAX_REGISTERED_GAMES = 8;
static constexpr uint32_t SYSTEM_FRAME_INTERVAL_MS = 33;
static constexpr uint32_t BOOT_SCREEN_MS = 1750;

} // namespace pocketgame::config

#ifndef POCKETGAME_ENABLE_SD_STORAGE
#define POCKETGAME_ENABLE_SD_STORAGE 0
#endif
