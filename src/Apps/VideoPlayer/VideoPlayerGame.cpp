#include "VideoPlayerGame.h"

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <FS.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

using pocketgame::MenuAction;
using pocketgame::MenuNavigator;
using pocketgame::PocketGameSystem;
using pocketgame::PocketLed;
using pocketgame::ScreenNavigator;

namespace {

static PocketGameSystem *pocketSystem = nullptr;
#define frame (pocketSystem->display().canvas())

static constexpr int16_t SCREEN_W = pocketgame::config::SCREEN_WIDTH;
static constexpr int16_t SCREEN_H = pocketgame::config::SCREEN_HEIGHT;
static constexpr uint32_t UI_FRAME_INTERVAL_MS = 33;
static constexpr uint8_t MAX_VIDEOS = 24;
static constexpr uint16_t CONVERSION_CHUNK_BYTES = 1024;
static constexpr uint32_t PGV_HEADER_BYTES = 32;
static constexpr const char *VIDEO_DIRECTORY = "/videos";

static constexpr uint8_t PIXEL_FORMAT_RGB332 = 1;
static constexpr uint8_t PIXEL_FORMAT_RGB565_LE = 2;

static constexpr uint16_t C565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((uint16_t)(red & 0xF8) << 8) |
         ((uint16_t)(green & 0xFC) << 3) |
         ((uint16_t)blue >> 3);
}

static constexpr uint16_t C_BLACK = C565(3, 7, 14);
static constexpr uint16_t C_NAVY = C565(7, 18, 36);
static constexpr uint16_t C_PANEL = C565(13, 29, 48);
static constexpr uint16_t C_PANEL_2 = C565(20, 42, 67);
static constexpr uint16_t C_BORDER = C565(48, 83, 115);
static constexpr uint16_t C_TEXT = C565(239, 246, 247);
static constexpr uint16_t C_MUTED = C565(119, 149, 171);
static constexpr uint16_t C_CYAN = C565(48, 200, 231);
static constexpr uint16_t C_GOLD = C565(255, 198, 68);
static constexpr uint16_t C_ORANGE = C565(244, 116, 57);
static constexpr uint16_t C_RED = C565(229, 66, 72);
static constexpr uint16_t C_GREEN = C565(62, 202, 133);

struct __attribute__((packed)) PgvHeader {
  char magic[4];
  uint16_t width;
  uint16_t height;
  uint16_t fpsTimes100;
  uint8_t pixelFormat;
  uint8_t flags;
  uint32_t frameCount;
  uint32_t frameBytes;
  uint8_t reserved[12];
};

static_assert(sizeof(PgvHeader) == PGV_HEADER_BYTES,
              "PGV header must remain 32 bytes");

struct VideoEntry {
  char path[96] = {};
  char title[25] = {};
};

enum class PlayerScreen : uint8_t {
  LIBRARY,
  PLAYING,
  PAUSED,
  NO_CARD,
  NO_VIDEOS,
  FILE_ERROR
};

struct PlayerState {
  ScreenNavigator<PlayerScreen> screens{PlayerScreen::LIBRARY};
  MenuNavigator pauseMenu{4, 0};

  VideoEntry videos[MAX_VIDEOS];
  uint8_t videoCount = 0;
  uint8_t selected = 0;
  bool backSelected = false;

  fs::File file;
  PgvHeader header{};
  uint32_t frameIndex = 0;
  uint32_t frameIntervalMs = 83;
  uint32_t nextFrameAt = 0;
  uint32_t overlayUntil = 0;
  uint32_t lastUiFrameAt = 0;

  char error[34] = {};
  uint8_t conversionBuffer[CONVERSION_CHUNK_BYTES] = {};
};

static PlayerState player;

static int16_t textWidth(const char *text, uint8_t size) {
  return (int16_t)(strlen(text) * 6 * size);
}

static void textLeft(const char *text, int16_t x, int16_t y,
                     uint8_t size, uint16_t colour) {
  frame.setTextWrap(false);
  frame.setTextSize(size);
  frame.setTextColor(colour);
  frame.setCursor(x, y);
  frame.print(text);
}

static void textRight(const char *text, int16_t rightX, int16_t y,
                      uint8_t size, uint16_t colour) {
  textLeft(text, rightX - textWidth(text, size), y, size, colour);
}

