#include "PocketGame.h"

#include <math.h>
#include <string.h>

namespace pocketgame {

namespace {
static constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((uint16_t)(red & 0xF8) << 8) | ((uint16_t)(green & 0xFC) << 3) |
         ((uint16_t)blue >> 3);
}

static constexpr uint16_t C_BLACK = rgb565(3, 6, 12);
static constexpr uint16_t C_NAVY = rgb565(8, 16, 29);
static constexpr uint16_t C_PANEL = rgb565(15, 28, 45);
static constexpr uint16_t C_PANEL_2 = rgb565(25, 43, 65);
static constexpr uint16_t C_BORDER = rgb565(64, 91, 119);
static constexpr uint16_t C_TEXT = rgb565(238, 244, 244);
static constexpr uint16_t C_MUTED = rgb565(133, 155, 176);
static constexpr uint16_t C_CYAN = rgb565(50, 190, 224);
static constexpr uint16_t C_BLUE = rgb565(68, 119, 224);
static constexpr uint16_t C_GOLD = rgb565(255, 196, 67);
static constexpr uint16_t C_ORANGE = rgb565(244, 116, 48);
static constexpr uint16_t C_RED = rgb565(231, 66, 73);

static int16_t textWidth(const char *text, uint8_t size) {
  return (int16_t)(strlen(text) * 6 * size);
}

static void textLeft(GFXcanvas16 &frame, const char *text, int16_t x, int16_t y,
                     uint8_t size, uint16_t colour) {
  frame.setTextWrap(false);
  frame.setTextSize(size);
  frame.setTextColor(colour);
  frame.setCursor(x, y);
  frame.print(text);
}

static void centered(GFXcanvas16 &frame, const char *text, int16_t y,
                     uint8_t size, uint16_t colour) {
  frame.setTextWrap(false);
  frame.setTextSize(size);
  frame.setTextColor(colour);
  frame.setCursor((config::SCREEN_WIDTH - textWidth(text, size)) / 2, y);
  frame.print(text);
}

static void drawPixelRocket(GFXcanvas16 &frame, int16_t x, int16_t y,
                            uint16_t accent) {
  frame.fillTriangle(x, y - 18, x - 8, y - 5, x + 8, y - 5, C_TEXT);
  frame.fillRoundRect(x - 7, y - 6, 14, 25, 3, C_TEXT);
  frame.fillRect(x - 7, y + 5, 14, 5, accent);
  frame.fillCircle(x, y, 3, C_NAVY);
  frame.fillTriangle(x - 7, y + 11, x - 13, y + 20, x - 6, y + 18, accent);
  frame.fillTriangle(x + 7, y + 11, x + 13, y + 20, x + 6, y + 18, accent);
  frame.fillTriangle(x - 4, y + 19, x, y + 30, x + 4, y + 19, C_ORANGE);
}

static void drawPixelTower(GFXcanvas16 &frame, int16_t x, int16_t y,
                           uint16_t accent) {
  for (uint8_t floor = 0; floor < 4; ++floor) {
    const int16_t width = 38 - floor * 2;
    const int16_t floorY = y + 23 - floor * 18;
    frame.fillRect(x - width / 2 - 2, floorY - 2, width + 4, 18, C_NAVY);
    frame.fillRect(x - width / 2, floorY, width, 14,
                   floor & 1 ? C_PANEL_2 : accent);
    frame.drawRect(x - width / 2, floorY, width, 14, C_TEXT);
    frame.fillRect(x - width / 2 + 5, floorY + 4, 5, 5, C_GOLD);
    frame.fillRect(x + width / 2 - 10, floorY + 4, 5, 5, C_GOLD);
  }
  frame.drawFastHLine(x - 36, y + 40, 72, C_BORDER);
}

static void drawGear(GFXcanvas16 &frame, int16_t x, int16_t y,
                     uint16_t colour) {
  frame.fillCircle(x, y, 18, colour);
  frame.fillCircle(x, y, 9, C_PANEL);
  for (uint8_t index = 0; index < 8; ++index) {
    const float angle = index * 0.78539816f;
    const int16_t px = x + (int16_t)roundf(cosf(angle) * 22.0f);
    const int16_t py = y + (int16_t)roundf(sinf(angle) * 22.0f);
    frame.fillRect(px - 3, py - 3, 7, 7, colour);
  }
}

static void drawControlHint(GFXcanvas16 &frame, const char *left,
                            const char *right) {
  frame.fillRoundRect(7, 286, 158, 25, 6, C_PANEL);
  frame.drawRoundRect(7, 286, 158, 25, 6, C_BORDER);
  textLeft(frame, left, 15, 295, 1, C_MUTED);
  const int16_t rightWidth = textWidth(right, 1);
  textLeft(frame, right, 157 - rightWidth, 295, 1, C_TEXT);
}

static uint8_t clampBrightness(uint8_t value) {
  if (value < config::DISPLAY_MIN_BRIGHTNESS) {
    return config::DISPLAY_MIN_BRIGHTNESS;
  }
  if (value > config::DISPLAY_MAX_BRIGHTNESS) {
    return config::DISPLAY_MAX_BRIGHTNESS;
  }
  return value - value % config::DISPLAY_BRIGHTNESS_STEP;
}
} // namespace

