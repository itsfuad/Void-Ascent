#include "PocketGameConfig.h"

#if POCKETGAME_ENABLE_SD_STORAGE

#include "SdStorageBackend.h"

#include <string.h>

namespace pocketgame {

bool SdStorageBackend::safeName(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }

  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    const char value = *cursor;
    const bool valid = (value >= 'a' && value <= 'z') ||
                       (value >= 'A' && value <= 'Z') ||
                       (value >= '0' && value <= '9') || value == '_' ||
                       value == '-';
    if (!valid) {
      return false;
    }
  }

  return true;
}

bool SdStorageBackend::open(const char *nameSpace) {
  close();
  if (!safeName(nameSpace) || root_ == nullptr) {
    return false;
  }

  mounted_ = SD.begin(chipSelectPin_);
  if (!mounted_) {
    return false;
  }

  SD.mkdir(root_);
  snprintf(directory_, sizeof(directory_), "%s/%s", root_, nameSpace);
  if (!SD.mkdir(directory_) && !SD.exists(directory_)) {
    close();
    return false;
  }

  return true;
}

void SdStorageBackend::close() {
  if (mounted_) {
    SD.end();
  }
  mounted_ = false;
  directory_[0] = '\0';
}

bool SdStorageBackend::pathFor(const char *key, char *path,
                               size_t pathSize) const {
  if (!mounted_ || !safeName(key) || path == nullptr || pathSize == 0) {
    return false;
  }

  const int written = snprintf(path, pathSize, "%s/%s.bin", directory_, key);
  return written > 0 && (size_t)written < pathSize;
}

bool SdStorageBackend::contains(const char *key) {
  char path[96];
  return pathFor(key, path, sizeof(path)) && SD.exists(path);
}

size_t SdStorageBackend::read(const char *key, void *destination, size_t size) {
  char path[96];
  if (!pathFor(key, path, sizeof(path)) || destination == nullptr || size == 0) {
    return 0;
  }

  File file = SD.open(path, FILE_READ);
  if (!file || file.size() != size) {
    if (file) {
      file.close();
    }
    return 0;
  }

  const size_t readCount = file.read((uint8_t *)destination, size);
  file.close();
  return readCount;
}

bool SdStorageBackend::write(const char *key, const void *source, size_t size) {
  char path[96];
  if (!pathFor(key, path, sizeof(path)) || source == nullptr || size == 0) {
    return false;
  }

  SD.remove(path);
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }

  const size_t written = file.write((const uint8_t *)source, size);
  file.close();
  return written == size;
}

bool SdStorageBackend::remove(const char *key) {
  char path[96];
  return pathFor(key, path, sizeof(path)) && SD.remove(path);
}

bool SdStorageBackend::clear() {
  if (!mounted_) {
    return false;
  }

  File directory = SD.open(directory_);
  if (!directory || !directory.isDirectory()) {
    return false;
  }

  while (true) {
    File entry = directory.openNextFile();
    if (!entry) {
      break;
    }

    char path[96];
    const char *entryName = entry.name();
    if (entryName != nullptr && entryName[0] == '/') {
      snprintf(path, sizeof(path), "%s", entryName);
    } else {
      snprintf(path, sizeof(path), "%s/%s", directory_,
               entryName != nullptr ? entryName : "");
    }
    entry.close();
    SD.remove(path);
  }

  directory.close();
  return true;
}

} // namespace pocketgame

#endif