static void centered(const char *text, int16_t y, uint8_t size,
                     uint16_t colour) {
  textLeft(text, (frame.width() - textWidth(text, size)) / 2, y, size, colour);
}

static bool equalsIgnoreCase(char first, char second) {
  return tolower((unsigned char)first) == tolower((unsigned char)second);
}

static bool endsWithPgv(const char *name) {
  if (name == nullptr) {
    return false;
  }

  const size_t length = strlen(name);
  return length >= 4 && name[length - 4] == '.' &&
         equalsIgnoreCase(name[length - 3], 'p') &&
         equalsIgnoreCase(name[length - 2], 'g') &&
         equalsIgnoreCase(name[length - 1], 'v');
}

static int compareIgnoreCase(const char *first, const char *second) {
  while (*first != '\0' && *second != '\0') {
    const int a = tolower((unsigned char)*first);
    const int b = tolower((unsigned char)*second);
    if (a != b) {
      return a - b;
    }
    ++first;
    ++second;
  }
  return (unsigned char)*first - (unsigned char)*second;
}

static void titleFromPath(const char *path, char *title, size_t titleSize) {
  if (title == nullptr || titleSize == 0) {
    return;
  }

  title[0] = '\0';
  if (path == nullptr) {
    return;
  }

  const char *base = strrchr(path, '/');
  base = base == nullptr ? path : base + 1;

  size_t output = 0;
  while (*base != '\0' && output + 1 < titleSize) {
    if (*base == '.' && endsWithPgv(base)) {
      break;
    }

    char value = *base++;
    if (value == '_' || value == '-') {
      value = ' ';
    }
    title[output++] = (char)toupper((unsigned char)value);
  }
  title[output] = '\0';
}

static void setError(const char *message) {
  strncpy(player.error, message, sizeof(player.error) - 1);
  player.error[sizeof(player.error) - 1] = '\0';
}

static void closeVideo() {
  if (player.file) {
    player.file.close();
  }
  player.frameIndex = 0;
}

static void sortVideos() {
  for (uint8_t first = 0; first < player.videoCount; ++first) {
    for (uint8_t second = first + 1; second < player.videoCount; ++second) {
      if (compareIgnoreCase(player.videos[first].title,
                            player.videos[second].title) > 0) {
        VideoEntry temporary = player.videos[first];
        player.videos[first] = player.videos[second];
        player.videos[second] = temporary;
      }
    }
  }
}

static bool scanVideos(uint32_t now) {
  closeVideo();
  player.videoCount = 0;
  player.selected = 0;
  player.backSelected = false;

  pocketgame::PocketMediaCard &card = pocketSystem->mediaCard();
  if (!card.mount()) {
    setError("INSERT FAT32 SD CARD");
    player.screens.goTo(PlayerScreen::NO_CARD, now);
    return false;
  }

  fs::File directory = card.open(VIDEO_DIRECTORY);
  if (!directory || !directory.isDirectory()) {
    if (directory) {
      directory.close();
    }
    setError("CREATE /VIDEOS FOLDER");
    player.screens.goTo(PlayerScreen::NO_VIDEOS, now);
    return false;
  }

  while (player.videoCount < MAX_VIDEOS) {
    fs::File entry = directory.openNextFile();
    if (!entry) {
      break;
    }

    if (!entry.isDirectory() && endsWithPgv(entry.name())) {
      VideoEntry &video = player.videos[player.videoCount];
      const char *entryName = entry.name();
      if (entryName != nullptr && entryName[0] == '/') {
        snprintf(video.path, sizeof(video.path), "%s", entryName);
      } else {
        snprintf(video.path, sizeof(video.path), "%s/%s", VIDEO_DIRECTORY,
                 entryName != nullptr ? entryName : "");
      }
      titleFromPath(video.path, video.title, sizeof(video.title));
      ++player.videoCount;
    }
    entry.close();
  }
  directory.close();

  if (player.videoCount == 0) {
    setError("NO .PGV VIDEOS FOUND");
    player.screens.goTo(PlayerScreen::NO_VIDEOS, now);
    return false;
  }

  sortVideos();
  const uint8_t saved = pocketSystem->storage().getUInt8("selected", 0);
  player.selected = saved < player.videoCount ? saved : 0;
  player.screens.goTo(PlayerScreen::LIBRARY, now);
  return true;
}

