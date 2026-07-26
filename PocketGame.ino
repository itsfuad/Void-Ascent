#include "src/PocketGame/PocketGame.h"
#include "src/PocketGame/PreferencesStorageBackend.h"
#include "src/Games/VoidAscent/VoidAscentGame.h"
#include "src/Games/CityTower/CityTowerGame.h"

// Firmware composition lives only here. Both games receive the same display,
// single-button controls, LED, navigation, and media-independent storage APIs.
pocketgame::SingleButtonControlSource controls;
pocketgame::PreferencesStorageBackend storageBackend;
pocketgame::PocketGameSystem pocketGame(controls, storageBackend);

VoidAscentGame voidAscent;
CityTowerGame cityTower;

void setup() {
  pocketGame.registerGame(voidAscent);
  pocketGame.registerGame(cityTower);
  pocketGame.begin();
}

void loop() { pocketGame.loop(); }
