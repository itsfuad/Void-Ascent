#include "src/PocketGame/PocketGame.h"
#include "src/PocketGame/PreferencesStorageBackend.h"
#include "src/Games/VoidAscent/VoidAscentGame.h"

// Hardware and storage composition is centralized here. Games only receive the
// PocketGame APIs and do not know which pins or storage medium are in use.
pocketgame::SingleButtonControlSource controls;
pocketgame::PreferencesStorageBackend storageBackend;
pocketgame::PocketGameSystem pocketGame(controls, storageBackend);

VoidAscentGame voidAscent;

void setup() {
  pocketGame.registerGame(voidAscent);
  pocketGame.begin();
}

void loop() { pocketGame.loop(); }