static bool isLandscapeHeader(const PgvHeader &header) {
  return header.width == (uint16_t)SCREEN_H &&
         header.height == (uint16_t)SCREEN_W;
}

static bool isPortraitHeader(const PgvHeader &header) {
  return header.width == (uint16_t)SCREEN_W &&
         header.height == (uint16_t)SCREEN_H;
}

static bool currentVideoIsLandscape() {
  return isLandscapeHeader(player.header);
}

static bool validHeader(const PgvHeader &header, uint64_t fileSize) {
  if (memcmp(header.magic, "PGV1", 4) != 0 ||
      (!isPortraitHeader(header) && !isLandscapeHeader(header)) ||
      header.fpsTimes100 < 100 || header.fpsTimes100 > 6000 ||
      header.frameCount == 0) {
    return false;
  }

  const uint32_t pixelCount =
      (uint32_t)header.width * (uint32_t)header.height;
  uint32_t expectedFrameBytes = 0;
  if (header.pixelFormat == PIXEL_FORMAT_RGB332) {
    expectedFrameBytes = pixelCount;
  } else if (header.pixelFormat == PIXEL_FORMAT_RGB565_LE) {
    expectedFrameBytes = pixelCount * 2U;
  } else {
    return false;
  }

  if (header.frameBytes != expectedFrameBytes) {
    return false;
  }

  const uint64_t required = PGV_HEADER_BYTES +
                            (uint64_t)header.frameCount * header.frameBytes;
  return fileSize >= required;
}

static bool openSelectedVideo(uint32_t now, bool showOverlay) {
  if (player.videoCount == 0 || player.selected >= player.videoCount) {
    return false;
  }

  closeVideo();
  player.file = pocketSystem->mediaCard().open(
      player.videos[player.selected].path, FILE_READ);
  if (!player.file) {
    setError("VIDEO FILE OPEN FAILED");
    player.screens.goTo(PlayerScreen::FILE_ERROR, now);
    return false;
  }

  if (player.file.read((uint8_t *)&player.header, sizeof(player.header)) !=
          sizeof(player.header) ||
      !validHeader(player.header, player.file.size())) {
    closeVideo();
    setError("INVALID PGV FILE");
    player.screens.goTo(PlayerScreen::FILE_ERROR, now);
    return false;
  }

  player.frameIndex = 0;
  player.frameIntervalMs =
      (uint32_t)((100000U + player.header.fpsTimes100 / 2U) /
                 player.header.fpsTimes100);
  if (player.frameIntervalMs == 0) {
    player.frameIntervalMs = 1;
  }
  player.nextFrameAt = now;
  player.overlayUntil = showOverlay ? now + 1400U : 0U;
  player.screens.goTo(PlayerScreen::PLAYING, now);
  pocketSystem->storage().putUInt8("selected", player.selected);
  return true;
}

static uint16_t rgb332To565(uint8_t pixel) {
  const uint8_t red3 = (pixel >> 5) & 0x07;
  const uint8_t green3 = (pixel >> 2) & 0x07;
  const uint8_t blue2 = pixel & 0x03;

  const uint8_t red5 = (uint8_t)((red3 << 2) | (red3 >> 1));
  const uint8_t green6 = (uint8_t)((green3 << 3) | green3);
  const uint8_t blue5 =
      (uint8_t)((blue2 << 3) | (blue2 << 1) | (blue2 >> 1));
  return (uint16_t)((red5 << 11) | (green6 << 5) | blue5);
}

static uint32_t landscapeDestinationIndex(uint32_t sourcePixelIndex) {
  const uint16_t sourceX =
      (uint16_t)(sourcePixelIndex % (uint32_t)player.header.width);
  const uint16_t sourceY =
      (uint16_t)(sourcePixelIndex / (uint32_t)player.header.width);

  // Match Adafruit_GFX rotation 1. The video is upright when the device is
  // turned sideways, while the physical LCD framebuffer remains 172x320.
  const uint16_t destinationX = (uint16_t)(SCREEN_W - 1 - sourceY);
  const uint16_t destinationY = sourceX;
  return (uint32_t)destinationY * (uint32_t)SCREEN_W + destinationX;
}

