#include "src/PocketGame/PocketGame.h"
#include "src/PocketGame/PreferencesStorageBackend.h"
#include "src/Games/VoidAscent/VoidAscentGame.h"
#include "src/Games/CityTower/CityTowerGame.h"
#include "src/Games/PocketCricket/PocketCricketGame.h"

// Firmware composition lives only here. Every game receives the same display,
// single-button controls, LED, navigation, and media-independent storage APIs.
pocketgame::SingleButtonControlSource controls;
pocketgame::PreferencesStorageBackend storageBackend;
pocketgame::PocketGameSystem pocketGame(controls, storageBackend);

VoidAscentGame voidAscent;
CityTowerGame cityTower;
PocketCricketGame pocketCricket;

void setup() {
  pocketGame.registerGame(voidAscent);
  pocketGame.registerGame(cityTower);
  pocketGame.registerGame(pocketCricket);
  pocketGame.begin();
}

void loop() { pocketGame.loop(); }
