#pragma once

#include <Preferences.h>

#include "PocketStorage.h"

namespace pocketgame {

// Default on-chip NVS backend. Replacing this backend does not require any
// change inside a game.
class PreferencesStorageBackend final : public IStorageBackend {
public:
  bool open(const char *nameSpace) override;
  void close() override;
  bool contains(const char *key) override;
  size_t read(const char *key, void *destination, size_t size) override;
  bool write(const char *key, const void *source, size_t size) override;
  bool remove(const char *key) override;
  bool clear() override;

private:
  Preferences preferences_;
  bool open_ = false;
};

} // namespace pocketgame