static bool readRgb332Frame() {
  uint16_t *destination = frame.getBuffer();
  uint32_t remaining = player.header.frameBytes;
  uint32_t pixelIndex = 0;
  const bool landscape = currentVideoIsLandscape();

  while (remaining > 0) {
    const size_t requested = remaining > CONVERSION_CHUNK_BYTES
                                 ? CONVERSION_CHUNK_BYTES
                                 : (size_t)remaining;
    const size_t readCount = player.file.read(player.conversionBuffer, requested);
    if (readCount != requested) {
      return false;
    }

    for (size_t index = 0; index < readCount; ++index) {
      const uint16_t colour = rgb332To565(player.conversionBuffer[index]);
      if (landscape) {
        destination[landscapeDestinationIndex(pixelIndex)] = colour;
      } else {
        destination[pixelIndex] = colour;
      }
      ++pixelIndex;
    }
    remaining -= readCount;
  }
  return true;
}

static bool readRgb565Frame() {
  if (!currentVideoIsLandscape()) {
    return player.file.read((uint8_t *)frame.getBuffer(),
                            player.header.frameBytes) == player.header.frameBytes;
  }

  uint16_t *destination = frame.getBuffer();
  uint32_t remaining = player.header.frameBytes;
  uint32_t pixelIndex = 0;

  while (remaining > 0) {
    size_t requested = remaining > CONVERSION_CHUNK_BYTES
                           ? CONVERSION_CHUNK_BYTES
                           : (size_t)remaining;
    requested &= ~(size_t)1U;
    if (requested == 0) {
      return false;
    }

    const size_t readCount = player.file.read(player.conversionBuffer, requested);
    if (readCount != requested || (readCount & 1U) != 0U) {
      return false;
    }

    for (size_t index = 0; index < readCount; index += 2) {
      const uint16_t colour =
          (uint16_t)player.conversionBuffer[index] |
          ((uint16_t)player.conversionBuffer[index + 1] << 8);
      destination[landscapeDestinationIndex(pixelIndex++)] = colour;
    }
    remaining -= readCount;
  }
  return true;
}

static void drawPlaybackOverlay(uint32_t now) {
  if (now >= player.overlayUntil) {
    return;
  }

  frame.setRotation(currentVideoIsLandscape() ? 1 : 0);
  const int16_t logicalWidth = frame.width();
  const int16_t logicalHeight = frame.height();

  frame.fillRect(0, 0, logicalWidth, 25, C_NAVY);
  frame.drawFastHLine(0, 24, logicalWidth, C_CYAN);
  textLeft(player.videos[player.selected].title, 7, 8, 1, C_TEXT);

  char counter[20];
  snprintf(counter, sizeof(counter), "%u/%u", player.selected + 1,
           player.videoCount);
  textRight(counter, logicalWidth - 7, 8, 1, C_MUTED);

  frame.fillRoundRect(12, logicalHeight - 30, logicalWidth - 24, 22, 5,
                      C_PANEL);
  frame.drawRoundRect(12, logicalHeight - 30, logicalWidth - 24, 22, 5,
                      C_BORDER);
  textLeft("CLICK NEXT", 19, logicalHeight - 22, 1, C_CYAN);
  textRight("HOLD PAUSE", logicalWidth - 19, logicalHeight - 22, 1, C_GOLD);
  frame.setRotation(0);
}

static bool decodeAndPresentFrame(uint32_t now) {
  if (!player.file || player.frameIndex >= player.header.frameCount) {
    return false;
  }

  bool decoded = false;
  if (player.header.pixelFormat == PIXEL_FORMAT_RGB332) {
    decoded = readRgb332Frame();
  } else if (player.header.pixelFormat == PIXEL_FORMAT_RGB565_LE) {
    decoded = readRgb565Frame();
  }

  if (!decoded) {
    return false;
  }

  ++player.frameIndex;
  drawPlaybackOverlay(now);
  pocketSystem->display().present();
  return true;
}

static void advanceVideo(uint32_t now, bool showOverlay = true) {
  if (player.videoCount == 0) {
    scanVideos(now);
    return;
  }
  player.selected = (uint8_t)((player.selected + 1U) % player.videoCount);
  openSelectedVideo(now, showOverlay);
}

