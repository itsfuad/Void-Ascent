#pragma once

#include <Arduino.h>
#include <FS.h>

namespace pocketgame {

// Shared access to the board's removable TF card. This is intentionally
// separate from PocketStorage: saves/settings can remain in on-chip NVS while
// large media files are streamed from the card.
class PocketMediaCard {
public:
  bool mount();
  void unmount();

  bool mounted() const { return mounted_; }
  bool exists(const char *path) const;
  fs::File open(const char *path, const char *mode = FILE_READ) const;

  uint64_t cardSizeBytes() const;
  uint64_t usedBytes() const;

private:
  bool mounted_ = false;
  bool busConfigured_ = false;
};

} // namespace pocketgame
