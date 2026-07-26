#include "PocketGame.h"

#include <string.h>

namespace pocketgame {

namespace {
static constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((uint16_t)(red & 0xF8) << 8) | ((uint16_t)(green & 0xFC) << 3) |
         ((uint16_t)blue >> 3);
}

static constexpr uint16_t C_BLACK = rgb565(2, 4, 8);
static constexpr uint16_t C_PANEL = rgb565(13, 19, 30);
static constexpr uint16_t C_BORDER = rgb565(43, 55, 73);
static constexpr uint16_t C_TEXT = rgb565(235, 241, 244);
static constexpr uint16_t C_MUTED = rgb565(136, 149, 164);
static constexpr uint16_t C_GREEN = rgb565(83, 235, 166);
static constexpr uint16_t C_CYAN = rgb565(54, 185, 216);

static int16_t textWidth(const char *text, uint8_t size) {
  return (int16_t)(strlen(text) * 6 * size);
}

static void centered(GFXcanvas16 &frame, const char *text, int16_t y,
                     uint8_t size, uint16_t colour) {
  frame.setTextWrap(false);
  frame.setTextSize(size);
  frame.setTextColor(colour);
  frame.setCursor((config::SCREEN_WIDTH - textWidth(text, size)) / 2, y);
  frame.print(text);
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
  launcherMenu_.setItemCount(gameCount_);
  return true;
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
  display_.clear(C_BLACK);
  display_.present();

  if (gameCount_ == 0) {
    Serial.println("PocketGame: no games registered.");
    renderLauncher(millis());
    return true;
  }

  if (gameCount_ == 1) {
    return launch(0);
  }

  launcherMenu_.reset(gameCount_, 0);
  lastLauncherFrameAt_ = 0;
  renderLauncher(millis());
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
    updateLauncher(now);
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
  controls_.consume();
  activeGame_->begin(*this);

  Serial.print("PocketGame: started ");
  Serial.println(activeGame_->title());
  return true;
}

void PocketGameSystem::requestLauncher() {
  if (gameCount_ <= 1) {
    return;
  }

  activeGame_ = nullptr;
  storage_.close();
  controls_.consume();
  lastLauncherFrameAt_ = 0;
  renderLauncher(millis());
}

void PocketGameSystem::updateLauncher(uint32_t now) {
  const MenuAction action = launcherMenu_.update(controls_.events());
  if (action == MenuAction::SELECTED) {
    launch(launcherMenu_.selected());
    return;
  }

  const bool frameDue = now - lastLauncherFrameAt_ >=
                        config::SYSTEM_FRAME_INTERVAL_MS;
  if (action == MenuAction::CYCLED || frameDue) {
    renderLauncher(now);
  }

  const uint8_t pulse = PocketLed::breatheValue(now, 1400, 22, 145);
  led_.set(0, pulse / 2, pulse);
  delay(1);
}

void PocketGameSystem::renderLauncher(uint32_t now) {
  lastLauncherFrameAt_ = now;
  GFXcanvas16 &frame = display_.canvas();
  frame.fillScreen(C_BLACK);

  centered(frame, "POCKETGAME", 24, 2, C_TEXT);
  centered(frame, "HOLD: CYCLE  TAP: SELECT", 52, 1, C_MUTED);

  if (gameCount_ == 0) {
    centered(frame, "NO GAMES REGISTERED", 146, 1, C_MUTED);
    display_.present();
    return;
  }

  const int16_t cardY = 102;
  frame.fillRoundRect(10, cardY, config::SCREEN_WIDTH - 20, 110, 10, C_PANEL);
  frame.drawRoundRect(10, cardY, config::SCREEN_WIDTH - 20, 110, 10,
                      C_BORDER);

  IGame *game = games_[launcherMenu_.selected()];
  centered(frame, game->title(), cardY + 24, 1, C_GREEN);
  centered(frame, game->description(), cardY + 49, 1, C_TEXT);

  char counter[20];
  snprintf(counter, sizeof(counter), "%u / %u", launcherMenu_.selected() + 1,
           gameCount_);
  centered(frame, counter, cardY + 80, 1, C_CYAN);

  if (((now / 360U) & 1U) == 0) {
    centered(frame, "TAP TO PLAY", 274, 1, C_TEXT);
  }

  display_.present();
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