static void updatePlayback(uint32_t now) {
  if (!player.file) {
    setError("VIDEO STREAM CLOSED");
    player.screens.goTo(PlayerScreen::FILE_ERROR, now);
    return;
  }

  if ((int32_t)(now - player.nextFrameAt) < 0) {
    delay(1);
    return;
  }

  if (player.frameIndex >= player.header.frameCount) {
    advanceVideo(now, true);
    return;
  }

  if (!decodeAndPresentFrame(now)) {
    advanceVideo(now, true);
    return;
  }

  player.nextFrameAt += player.frameIntervalMs;
  if ((int32_t)(now - player.nextFrameAt) >
      (int32_t)(player.frameIntervalMs * 2U)) {
    player.nextFrameAt = now + player.frameIntervalMs;
  }
}

static void drawVideoIcon(int16_t x, int16_t y, bool large) {
  const int16_t width = large ? 84 : 58;
  const int16_t height = large ? 58 : 40;
  const int16_t radius = large ? 9 : 7;
  frame.fillRoundRect(x - width / 2, y - height / 2, width, height, radius,
                      C_NAVY);
  frame.drawRoundRect(x - width / 2, y - height / 2, width, height, radius,
                      C_CYAN);
  frame.fillTriangle(x - 8, y - 14, x - 8, y + 14, x + 16, y, C_GOLD);

  for (int16_t offset = -width / 2 + 6; offset < width / 2 - 5; offset += 12) {
    frame.fillRect(x + offset, y - height / 2 + 4, 6, 3, C_BORDER);
    frame.fillRect(x + offset, y + height / 2 - 7, 6, 3, C_BORDER);
  }
}

static void drawHeader(const char *subtitle) {
  frame.setRotation(0);
  frame.fillScreen(C_BLACK);
  frame.fillRect(0, 0, SCREEN_W, 55, C_NAVY);
  frame.drawFastHLine(0, 54, SCREEN_W, C_CYAN);
  centered("VIDEO PLAYER", 13, 2, C_TEXT);
  centered(subtitle, 39, 1, C_MUTED);
}

static void drawControlHint(const char *left, const char *right) {
  frame.fillRoundRect(8, 287, 156, 24, 6, C_PANEL);
  frame.drawRoundRect(8, 287, 156, 24, 6, C_BORDER);
  textLeft(left, 15, 295, 1, C_MUTED);
  textRight(right, 157, 295, 1, C_TEXT);
}

static void renderLibrary() {
  drawHeader("SD VIDEO LIBRARY");
  frame.fillRoundRect(11, 69, 150, 184, 11, C_PANEL);
  frame.drawRoundRect(11, 69, 150, 184, 11,
                      player.backSelected ? C_GOLD : C_BORDER);

  if (player.backSelected) {
    frame.drawFastHLine(46, 133, 80, C_GOLD);
    frame.fillTriangle(46, 133, 61, 122, 61, 144, C_GOLD);
    centered("POCKETGAME", 180, 2, C_TEXT);
    centered("RETURN TO MAIN MENU", 213, 1, C_MUTED);
  } else {
    drawVideoIcon(86, 120, true);
    centered(player.videos[player.selected].title, 171, 1, C_GOLD);

    char counter[22];
    snprintf(counter, sizeof(counter), "%u / %u", player.selected + 1,
             player.videoCount);
    centered(counter, 198, 1, C_MUTED);

    centered("CLICK TO CYCLE", 223, 1, C_CYAN);
    centered("HOLD TO PLAY", 239, 1, C_TEXT);
  }

  drawControlHint("CLICK  NEXT", "HOLD  OPEN");
}

static void renderProblem(PlayerScreen screen) {
  const bool noCard = screen == PlayerScreen::NO_CARD;
  const bool fileError = screen == PlayerScreen::FILE_ERROR;
  drawHeader(noCard ? "SD CARD REQUIRED"
                    : (fileError ? "VIDEO ERROR" : "NO VIDEOS"));

  frame.fillRoundRect(14, 82, 144, 157, 11, C_PANEL);
  frame.drawRoundRect(14, 82, 144, 157, 11,
                      fileError ? C_RED : C_ORANGE);

  drawVideoIcon(86, 121, false);
  centered(player.error, 164, 1, fileError ? C_RED : C_GOLD);

  if (noCard) {
    centered("FORMAT CARD AS FAT32", 191, 1, C_TEXT);
    centered("THEN INSERT AND RETRY", 209, 1, C_MUTED);
  } else if (!fileError) {
    centered("RUN THE CONVERTER", 191, 1, C_TEXT);
    centered("COPY OUTPUT TO /VIDEOS", 209, 1, C_MUTED);
  } else {
    centered("RECONVERT THIS VIDEO", 191, 1, C_TEXT);
    centered("OR CLICK TO RESCAN", 209, 1, C_MUTED);
  }

  centered("CLICK RETRY", 260, 1, C_CYAN);
  drawControlHint("CLICK  RETRY", "HOLD  BACK");
}

