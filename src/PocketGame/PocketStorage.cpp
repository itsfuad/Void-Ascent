#include "PocketStorage.h"

namespace pocketgame {

bool PocketStorage::open(const char *nameSpace) {
  close();
  ready_ = nameSpace != nullptr && nameSpace[0] != '\0' && backend_.open(nameSpace);
  return ready_;
}

void PocketStorage::close() {
  if (ready_) {
    backend_.close();
  }
  ready_ = false;
}

uint8_t PocketStorage::getUInt8(const char *key, uint8_t fallback) const {
  return getValue<uint8_t>(key, fallback);
}

uint16_t PocketStorage::getUInt16(const char *key, uint16_t fallback) const {
  return getValue<uint16_t>(key, fallback);
}

uint32_t PocketStorage::getUInt32(const char *key, uint32_t fallback) const {
  return getValue<uint32_t>(key, fallback);
}

int32_t PocketStorage::getInt32(const char *key, int32_t fallback) const {
  return getValue<int32_t>(key, fallback);
}

bool PocketStorage::getBool(const char *key, bool fallback) const {
  const uint8_t stored = getValue<uint8_t>(key, fallback ? 1U : 0U);
  return stored != 0;
}

bool PocketStorage::putUInt8(const char *key, uint8_t value) {
  return putValue(key, value);
}

bool PocketStorage::putUInt16(const char *key, uint16_t value) {
  return putValue(key, value);
}

bool PocketStorage::putUInt32(const char *key, uint32_t value) {
  return putValue(key, value);
}

bool PocketStorage::putInt32(const char *key, int32_t value) {
  return putValue(key, value);
}

bool PocketStorage::putBool(const char *key, bool value) {
  const uint8_t stored = value ? 1U : 0U;
  return putValue(key, stored);
}

size_t PocketStorage::getBytes(const char *key, void *destination,
                               size_t size) const {
  return ready_ ? backend_.read(key, destination, size) : 0;
}

bool PocketStorage::putBytes(const char *key, const void *source, size_t size) {
  return ready_ && backend_.write(key, source, size);
}

} // namespace pocketgame