bool PocketGameSystem::registerGame(IGame &game) {
  if (begun_ || gameCount_ >= config::MAX_REGISTERED_GAMES) {
    return false;
  }

  for (uint8_t index = 0; index < gameCount_; ++index) {
    if (games_[index] == &game || strcmp(games_[index]->id(), game.id()) == 0) {
      return false;
    }
  }

  games_[gameCount_++] = &game;
  gameMenu_.setItemCount(gameCount_ + 1); // final item is BACK
  return true;
}

bool PocketGameSystem::openSystemStorage() {
  storage_.close();
  return storage_.open(SYSTEM_STORAGE_NAMESPACE);
}

bool PocketGameSystem::begin(uint32_t serialBaud) {
  if (begun_) {
    return true;
  }

  Serial.begin(serialBaud);
  controls_.begin();
  led_.begin();

  if (!display_.begin()) {
    Serial.println("PocketGame: LCD initialization failed.");
    showHardwareFault();
    return false;
  }

  begun_ = true;
  openSystemStorage();
  brightness_ = clampBrightness(
      storage_.getUInt8("brightness", config::DISPLAY_DEFAULT_BRIGHTNESS));
  display_.setBrightness(brightness_);

  homeMenu_.reset(2, 0); // GAMES, SETTINGS
  gameMenu_.reset(gameCount_ + 1, 0);
  setSystemScreen(SystemScreen::BOOT, millis());
  renderSystem(millis());

  Serial.print("PocketGame: registered games: ");
  Serial.println(gameCount_);
  return true;
}

void PocketGameSystem::loop() {
  if (!begun_) {
    delay(1);
    return;
  }

  const uint32_t now = millis();
  controls_.update(now);

  if (activeGame_ != nullptr) {
    activeGame_->loop(*this, now);
  } else {
    updateSystem(now);
  }

  controls_.consume();
}

bool PocketGameSystem::launch(uint8_t index) {
  if (index >= gameCount_ || games_[index] == nullptr) {
    return false;
  }

  IGame *next = games_[index];
  storage_.close();
  if (!storage_.open(next->storageNamespace())) {
    Serial.print("PocketGame: storage unavailable for ");
    Serial.println(next->title());
    // Games still receive begin() and can run with API fallback values.
  }

  activeGame_ = next;
  led_.off();
  controls_.consume();
  activeGame_->begin(*this);

  Serial.print("PocketGame: started ");
  Serial.println(activeGame_->title());
  return true;
}

void PocketGameSystem::requestLauncher() {
  activeGame_ = nullptr;
  openSystemStorage();
  brightness_ = clampBrightness(
      storage_.getUInt8("brightness", config::DISPLAY_DEFAULT_BRIGHTNESS));
  display_.setBrightness(brightness_);
  controls_.consume();
  homeMenu_.reset(2, 0);
  setSystemScreen(SystemScreen::HOME, millis());
  renderSystem(millis());
}