static void drawPauseOption(const char *label, int16_t y, bool selected) {
  frame.fillRoundRect(29, y, 114, 27, 7, selected ? C_PANEL_2 : C_PANEL);
  frame.drawRoundRect(29, y, 114, 27, 7, selected ? C_GOLD : C_BORDER);
  centered(label, y + 9, 1, selected ? C_TEXT : C_MUTED);
}

static void renderPaused() {
  if (currentVideoIsLandscape()) {
    frame.setRotation(1);
    frame.fillRoundRect(34, 14, 252, 144, 12, C_PANEL);
    frame.drawRoundRect(34, 14, 252, 144, 12, C_GOLD);
    centered("PAUSED", 25, 2, C_TEXT);
    frame.drawFastHLine(54, 52, 212, C_BORDER);

    const char *labels[4] = {"RESUME", "RESTART", "VIDEO LIBRARY",
                             "POCKETGAME"};
    for (uint8_t index = 0; index < 4; ++index) {
      const int16_t x = 50 + (index % 2) * 112;
      const int16_t y = 63 + (index / 2) * 38;
      const bool selected = player.pauseMenu.selected() == index;
      frame.fillRoundRect(x, y, 104, 28, 7,
                          selected ? C_PANEL_2 : C_PANEL);
      frame.drawRoundRect(x, y, 104, 28, 7,
                          selected ? C_GOLD : C_BORDER);
      textLeft(labels[index],
               x + (104 - textWidth(labels[index], 1)) / 2,
               y + 10, 1, selected ? C_TEXT : C_MUTED);
    }
    centered("CLICK NEXT  HOLD SELECT", 143, 1, C_MUTED);
    frame.setRotation(0);
  } else {
    frame.setRotation(0);
    frame.fillRoundRect(17, 50, 138, 232, 12, C_PANEL);
    frame.drawRoundRect(17, 50, 138, 232, 12, C_GOLD);
    centered("PAUSED", 67, 2, C_TEXT);
    frame.drawFastHLine(31, 96, 110, C_BORDER);

    drawPauseOption("RESUME", 108, player.pauseMenu.selected() == 0);
    drawPauseOption("RESTART", 143, player.pauseMenu.selected() == 1);
    drawPauseOption("VIDEO LIBRARY", 178, player.pauseMenu.selected() == 2);
    drawPauseOption("POCKETGAME", 213, player.pauseMenu.selected() == 3);
    centered("CLICK NEXT  HOLD SELECT", 257, 1, C_MUTED);
  }
  pocketSystem->display().present();
}

static void handleLibraryInput(uint32_t now,
                               const pocketgame::ControlEvents &controls) {
  if (controls.cycle) {
    if (player.backSelected) {
      player.backSelected = false;
      player.selected = 0;
    } else if (player.selected + 1U < player.videoCount) {
      ++player.selected;
    } else {
      player.backSelected = true;
    }
    renderLibrary();
    pocketSystem->display().present();
  }

  if (controls.select) {
    if (player.backSelected) {
      closeVideo();
      pocketSystem->mediaCard().unmount();
      pocketSystem->requestLauncher();
    } else {
      openSelectedVideo(now, true);
    }
  }
}

static void handlePlayingInput(uint32_t now,
                               const pocketgame::ControlEvents &controls) {
  if (controls.cycle) {
    advanceVideo(now, true);
    return;
  }

  if (controls.select) {
    player.pauseMenu.reset(4, 0);
    player.screens.goTo(PlayerScreen::PAUSED, now);
    renderPaused();
  }
}

