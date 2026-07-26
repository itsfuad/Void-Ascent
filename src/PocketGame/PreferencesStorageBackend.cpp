#include "PreferencesStorageBackend.h"

namespace pocketgame {

bool PreferencesStorageBackend::open(const char *nameSpace) {
  close();
  open_ = preferences_.begin(nameSpace, false);
  return open_;
}

void PreferencesStorageBackend::close() {
  if (open_) {
    preferences_.end();
    open_ = false;
  }
}

bool PreferencesStorageBackend::contains(const char *key) {
  return open_ && preferences_.isKey(key);
}

size_t PreferencesStorageBackend::read(const char *key, void *destination,
                                       size_t size) {
  if (!open_ || destination == nullptr || size == 0) {
    return 0;
  }

  if (preferences_.getBytesLength(key) == size) {
    return preferences_.getBytes(key, destination, size);
  }

  // Compatibility migration for values written by the original game directly
  // through Preferences (UChar and Int) before storage was abstracted.
  const PreferenceType type = preferences_.getType(key);
  if (type == PT_U8 && size == sizeof(uint8_t)) {
    *static_cast<uint8_t *>(destination) = preferences_.getUChar(key, 0);
    return size;
  }

  if (type == PT_I32 && size == sizeof(int32_t)) {
    *static_cast<int32_t *>(destination) = preferences_.getInt(key, 0);
    return size;
  }

  if (type == PT_U32 && size == sizeof(uint32_t)) {
    *static_cast<uint32_t *>(destination) = preferences_.getUInt(key, 0);
    return size;
  }

  return 0;
}

bool PreferencesStorageBackend::write(const char *key, const void *source,
                                      size_t size) {
  return open_ && source != nullptr && size > 0 &&
         preferences_.putBytes(key, source, size) == size;
}

bool PreferencesStorageBackend::remove(const char *key) {
  return open_ && preferences_.remove(key);
}

bool PreferencesStorageBackend::clear() {
  return open_ && preferences_.clear();
}

} // namespace pocketgame