void PocketGameSystem::setSystemScreen(SystemScreen screen, uint32_t now) {
  systemScreen_ = screen;
  screenStartedAt_ = now;
  lastSystemFrameAt_ = 0;
}

void PocketGameSystem::updateSystem(uint32_t now) {
  switch (systemScreen_) {
  case SystemScreen::BOOT:
    if (now - screenStartedAt_ >= config::BOOT_SCREEN_MS) {
      setSystemScreen(SystemScreen::HOME, now);
      renderSystem(now);
    }
    break;
  case SystemScreen::HOME:
    updateHome(now);
    break;
  case SystemScreen::GAMES:
    updateGames(now);
    break;
  case SystemScreen::SETTINGS:
    updateSettings(now);
    break;
  }

  // launch() calls the game's begin(), which may render its first frame.
  // Do not overwrite that frame with the old system screen in this loop.
  if (activeGame_ != nullptr) {
    return;
  }

  if (now - lastSystemFrameAt_ >= config::SYSTEM_FRAME_INTERVAL_MS) {
    renderSystem(now);
  }

  const uint8_t pulse = PocketLed::breatheValue(now, 1500, 18, 118);
  if (systemScreen_ == SystemScreen::SETTINGS) {
    led_.set(pulse, pulse / 3, 0);
  } else {
    led_.set(0, pulse / 2, pulse);
  }
  delay(1);
}

void PocketGameSystem::updateHome(uint32_t now) {
  const MenuAction action = homeMenu_.update(controls_.events());
  if (action == MenuAction::CYCLED) {
    renderSystem(now);
  } else if (action == MenuAction::SELECTED) {
    if (homeMenu_.selected() == 0) {
      gameMenu_.reset(gameCount_ + 1, 0);
      setSystemScreen(SystemScreen::GAMES, now);
    } else {
      setSystemScreen(SystemScreen::SETTINGS, now);
    }
    renderSystem(now);
  }
}

void PocketGameSystem::updateGames(uint32_t now) {
  gameMenu_.setItemCount(gameCount_ + 1);
  const MenuAction action = gameMenu_.update(controls_.events());
  if (action == MenuAction::CYCLED) {
    renderSystem(now);
  } else if (action == MenuAction::SELECTED) {
    if (gameMenu_.selected() >= gameCount_) {
      setSystemScreen(SystemScreen::HOME, now);
      renderSystem(now);
    } else {
      launch(gameMenu_.selected());
    }
  }
}

void PocketGameSystem::updateSettings(uint32_t now) {
  const ControlEvents &events = controls_.events();
  if (events.cycle) {
    uint16_t next = brightness_ + config::DISPLAY_BRIGHTNESS_STEP;
    if (next > config::DISPLAY_MAX_BRIGHTNESS) {
      next = config::DISPLAY_MIN_BRIGHTNESS;
    }
    brightness_ = (uint8_t)next;
    display_.setBrightness(brightness_);
    renderSystem(now);
  }

  if (events.select) {
    storage_.putUInt8("brightness", brightness_);
    setSystemScreen(SystemScreen::HOME, now);
    renderSystem(now);
  }
}

void PocketGameSystem::renderSystem(uint32_t now) {
  lastSystemFrameAt_ = now;
  switch (systemScreen_) {
  case SystemScreen::BOOT:
    renderBoot(now);
    break;
  case SystemScreen::HOME:
    renderHome(now);
    break;
  case SystemScreen::GAMES:
    renderGames(now);
    break;
  case SystemScreen::SETTINGS:
    renderSettings(now);
    break;
  }
  display_.present();
}

