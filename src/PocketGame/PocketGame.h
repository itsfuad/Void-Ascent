#pragma once

#include "PocketControls.h"
#include "PocketDisplay.h"
#include "PocketLed.h"
#include "PocketMediaCard.h"
#include "PocketNavigation.h"
#include "PocketStorage.h"

namespace pocketgame {

class PocketGameSystem;

class IGame {
public:
  virtual ~IGame() = default;

  virtual const char *id() const = 0;
  virtual const char *title() const = 0;
  virtual const char *description() const { return ""; }

  // Must be a storage-safe namespace. The backend decides where it resides.
  virtual const char *storageNamespace() const = 0;

  virtual void begin(PocketGameSystem &system) = 0;
  virtual void loop(PocketGameSystem &system, uint32_t now) = 0;
};

enum class SystemScreen : uint8_t { BOOT, HOME, GAMES, SETTINGS };

class PocketGameSystem {
public:
  PocketGameSystem(IControlSource &controlSource, IStorageBackend &storageBackend)
      : controls_(controlSource), storage_(storageBackend) {}

  bool registerGame(IGame &game);
  bool begin(uint32_t serialBaud = 115200);
  void loop();

  PocketDisplay &display() { return display_; }
  PocketControls &controls() { return controls_; }
  PocketLed &led() { return led_; }
  PocketMediaCard &mediaCard() { return mediaCard_; }
  PocketStorage &storage() { return storage_; }

  const PocketDisplay &display() const { return display_; }
  const PocketControls &controls() const { return controls_; }
  const PocketLed &led() const { return led_; }
  const PocketMediaCard &mediaCard() const { return mediaCard_; }
  const PocketStorage &storage() const { return storage_; }

  IGame *activeGame() const { return activeGame_; }
  uint8_t gameCount() const { return gameCount_; }
  uint8_t brightness() const { return brightness_; }

  // A game may return to PocketGame without knowing how the system screens are
  // drawn, where settings are stored, or how the controls are wired.
  void requestLauncher();

private:
  static constexpr const char *SYSTEM_STORAGE_NAMESPACE = "pocketgame";

  PocketDisplay display_;
  PocketControls controls_;
  PocketLed led_;
  PocketMediaCard mediaCard_;
  PocketStorage storage_;

  IGame *games_[config::MAX_REGISTERED_GAMES] = {};
  uint8_t gameCount_ = 0;
  IGame *activeGame_ = nullptr;

  SystemScreen systemScreen_ = SystemScreen::BOOT;
  MenuNavigator homeMenu_;
  MenuNavigator gameMenu_;
  uint32_t screenStartedAt_ = 0;
  uint32_t lastSystemFrameAt_ = 0;
  uint8_t brightness_ = config::DISPLAY_DEFAULT_BRIGHTNESS;
  bool begun_ = false;

  bool launch(uint8_t index);
  bool openSystemStorage();
  void setSystemScreen(SystemScreen screen, uint32_t now);
  void updateSystem(uint32_t now);
  void updateHome(uint32_t now);
  void updateGames(uint32_t now);
  void updateSettings(uint32_t now);
  void renderSystem(uint32_t now);
  void renderBoot(uint32_t now);
  void renderHome(uint32_t now);
  void renderGames(uint32_t now);
  void renderSettings(uint32_t now);
  void showHardwareFault();
};

} // namespace pocketgame
