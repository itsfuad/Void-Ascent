#include "PocketGame.h"

#include <math.h>
#include <string.h>

namespace pocketgame {

namespace {

static constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((uint16_t)(red & 0xF8) << 8) |
         ((uint16_t)(green & 0xFC) << 3) |
         ((uint16_t)blue >> 3);
}

static constexpr uint16_t C_BLACK = rgb565(3, 7, 14);
static constexpr uint16_t C_NAVY = rgb565(7, 18, 36);
static constexpr uint16_t C_PANEL = rgb565(13, 29, 48);
static constexpr uint16_t C_PANEL_2 = rgb565(20, 42, 67);
static constexpr uint16_t C_BORDER = rgb565(48, 83, 115);
static constexpr uint16_t C_TEXT = rgb565(239, 246, 247);
static constexpr uint16_t C_MUTED = rgb565(119, 149, 171);
static constexpr uint16_t C_CYAN = rgb565(48, 200, 231);
static constexpr uint16_t C_BLUE = rgb565(54, 114, 210);
static constexpr uint16_t C_GOLD = rgb565(255, 198, 68);
static constexpr uint16_t C_ORANGE = rgb565(244, 116, 57);
static constexpr uint16_t C_RED = rgb565(229, 66, 72);
static constexpr uint16_t C_GREEN = rgb565(62, 202, 133);
static constexpr uint16_t C_BAT = rgb565(235, 211, 148);

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
  textLeft(frame, text,
           (config::SCREEN_WIDTH - textWidth(text, size)) / 2,
           y, size, colour);
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

static void drawControlHint(GFXcanvas16 &frame, const char *left,
                            const char *right) {
  frame.fillRoundRect(8, 287, 156, 24, 6, C_PANEL);
  frame.drawRoundRect(8, 287, 156, 24, 6, C_BORDER);
  textLeft(frame, left, 15, 295, 1, C_MUTED);
  textLeft(frame, right, 157 - textWidth(right, 1), 295, 1, C_TEXT);
}

static void drawGamesIcon(GFXcanvas16 &frame, int16_t centreX, int16_t centreY,
                          bool selected) {
  const uint16_t outline = selected ? C_CYAN : C_BORDER;
  const uint16_t accent = selected ? C_GOLD : C_MUTED;

  frame.drawRoundRect(centreX - 20, centreY - 18, 40, 36, 6, outline);
  frame.drawFastHLine(centreX - 14, centreY - 9, 28, outline);
  frame.drawFastHLine(centreX - 14, centreY + 1, 28, outline);
  frame.drawFastHLine(centreX - 14, centreY + 11, 28, outline);

  for (uint8_t row = 0; row < 3; ++row) {
    frame.fillRect(centreX - 14, centreY - 13 + row * 10, 5, 5, accent);
    frame.fillRect(centreX - 5, centreY - 13 + row * 10, 5, 5, outline);
  }
}

static void drawGearIcon(GFXcanvas16 &frame, int16_t centreX, int16_t centreY,
                         bool selected) {
  const uint16_t outline = selected ? C_ORANGE : C_BORDER;
  const uint16_t inner = selected ? C_CYAN : C_MUTED;

  for (uint8_t index = 0; index < 8; ++index) {
    const float angle = index * 0.78539816f;
    const int16_t x = centreX + (int16_t)roundf(cosf(angle) * 16.0f);
    const int16_t y = centreY + (int16_t)roundf(sinf(angle) * 16.0f);
    frame.fillRoundRect(x - 3, y - 3, 7, 7, 2, outline);
  }

  frame.fillCircle(centreX, centreY, 14, outline);
  frame.fillCircle(centreX, centreY, 8, C_PANEL);
  frame.drawCircle(centreX, centreY, 7, inner);
  frame.fillCircle(centreX, centreY, 3, inner);
}

static void drawRocketIcon(GFXcanvas16 &frame, int16_t x, int16_t y) {
  frame.drawCircle(x - 35, y - 25, 1, C_TEXT);
  frame.drawCircle(x + 34, y - 17, 1, C_TEXT);
  frame.drawPixel(x + 28, y + 29, C_CYAN);
  frame.drawPixel(x - 28, y + 24, C_CYAN);

  frame.fillTriangle(x, y - 31, x - 12, y - 12, x + 12, y - 12, C_TEXT);
  frame.fillRoundRect(x - 10, y - 14, 20, 34, 4, C_TEXT);
  frame.fillRect(x - 10, y + 2, 20, 7, C_RED);
  frame.fillCircle(x, y - 5, 4, C_NAVY);
  frame.fillTriangle(x - 10, y + 10, x - 19, y + 23, x - 9, y + 20, C_RED);
  frame.fillTriangle(x + 10, y + 10, x + 19, y + 23, x + 9, y + 20, C_RED);
  frame.fillTriangle(x - 5, y + 20, x, y + 36, x + 5, y + 20, C_ORANGE);
  frame.fillTriangle(x - 3, y + 20, x, y + 30, x + 3, y + 20, C_GOLD);
}

static void drawTowerIcon(GFXcanvas16 &frame, int16_t x, int16_t y) {
  frame.drawFastHLine(x - 42, y - 33, 84, C_GOLD);
  frame.drawFastVLine(x, y - 33, 18, C_BORDER);
  frame.fillCircle(x, y - 14, 3, C_GOLD);

  for (uint8_t floor = 0; floor < 3; ++floor) {
    const int16_t width = 38 - floor * 2;
    const int16_t floorY = y + 21 - floor * 20;
    const uint16_t body = floor == 0 ? C_BLUE : (floor == 1 ? C_RED : C_CYAN);
    frame.fillRect(x - width / 2 - 2, floorY - 2, width + 4, 18, C_NAVY);
    frame.fillRect(x - width / 2, floorY, width, 14, body);
    frame.drawRect(x - width / 2, floorY, width, 14, C_TEXT);
    frame.fillRect(x - width / 2 + 5, floorY + 4, 5, 5, C_GOLD);
    frame.fillRect(x + width / 2 - 10, floorY + 4, 5, 5, C_GOLD);
  }

  frame.drawFastHLine(x - 31, y + 39, 62, C_BORDER);
}

static void drawVideoIcon(GFXcanvas16 &frame, int16_t x, int16_t y) {
  frame.fillRoundRect(x - 38, y - 27, 76, 54, 9, C_NAVY);
  frame.drawRoundRect(x - 38, y - 27, 76, 54, 9, C_CYAN);
  frame.fillTriangle(x - 8, y - 15, x - 8, y + 15, x + 18, y, C_GOLD);

  for (int16_t offset = -31; offset <= 25; offset += 14) {
    frame.fillRect(x + offset, y - 22, 7, 4, C_BORDER);
    frame.fillRect(x + offset, y + 19, 7, 4, C_BORDER);
  }
}

static void drawCricketIcon(GFXcanvas16 &frame, int16_t x, int16_t y) {
  frame.drawFastVLine(x - 16, y - 10, 30, C_BAT);
  frame.drawFastVLine(x - 10, y - 10, 30, C_BAT);
  frame.drawFastVLine(x - 4, y - 10, 30, C_BAT);
  frame.drawFastHLine(x - 18, y - 10, 8, C_TEXT);
  frame.drawFastHLine(x - 8, y - 10, 7, C_TEXT);

  frame.drawLine(x + 8, y + 24, x + 31, y - 22, C_NAVY);
  frame.drawLine(x + 9, y + 24, x + 32, y - 22, C_BAT);
  frame.fillRoundRect(x + 24, y - 29, 10, 19, 3, C_BAT);
  frame.drawRoundRect(x + 24, y - 29, 10, 19, 3, C_TEXT);

  frame.fillCircle(x + 19, y + 18, 7, C_RED);
  frame.drawPixel(x + 17, y + 16, C_TEXT);
  frame.drawLine(x + 15, y + 14, x + 23, y + 22, C_TEXT);
}

static void drawGameIcon(GFXcanvas16 &frame, const char *id,
                         int16_t centreX, int16_t centreY) {
  frame.fillRoundRect(23, 75, 126, 105, 9, C_NAVY);
  frame.drawRoundRect(23, 75, 126, 105, 9, C_CYAN);

  if (strcmp(id, "void-ascent") == 0) {
    drawRocketIcon(frame, centreX, centreY);
  } else if (strcmp(id, "city-tower") == 0) {
    drawTowerIcon(frame, centreX, centreY);
  } else if (strcmp(id, "video-player") == 0) {
    drawVideoIcon(frame, centreX, centreY);
  } else if (strcmp(id, "pocket-cricket") == 0) {
    drawCricketIcon(frame, centreX, centreY);
  } else {
    frame.fillRoundRect(centreX - 24, centreY - 24, 48, 48, 8, C_PANEL_2);
    frame.drawRoundRect(centreX - 24, centreY - 24, 48, 48, 8, C_BORDER);
    centered(frame, "PG", centreY - 7, 2, C_GOLD);
  }
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
  gameMenu_.setItemCount(gameCount_ + 1);
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

  homeMenu_.reset(2, 0);
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

  centered(frame, "POCKETGAME", 112, 2, C_TEXT);
  centered(frame, "ONE BUTTON ARCADE", 143, 1, C_MUTED);

  frame.drawRoundRect(27, 194, 118, 12, 6, C_BORDER);

  uint32_t elapsed = now - screenStartedAt_;
  if (elapsed > config::BOOT_SCREEN_MS) {
    elapsed = config::BOOT_SCREEN_MS;
  }
  const int16_t width =
      (int16_t)(elapsed * 112U / config::BOOT_SCREEN_MS);
  frame.fillRoundRect(30, 197, width, 6, 3, C_CYAN);
}

void PocketGameSystem::renderHome(uint32_t now) {
  GFXcanvas16 &frame = display_.canvas();
  frame.fillScreen(C_BLACK);

  frame.fillRect(0, 0, config::SCREEN_WIDTH, 58, C_NAVY);
  frame.drawFastHLine(0, 57, config::SCREEN_WIDTH, C_CYAN);
  centered(frame, "POCKETGAME", 13, 2, C_TEXT);
  centered(frame, "MAIN MENU", 40, 1, C_MUTED);

  const uint8_t selected = homeMenu_.selected();
  for (uint8_t index = 0; index < 2; ++index) {
    const int16_t y = 79 + index * 79;
    const bool active = selected == index;

    frame.fillRoundRect(13, y, 146, 63, 9, active ? C_PANEL_2 : C_PANEL);
    frame.drawRoundRect(13, y, 146, 63, 9, active ? C_ORANGE : C_BORDER);

    if (index == 0) {
      drawGamesIcon(frame, 43, y + 31, active);
      textLeft(frame, "GAMES", 76, y + 14, 2, active ? C_TEXT : C_MUTED);
      char count[20];
      snprintf(count, sizeof(count), "%u INSTALLED", gameCount_);
      textLeft(frame, count, 76, y + 40, 1, C_MUTED);
    } else {
      drawGearIcon(frame, 43, y + 31, active);
      textLeft(frame, "SETTINGS", 76, y + 15, 1, active ? C_TEXT : C_MUTED);
      textLeft(frame, "DISPLAY", 76, y + 38, 1, C_MUTED);
    }

    if (active) {
      frame.fillTriangle(150, y + 26, 156, y + 31, 150, y + 36, C_GOLD);
    }
  }

  centered(frame, ((now / 420U) & 1U) == 0 ? "CLICK TO CYCLE"
                                             : "HOLD TO SELECT",
           255, 1, ((now / 420U) & 1U) == 0 ? C_CYAN : C_GOLD);
  drawControlHint(frame, "CLICK  NEXT", "HOLD  OPEN");
}

void PocketGameSystem::renderGames(uint32_t now) {
  (void)now;
  GFXcanvas16 &frame = display_.canvas();
  frame.fillScreen(C_BLACK);

  frame.fillRect(0, 0, config::SCREEN_WIDTH, 54, C_NAVY);
  frame.drawFastHLine(0, 53, config::SCREEN_WIDTH, C_CYAN);
  textLeft(frame, "<", 10, 18, 2, C_CYAN);
  centered(frame, "GAME LIBRARY", 18, 1, C_TEXT);

  const uint8_t selected = gameMenu_.selected();
  frame.fillRoundRect(10, 66, 152, 185, 11, C_PANEL);
  frame.drawRoundRect(10, 66, 152, 185, 11, C_BORDER);

  if (selected >= gameCount_) {
    frame.fillRoundRect(23, 75, 126, 105, 9, C_NAVY);
    frame.drawRoundRect(23, 75, 126, 105, 9, C_GOLD);
    frame.drawFastHLine(48, 127, 76, C_GOLD);
    frame.fillTriangle(48, 127, 62, 117, 62, 137, C_GOLD);
    centered(frame, "BACK", 194, 2, C_TEXT);
    centered(frame, "RETURN TO MAIN MENU", 221, 1, C_MUTED);
  } else {
    IGame *game = games_[selected];
    drawGameIcon(frame, game->id(), 86, 126);
    centered(frame, game->title(), 192, 1, C_GOLD);
    centered(frame, game->description(), 215, 1, C_TEXT);

    char counter[18];
    snprintf(counter, sizeof(counter), "%u / %u", selected + 1, gameCount_);
    centered(frame, counter, 237, 1, C_MUTED);
  }

  centered(frame, "HOLD TO OPEN", 263, 1, C_TEXT);
  drawControlHint(frame, "CLICK  NEXT", "HOLD  SELECT");
}

void PocketGameSystem::renderSettings(uint32_t now) {
  (void)now;
  GFXcanvas16 &frame = display_.canvas();
  frame.fillScreen(C_BLACK);

  frame.fillRect(0, 0, config::SCREEN_WIDTH, 58, C_NAVY);
  frame.drawFastHLine(0, 57, config::SCREEN_WIDTH, C_ORANGE);
  centered(frame, "SETTINGS", 15, 2, C_TEXT);
  centered(frame, "DISPLAY", 41, 1, C_MUTED);

  frame.fillRoundRect(13, 80, 146, 156, 11, C_PANEL);
  frame.drawRoundRect(13, 80, 146, 156, 11, C_BORDER);
  drawGearIcon(frame, 86, 116, true);
  centered(frame, "BRIGHTNESS", 151, 1, C_TEXT);

  char value[12];
  snprintf(value, sizeof(value), "%u%%", brightness_);
  centered(frame, value, 176, 2, C_GOLD);

  frame.drawRoundRect(24, 209, 124, 13, 5, C_BORDER);
  const int16_t fillWidth =
      (int16_t)((brightness_ - config::DISPLAY_MIN_BRIGHTNESS) * 118U /
                (config::DISPLAY_MAX_BRIGHTNESS -
                 config::DISPLAY_MIN_BRIGHTNESS));
  frame.fillRoundRect(27, 212, fillWidth + 1, 7, 3, C_CYAN);

  centered(frame, "100% = SAFE 60% HARDWARE", 250, 1, C_MUTED);
  centered(frame, "CLICK +5%  HOLD SAVE", 270, 1, C_CYAN);
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