void PocketGameSystem::renderBoot(uint32_t now) {
  GFXcanvas16 &frame = display_.canvas();
  frame.fillScreen(C_BLACK);

  for (int16_t y = 0; y < config::SCREEN_HEIGHT; y += 16) {
    const uint8_t shade = (uint8_t)(8 + y / 18);
    frame.fillRect(0, y, config::SCREEN_WIDTH, 16,
                   rgb565(3, shade, shade + 10));
  }

  for (uint8_t i = 0; i < 24; ++i) {
    const int16_t x = (i * 47 + 13) % config::SCREEN_WIDTH;
    const int16_t y = (i * 83 + 29) % 225;
    const bool lit = ((now / 240U + i) % 5U) != 0;
    frame.drawPixel(x, y, lit ? C_TEXT : C_BORDER);
  }

  frame.fillRoundRect(22, 89, 128, 105, 18, C_PANEL);
  frame.drawRoundRect(22, 89, 128, 105, 18, C_CYAN);
  frame.fillRoundRect(35, 103, 102, 55, 8, C_NAVY);
  frame.drawRoundRect(35, 103, 102, 55, 8, C_BORDER);
  centered(frame, "POCKET", 112, 2, C_TEXT);
  centered(frame, "GAME", 136, 2, C_GOLD);

  frame.fillCircle(54, 177, 9, C_PANEL_2);
  frame.fillCircle(118, 177, 9, C_ORANGE);
  frame.drawFastHLine(45, 177, 18, C_MUTED);
  frame.drawFastVLine(54, 168, 18, C_MUTED);

  centered(frame, "ONE BUTTON ARCADE", 224, 1, C_MUTED);
  const int16_t progress =
      (int16_t)((now - screenStartedAt_) * 116U / config::BOOT_SCREEN_MS);
  frame.drawRoundRect(28, 255, 116, 10, 5, C_BORDER);
  frame.fillRoundRect(30, 257, progress > 112 ? 112 : progress, 6, 3, C_CYAN);
}

void PocketGameSystem::renderHome(uint32_t now) {
  GFXcanvas16 &frame = display_.canvas();
  frame.fillScreen(C_BLACK);
  frame.fillRect(0, 0, config::SCREEN_WIDTH, 61, C_NAVY);
  frame.drawFastHLine(0, 60, config::SCREEN_WIDTH, C_CYAN);
  centered(frame, "POCKETGAME", 14, 2, C_TEXT);
  centered(frame, "MAIN MENU", 41, 1, C_MUTED);

  const uint8_t selected = homeMenu_.selected();
  for (uint8_t index = 0; index < 2; ++index) {
    const int16_t y = 82 + index * 82;
    const bool active = index == selected;
    frame.fillRoundRect(12, y, 148, 67, 10, active ? C_PANEL_2 : C_PANEL);
    frame.drawRoundRect(12, y, 148, 67, 10, active ? C_GOLD : C_BORDER);
    if (index == 0) {
      drawPixelTower(frame, 43, y + 30, active ? C_CYAN : C_BORDER);
      textLeft(frame, "GAMES", 78, y + 17, 2, active ? C_TEXT : C_MUTED);
      char count[18];
      snprintf(count, sizeof(count), "%u INSTALLED", gameCount_);
      textLeft(frame, count, 78, y + 43, 1, C_MUTED);
    } else {
      drawGear(frame, 43, y + 33, active ? C_ORANGE : C_BORDER);
      textLeft(frame, "SETTINGS", 76, y + 17, 1, active ? C_TEXT : C_MUTED);
      textLeft(frame, "DISPLAY", 76, y + 40, 1, C_MUTED);
    }
  }

  if (((now / 350U) & 1U) == 0) {
    centered(frame, "CLICK TO CYCLE", 260, 1, C_CYAN);
  } else {
    centered(frame, "HOLD TO SELECT", 260, 1, C_GOLD);
  }
  drawControlHint(frame, "CLICK  NEXT", "HOLD  OPEN");
}

