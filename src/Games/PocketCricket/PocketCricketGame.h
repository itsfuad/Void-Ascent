#pragma once

#include "../../PocketGame/PocketGame.h"

class PocketCricketGame final : public pocketgame::IGame {
public:
  const char *id() const override { return "pocket-cricket"; }
  const char *title() const override { return "POCKET CRICKET"; }
  const char *description() const override { return "TIME THE PERFECT SHOT"; }
  const char *storageNamespace() const override { return "cricket"; }

  void begin(pocketgame::PocketGameSystem &system) override;
  void loop(pocketgame::PocketGameSystem &system, uint32_t now) override;
};
