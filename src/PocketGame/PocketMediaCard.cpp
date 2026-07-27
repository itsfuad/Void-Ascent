#include "PocketMediaCard.h"

#include <SD.h>
#include <SPI.h>

#include "PocketGameConfig.h"

namespace pocketgame {

bool PocketMediaCard::mount() {
  if (mounted_) {
    return true;
  }

  // Keep both SPI devices deselected while configuring the shared bus.
  pinMode(config::LCD_CS_PIN, OUTPUT);
  digitalWrite(config::LCD_CS_PIN, HIGH);
  pinMode(config::SD_CS_PIN, OUTPUT);
  digitalWrite(config::SD_CS_PIN, HIGH);

  if (!busConfigured_) {
    SPI.begin(config::SD_SCLK_PIN, config::SD_MISO_PIN,
              config::SD_MOSI_PIN, config::SD_CS_PIN);
    busConfigured_ = true;
  }

  mounted_ = SD.begin(config::SD_CS_PIN, SPI, config::SD_SPI_HZ);
  return mounted_;
}

void PocketMediaCard::unmount() {
  if (!mounted_) {
    return;
  }

  SD.end();
  mounted_ = false;
  digitalWrite(config::SD_CS_PIN, HIGH);
}

bool PocketMediaCard::exists(const char *path) const {
  return mounted_ && path != nullptr && SD.exists(path);
}

fs::File PocketMediaCard::open(const char *path, const char *mode) const {
  if (!mounted_ || path == nullptr) {
    return fs::File();
  }
  return SD.open(path, mode);
}

uint64_t PocketMediaCard::cardSizeBytes() const {
  return mounted_ ? SD.cardSize() : 0;
}

uint64_t PocketMediaCard::usedBytes() const {
  return mounted_ ? SD.usedBytes() : 0;
}

} // namespace pocketgame