void PocketGameSystem::renderGames(uint32_t now) {
  GFXcanvas16 &frame = display_.canvas();
  frame.fillScreen(C_BLACK);
  frame.fillRect(0, 0, config::SCREEN_WIDTH, 54, C_NAVY);
  frame.drawFastHLine(0, 53, config::SCREEN_WIDTH, C_CYAN);
  textLeft(frame, "<", 10, 18, 2, C_MUTED);
  centered(frame, "GAME LIBRARY", 18, 1, C_TEXT);

  const uint8_t selected = gameMenu_.selected();
  if (selected >= gameCount_) {
    frame.fillRoundRect(12, 82, 148, 154, 12, C_PANEL);
    frame.drawRoundRect(12, 82, 148, 154, 12, C_GOLD);
    frame.drawFastHLine(52, 141, 68, C_GOLD);
    frame.fillTriangle(52, 141, 65, 132, 65, 150, C_GOLD);
    centered(frame, "BACK", 169, 2, C_TEXT);
    centered(frame, "RETURN TO MAIN MENU", 202, 1, C_MUTED);
  } else {
    IGame *game = games_[selected];
    frame.fillRoundRect(9, 67, 154, 184, 12, C_PANEL);
    frame.drawRoundRect(9, 67, 154, 184, 12, C_BORDER);
    frame.fillRoundRect(18, 78, 136, 91, 8, C_NAVY);
    frame.drawRoundRect(18, 78, 136, 91, 8, C_CYAN);

    if (strcmp(game->id(), "void-ascent") == 0) {
      drawPixelRocket(frame, 86, 116, C_ORANGE);
    } else if (strcmp(game->id(), "city-tower") == 0) {
      drawPixelTower(frame, 86, 114, C_CYAN);
    } else {
      frame.fillRoundRect(61, 96, 50, 50, 9, C_PANEL_2);
      centered(frame, "PG", 112, 2, C_GOLD);
    }

    centered(frame, game->title(), 184, 1, C_GOLD);
    centered(frame, game->description(), 207, 1, C_TEXT);

    char counter[20];
    snprintf(counter, sizeof(counter), "%u / %u", selected + 1, gameCount_);
    centered(frame, counter, 231, 1, C_MUTED);
  }

  if (((now / 330U) & 1U) == 0) {
    centered(frame, "HOLD TO OPEN", 265, 1, C_TEXT);
  }
  drawControlHint(frame, "CLICK  NEXT", "HOLD  SELECT");
}

void PocketGameSystem::renderSettings(uint32_t now) {
  (void)now;
  GFXcanvas16 &frame = display_.canvas();
  frame.fillScreen(C_BLACK);
  frame.fillRect(0, 0, config::SCREEN_WIDTH, 58, C_NAVY);
  frame.drawFastHLine(0, 57, config::SCREEN_WIDTH, C_ORANGE);
  centered(frame, "SETTINGS", 15, 2, C_TEXT);
  centered(frame, "DISPLAY ONLY", 41, 1, C_MUTED);

  frame.fillRoundRect(12, 80, 148, 160, 12, C_PANEL);
  frame.drawRoundRect(12, 80, 148, 160, 12, C_BORDER);
  drawGear(frame, 86, 113, C_ORANGE);
  centered(frame, "BRIGHTNESS", 149, 1, C_TEXT);

  char value[12];
  snprintf(value, sizeof(value), "%u%%", brightness_);
  centered(frame, value, 174, 2, C_GOLD);

  frame.drawRoundRect(24, 207, 124, 14, 5, C_BORDER);
  const int16_t fillWidth =
      (int16_t)((brightness_ - config::DISPLAY_MIN_BRIGHTNESS) * 118U /
                (config::DISPLAY_MAX_BRIGHTNESS -
                 config::DISPLAY_MIN_BRIGHTNESS));
  frame.fillRoundRect(27, 210, fillWidth + 1, 8, 3, C_CYAN);

  centered(frame, "100% = SAFE 60% HARDWARE", 250, 1, C_MUTED);
  centered(frame, "CLICK: +5%", 270, 1, C_CYAN);
  drawControlHint(frame, "CLICK  CHANGE", "HOLD  SAVE");
}

void PocketGameSystem::showHardwareFault() {
  while (true) {
    led_.set(240, 0, 0);
    delay(160);
    led_.off();
    delay(160);
  }
}

} // namespace pocketgame