static void handlePausedInput(uint32_t now,
                              const pocketgame::ControlEvents &controls) {
  const MenuAction action = player.pauseMenu.update(controls);
  if (action == MenuAction::CYCLED) {
    renderPaused();
    return;
  }

  if (action != MenuAction::SELECTED) {
    return;
  }

  switch (player.pauseMenu.selected()) {
  case 0:
    player.nextFrameAt = now + player.frameIntervalMs;
    player.overlayUntil = now + 900U;
    player.screens.goTo(PlayerScreen::PLAYING, now);
    break;
  case 1:
    if (player.file && player.file.seek(PGV_HEADER_BYTES)) {
      player.frameIndex = 0;
      player.nextFrameAt = now;
      player.overlayUntil = now + 900U;
      player.screens.goTo(PlayerScreen::PLAYING, now);
    } else {
      openSelectedVideo(now, true);
    }
    break;
  case 2:
    closeVideo();
    player.backSelected = false;
    player.screens.goTo(PlayerScreen::LIBRARY, now);
    renderLibrary();
    pocketSystem->display().present();
    break;
  default:
    closeVideo();
    pocketSystem->mediaCard().unmount();
    pocketSystem->requestLauncher();
    break;
  }
}

static void handleProblemInput(uint32_t now,
                               const pocketgame::ControlEvents &controls) {
  if (controls.cycle) {
    scanVideos(now);
    if (player.screens.is(PlayerScreen::LIBRARY)) {
      renderLibrary();
    } else {
      renderProblem(player.screens.current());
    }
    pocketSystem->display().present();
  }

  if (controls.select) {
    closeVideo();
    pocketSystem->mediaCard().unmount();
    pocketSystem->requestLauncher();
  }
}

static void handleInput(uint32_t now) {
  const pocketgame::ControlEvents &controls = pocketSystem->controls().events();

  switch (player.screens.current()) {
  case PlayerScreen::LIBRARY:
    handleLibraryInput(now, controls);
    break;
  case PlayerScreen::PLAYING:
    handlePlayingInput(now, controls);
    break;
  case PlayerScreen::PAUSED:
    handlePausedInput(now, controls);
    break;
  case PlayerScreen::NO_CARD:
  case PlayerScreen::NO_VIDEOS:
  case PlayerScreen::FILE_ERROR:
    handleProblemInput(now, controls);
    break;
  }
}

static void renderNonPlaybackScreen(uint32_t now) {
  if (now - player.lastUiFrameAt < UI_FRAME_INTERVAL_MS) {
    return;
  }
  player.lastUiFrameAt = now;

  switch (player.screens.current()) {
  case PlayerScreen::LIBRARY:
    renderLibrary();
    break;
  case PlayerScreen::PAUSED:
    renderPaused();
    return;
  case PlayerScreen::NO_CARD:
  case PlayerScreen::NO_VIDEOS:
  case PlayerScreen::FILE_ERROR:
    renderProblem(player.screens.current());
    break;
  case PlayerScreen::PLAYING:
    return;
  }
  pocketSystem->display().present();
}

static void updateLed(uint32_t now) {
  PocketLed &led = pocketSystem->led();
  if (player.screens.is(PlayerScreen::PLAYING)) {
    const uint8_t pulse = PocketLed::breatheValue(now, 1100, 18, 100);
    led.set(0, pulse, pulse / 3);
  } else if (player.screens.is(PlayerScreen::PAUSED)) {
    const uint8_t pulse = PocketLed::breatheValue(now, 1700, 15, 90);
    led.set(pulse, pulse / 2, 0);
  } else if (player.screens.is(PlayerScreen::NO_CARD) ||
             player.screens.is(PlayerScreen::FILE_ERROR)) {
    const bool lit = PocketLed::doubleBlink(now, 1000);
    led.set(lit ? 190 : 16, 0, 0);
  } else {
    const uint8_t pulse = PocketLed::breatheValue(now, 1450, 18, 110);
    led.set(0, pulse / 2, pulse);
  }
}

} // namespace

void VideoPlayerGame::begin(PocketGameSystem &system) {
  pocketSystem = &system;
  closeVideo();
  player.lastUiFrameAt = 0;
  scanVideos(millis());

  if (player.screens.is(PlayerScreen::LIBRARY)) {
    renderLibrary();
  } else {
    renderProblem(player.screens.current());
  }
  pocketSystem->display().present();
  Serial.println("VIDEO PLAYER started.");
}

void VideoPlayerGame::loop(PocketGameSystem &system, uint32_t now) {
  pocketSystem = &system;
  handleInput(now);

  if (system.activeGame() != this) {
    return;
  }

  if (player.screens.is(PlayerScreen::PLAYING)) {
    updatePlayback(now);
  } else {
    renderNonPlaybackScreen(now);
    delay(1);
  }
  updateLed(now);
}
