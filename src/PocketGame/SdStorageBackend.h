#pragma once

#include "PocketGameConfig.h"

#if POCKETGAME_ENABLE_SD_STORAGE

#include <SD.h>

#include "PocketStorage.h"

namespace pocketgame {

// Optional removable-card backend. Enable POCKETGAME_ENABLE_SD_STORAGE and
// construct this backend in the sketch. Games continue using PocketStorage.
class SdStorageBackend final : public IStorageBackend {
public:
  explicit SdStorageBackend(uint8_t chipSelectPin,
                            const char *root = "/pocketgame")
      : chipSelectPin_(chipSelectPin), root_(root) {}

  bool open(const char *nameSpace) override;
  void close() override;
  bool contains(const char *key) override;
  size_t read(const char *key, void *destination, size_t size) override;
  bool write(const char *key, const void *source, size_t size) override;
  bool remove(const char *key) override;
  bool clear() override;

private:
  uint8_t chipSelectPin_;
  const char *root_;
  bool mounted_ = false;
  char directory_[64] = {};

  bool pathFor(const char *key, char *path, size_t pathSize) const;
  static bool safeName(const char *text);
};

} // namespace pocketgame

#endif
