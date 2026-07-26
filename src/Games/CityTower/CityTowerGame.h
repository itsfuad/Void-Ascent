#pragma once

#include "../../PocketGame/PocketGame.h"

class CityTowerGame final : public pocketgame::IGame {
public:
  const char *id() const override { return "city-tower"; }
  const char *title() const override { return "CITY TOWER"; }
  const char *description() const override { return "STACK A LIVING CITY"; }
  const char *storageNamespace() const override { return "citytower"; }

  void begin(pocketgame::PocketGameSystem &system) override;
  void loop(pocketgame::PocketGameSystem &system, uint32_t now) override;
};
