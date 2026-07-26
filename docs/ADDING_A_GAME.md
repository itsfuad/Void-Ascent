# Adding another game

## 1. Create a game class

```cpp
#pragma once
#include "../../PocketGame/PocketGame.h"

class CityTowerGame final : public pocketgame::IGame {
public:
  const char *id() const override { return "city-tower"; }
  const char *title() const override { return "CITY TOWER"; }
  const char *description() const override { return "STACK THE CITY"; }
  const char *storageNamespace() const override { return "citytower"; }

  void begin(pocketgame::PocketGameSystem &system) override;
  void loop(pocketgame::PocketGameSystem &system, uint32_t now) override;
};
```

Use a short, unique storage namespace. ESP32 NVS namespaces should remain at most 15 characters.

## 2. Use shared services

```cpp
void CityTowerGame::begin(pocketgame::PocketGameSystem &system) {
  const int32_t best = system.storage().getInt32("best", 0);
  system.led().off();
  system.display().canvas().fillScreen(0);
}

void CityTowerGame::loop(pocketgame::PocketGameSystem &system, uint32_t now) {
  const auto &input = system.controls().events();

  if (input.actionPressed) {
    // Drop the moving floor immediately on the press edge.
  }

  if (input.cycle) {
    // Cycle a menu item.
  }

  if (input.select) {
    // Select the current menu item.
  }

  auto &frame = system.display().canvas();
  // Draw a complete RAM frame...
  system.display().present();
}
```

For menus, use `pocketgame::MenuNavigator`. For game screen state, use `pocketgame::ScreenNavigator<YourScreenEnum>`.

## 3. Register it

In `PocketGame_VoidAscent.ino`:

```cpp
#include "src/Games/CityTower/CityTowerGame.h"

CityTowerGame cityTower;

void setup() {
  pocketGame.registerGame(voidAscent);
  pocketGame.registerGame(cityTower);
  pocketGame.begin();
}
```

With multiple registered games, the system launcher appears automatically. Hold cycles through games and tap starts the selected game.

## 4. Changing physical controls

Games must not call `digitalRead()` directly. Create another `IControlSource` implementation that maps the new hardware to `ControlEvents`, then replace the control-source object in the `.ino`. Existing games remain unchanged.
