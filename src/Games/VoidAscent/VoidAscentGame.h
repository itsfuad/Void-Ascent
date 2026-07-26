#pragma once

#include "../../PocketGame/PocketGame.h"

class VoidAscentGame final : public pocketgame::IGame {
public:
  const char *id() const override { return "void-ascent"; }
  const char *title() const override { return "VOID ASCENT"; }
  const char *description() const override { return "ONE-BUTTON ROCKET"; }
  const char *storageNamespace() const override { return "voidascent"; }

  void begin(pocketgame::PocketGameSystem &system) override;
  void loop(pocketgame::PocketGameSystem &system, uint32_t now) override;
};
