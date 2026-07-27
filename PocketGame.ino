#include "src/PocketGame/PocketGame.h"
#include "src/PocketGame/PreferencesStorageBackend.h"
#include "src/Games/VoidAscent/VoidAscentGame.h"
#include "src/Games/CityTower/CityTowerGame.h"
#include "src/Apps/VideoPlayer/VideoPlayerGame.h"

// Firmware composition lives only here. Pocket Cricket remains in src/Games
// but is intentionally not registered, so it does not appear in the launcher.
pocketgame::SingleButtonControlSource controls;
pocketgame::PreferencesStorageBackend storageBackend;
pocketgame::PocketGameSystem pocketGame(controls, storageBackend);

VoidAscentGame voidAscent;
CityTowerGame cityTower;
VideoPlayerGame videoPlayer;

void setup() {
  pocketGame.registerGame(voidAscent);
  pocketGame.registerGame(cityTower);
  pocketGame.registerGame(videoPlayer);
  pocketGame.begin();
}

void loop() { pocketGame.loop(); }
