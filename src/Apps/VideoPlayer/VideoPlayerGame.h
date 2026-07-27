#pragma once

#include "../../PocketGame/PocketGame.h"

class VideoPlayerGame final : public pocketgame::IGame {
public:
  const char *id() const override { return "video-player"; }
  const char *title() const override { return "VIDEO PLAYER"; }
  const char *description() const override { return "PLAY SD CARD VIDEOS"; }
  const char *storageNamespace() const override { return "videoplayer"; }

  void begin(pocketgame::PocketGameSystem &system) override;
  void loop(pocketgame::PocketGameSystem &system, uint32_t now) override;
};
