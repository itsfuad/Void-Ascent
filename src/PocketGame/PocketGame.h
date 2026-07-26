#pragma once

#include "PocketControls.h"
#include "PocketDisplay.h"
#include "PocketLed.h"
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
  PocketStorage &storage() { return storage_; }

  const PocketDisplay &display() const { return display_; }
  const PocketControls &controls() const { return controls_; }
  const PocketLed &led() const { return led_; }
  const PocketStorage &storage() const { return storage_; }

  IGame *activeGame() const { return activeGame_; }
  uint8_t gameCount() const { return gameCount_; }

  // A game may return to the shared launcher without knowing how the launcher
  // is drawn or how its controls are mapped.
  void requestLauncher();

private:
  PocketDisplay display_;
  PocketControls controls_;
  PocketLed led_;
  PocketStorage storage_;

  IGame *games_[config::MAX_REGISTERED_GAMES] = {};
  uint8_t gameCount_ = 0;
  IGame *activeGame_ = nullptr;
  MenuNavigator launcherMenu_;
  uint32_t lastLauncherFrameAt_ = 0;
  bool begun_ = false;

  bool launch(uint8_t index);
  void updateLauncher(uint32_t now);
  void renderLauncher(uint32_t now);
  void showHardwareFault();
};

} // namespace pocketgame
