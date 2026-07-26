#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace pocketgame {

// Storage medium interface. Games only see PocketStorage; they never include
// Preferences, SD, LittleFS, or any board-specific storage API.
class IStorageBackend {
public:
  virtual ~IStorageBackend() = default;
  virtual bool open(const char *nameSpace) = 0;
  virtual void close() = 0;
  virtual bool contains(const char *key) = 0;
  virtual size_t read(const char *key, void *destination, size_t size) = 0;
  virtual bool write(const char *key, const void *source, size_t size) = 0;
  virtual bool remove(const char *key) = 0;
  virtual bool clear() = 0;
};

class PocketStorage {
public:
  explicit PocketStorage(IStorageBackend &backend) : backend_(backend) {}

  bool open(const char *nameSpace);
  void close();
  bool ready() const { return ready_; }

  bool contains(const char *key) { return ready_ && backend_.contains(key); }
  bool remove(const char *key) { return ready_ && backend_.remove(key); }
  bool clear() { return ready_ && backend_.clear(); }

  uint8_t getUInt8(const char *key, uint8_t fallback) const;
  uint16_t getUInt16(const char *key, uint16_t fallback) const;
  uint32_t getUInt32(const char *key, uint32_t fallback) const;
  int32_t getInt32(const char *key, int32_t fallback) const;
  bool getBool(const char *key, bool fallback) const;

  bool putUInt8(const char *key, uint8_t value);
  bool putUInt16(const char *key, uint16_t value);
  bool putUInt32(const char *key, uint32_t value);
  bool putInt32(const char *key, int32_t value);
  bool putBool(const char *key, bool value);

  size_t getBytes(const char *key, void *destination, size_t size) const;
  bool putBytes(const char *key, const void *source, size_t size);

private:
  IStorageBackend &backend_;
  bool ready_ = false;

  template <typename T> T getValue(const char *key, T fallback) const {
    if (!ready_) {
      return fallback;
    }

    T value{};
    return backend_.read(key, &value, sizeof(value)) == sizeof(value) ? value
                                                                     : fallback;
  }

  template <typename T> bool putValue(const char *key, const T &value) {
    return ready_ && backend_.write(key, &value, sizeof(value));
  }
};

} // namespace pocketgame
