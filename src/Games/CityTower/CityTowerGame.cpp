#include "CityTowerGame.h"

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <esp_system.h>
#include <math.h>
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
static constexpr uint32_t FRAME_INTERVAL_MS = 33;
static constexpr float MAX_DELTA_SECONDS = 0.08f;
static constexpr uint8_t MAX_FLOORS = 36;
static constexpr uint8_t MAX_PARTICLES = 32;
static constexpr uint8_t MAX_PARACHUTISTS = 10;
static constexpr float FLOOR_HEIGHT = 24.0f;
static constexpr float FLOOR_WIDTH = 64.0f;
static constexpr float GRAVITY = 310.0f;
static constexpr int16_t GROUND_Y = 286;

static constexpr uint16_t C565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((uint16_t)(red & 0xF8) << 8) | ((uint16_t)(green & 0xFC) << 3) |
         ((uint16_t)blue >> 3);
}

static constexpr uint16_t C_BLACK = C565(4, 7, 12);
static constexpr uint16_t C_INK = C565(18, 25, 32);
static constexpr uint16_t C_PANEL = C565(20, 33, 45);
static constexpr uint16_t C_PANEL_2 = C565(31, 50, 66);
static constexpr uint16_t C_BORDER = C565(65, 88, 108);
static constexpr uint16_t C_WHITE = C565(245, 246, 238);
static constexpr uint16_t C_TEXT = C565(232, 239, 239);
static constexpr uint16_t C_MUTED = C565(123, 143, 157);
static constexpr uint16_t C_SKY = C565(133, 205, 238);
static constexpr uint16_t C_SKY_HIGH = C565(76, 156, 216);
static constexpr uint16_t C_DUSK = C565(73, 85, 145);
static constexpr uint16_t C_NIGHT = C565(13, 24, 50);
static constexpr uint16_t C_CLOUD = C565(233, 239, 232);
static constexpr uint16_t C_CLOUD_SHADE = C565(188, 207, 210);
static constexpr uint16_t C_CYAN = C565(55, 190, 220);
static constexpr uint16_t C_BLUE = C565(55, 117, 194);
static constexpr uint16_t C_BLUE_DARK = C565(27, 63, 105);
static constexpr uint16_t C_GOLD = C565(255, 202, 67);
static constexpr uint16_t C_ORANGE = C565(244, 124, 43);
static constexpr uint16_t C_RED = C565(231, 59, 67);
static constexpr uint16_t C_GREEN = C565(62, 204, 135);
static constexpr uint16_t C_BRICK = C565(171, 77, 61);
static constexpr uint16_t C_BRICK_DARK = C565(104, 45, 42);
static constexpr uint16_t C_STONE = C565(159, 165, 158);
static constexpr uint16_t C_STONE_DARK = C565(88, 99, 100);
static constexpr uint16_t C_GLASS = C565(48, 112, 150);
static constexpr uint16_t C_GLASS_DARK = C565(21, 55, 78);
static constexpr uint16_t C_GROUND = C565(75, 82, 74);
static constexpr uint16_t C_ROAD = C565(43, 49, 52);

static float clampFloat(float value, float minimum, float maximum) {
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

static float lerpFloat(float first, float second, float amount) {
  return first + (second - first) * amount;
}

static uint16_t blend565(uint16_t first, uint16_t second, uint8_t amount) {
  const uint8_t firstRed = ((first >> 11) & 31) << 3;
  const uint8_t firstGreen = ((first >> 5) & 63) << 2;
  const uint8_t firstBlue = (first & 31) << 3;
  const uint8_t secondRed = ((second >> 11) & 31) << 3;
  const uint8_t secondGreen = ((second >> 5) & 63) << 2;
  const uint8_t secondBlue = (second & 31) << 3;
  const uint16_t red =
      (firstRed * (255 - amount) + secondRed * amount) >> 8;
  const uint16_t green =
      (firstGreen * (255 - amount) + secondGreen * amount) >> 8;
  const uint16_t blue =
      (firstBlue * (255 - amount) + secondBlue * amount) >> 8;
  return C565((uint8_t)red, (uint8_t)green, (uint8_t)blue);
}

struct RandomGenerator {
  uint32_t state = 0xA341316CUL;

  uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  float range(float minimum, float maximum) {
    const float fraction = (float)(next() & 0xFFFFU) / 65535.0f;
    return minimum + (maximum - minimum) * fraction;
  }

  int rangeInt(int minimum, int maximum) {
    return minimum + (int)(next() % (uint32_t)(maximum - minimum + 1));
  }
};

static RandomGenerator randomGenerator;

enum class GameScreen : uint8_t { MENU, HOW_TO, PLAYING, PAUSED, RESULT };
enum class BlockMode : uint8_t { HANGING, DROPPING, MISSED, WAITING };

enum class LandingQuality : uint8_t { ROUGH, GOOD, PERFECT };

struct Floor {
  float centreX = SCREEN_W * 0.5f;
  float width = FLOOR_WIDTH;
  float bottomWorld = 0.0f;
  uint8_t style = 0;
  LandingQuality quality = LandingQuality::GOOD;
  uint32_t seed = 0;
};

struct ActiveBlock {
  BlockMode mode = BlockMode::HANGING;
  float centreX = SCREEN_W * 0.5f;
  float topY = 95.0f;
  float velocityX = 0.0f;
  float velocityY = 0.0f;
  float swingPhase = 0.0f;
  float swingSpeed = 2.0f;
  float swingAmplitude = 0.45f;
  float ropeLength = 54.0f;
  float anchorX = SCREEN_W * 0.5f;
  float hookX = SCREEN_W * 0.5f;
  float hookY = 80.0f;
  uint8_t style = 0;
  uint32_t seed = 0;
};

struct Particle {
  bool active = false;
  float x = 0.0f;
  float y = 0.0f;
  float velocityX = 0.0f;
  float velocityY = 0.0f;
  float life = 0.0f;
  uint16_t colour = C_WHITE;
};

struct Parachutist {
  bool active = false;
  float x = 0.0f;
  float y = 0.0f;
  float targetX = 0.0f;
  float targetY = 0.0f;
  float phase = 0.0f;
  uint16_t umbrellaColour = C_RED;
};

struct CityState {
  ScreenNavigator<GameScreen> screens{GameScreen::MENU};
  MenuNavigator titleMenu{3, 0};
  MenuNavigator pauseMenu{3, 0};
  MenuNavigator resultMenu{3, 0};

  uint8_t howToPage = 0;
  Floor floors[MAX_FLOORS];
  uint8_t floorCount = 0;
  ActiveBlock active;
  Particle particles[MAX_PARTICLES];
  Parachutist parachutists[MAX_PARACHUTISTS];

  uint8_t lives = 3;
  uint8_t combo = 0;
  uint8_t bestCombo = 0;
  uint32_t score = 0;
  uint32_t population = 0;
  uint32_t bestScore = 0;
  uint8_t bestFloors = 0;
  uint32_t lifetimePopulation = 0;

  float towerTopWorld = 0.0f;
  float cameraWorld = 0.0f;
  float cameraTarget = 0.0f;
  float towerLean = 0.0f;
  float feedbackLife = 0.0f;
  char feedback[20] = {};
  uint16_t feedbackColour = C_TEXT;
  uint32_t nextBlockAt = 0;
  uint32_t lastFrameAt = 0;
  uint32_t perfectFlashUntil = 0;
  bool completed = false;
  bool newBest = false;
};

static CityState city;

static int16_t textWidth(const char *text, uint8_t size) {
  return (int16_t)(strlen(text) * 6 * size);
}

static void textLeft(const char *text, int16_t x, int16_t y, uint8_t size,
                     uint16_t colour) {
  frame.setTextWrap(false);
  frame.setTextSize(size);
  frame.setTextColor(colour);
  frame.setCursor(x, y);
  frame.print(text);
}

static void textRight(const char *text, int16_t rightX, int16_t y, uint8_t size,
                      uint16_t colour) {
  textLeft(text, rightX - textWidth(text, size), y, size, colour);
}

static void centered(const char *text, int16_t y, uint8_t size,
                     uint16_t colour) {
  textLeft(text, (SCREEN_W - textWidth(text, size)) / 2, y, size, colour);
}

static float towerTopScreenY() {
  return GROUND_Y + city.cameraWorld - city.towerTopWorld;
}

static float floorTopScreenY(const Floor &floor) {
  return GROUND_Y + city.cameraWorld - floor.bottomWorld - FLOOR_HEIGHT;
}

static float currentAnchorX(uint32_t now) {
  return SCREEN_W * 0.5f + sinf(now * 0.00072f) * 15.0f;
}

static void setFeedback(const char *text, uint16_t colour, float seconds) {
  strncpy(city.feedback, text, sizeof(city.feedback) - 1);
  city.feedback[sizeof(city.feedback) - 1] = '\0';
  city.feedbackColour = colour;
  city.feedbackLife = seconds;
}

static Particle *allocateParticle() {
  for (uint8_t index = 0; index < MAX_PARTICLES; ++index) {
    if (!city.particles[index].active) {
      city.particles[index].active = true;
      return &city.particles[index];
    }
  }
  const uint8_t replacement = randomGenerator.next() % MAX_PARTICLES;
  city.particles[replacement].active = true;
  return &city.particles[replacement];
}

static void spawnParticle(float x, float y, float vx, float vy, float life,
                          uint16_t colour) {
  Particle *particle = allocateParticle();
  particle->x = x;
  particle->y = y;
  particle->velocityX = vx;
  particle->velocityY = vy;
  particle->life = life;
  particle->colour = colour;
}

static void spawnLandingBurst(float x, float y, bool perfect) {
  const uint8_t count = perfect ? 20 : 8;
  for (uint8_t index = 0; index < count; ++index) {
    const float angle = randomGenerator.range(3.45f, 5.98f);
    const float speed = randomGenerator.range(perfect ? 35.0f : 20.0f,
                                              perfect ? 105.0f : 62.0f);
    const uint16_t colour =
        perfect ? (index & 1 ? C_GOLD : C_GREEN) : C_STONE;
    spawnParticle(x, y, cosf(angle) * speed, sinf(angle) * speed,
                  randomGenerator.range(0.35f, 0.8f), colour);
  }
}

static Parachutist *allocateParachutist() {
  for (uint8_t index = 0; index < MAX_PARACHUTISTS; ++index) {
    if (!city.parachutists[index].active) {
      city.parachutists[index].active = true;
      return &city.parachutists[index];
    }
  }
  const uint8_t replacement = randomGenerator.next() % MAX_PARACHUTISTS;
  city.parachutists[replacement].active = true;
  return &city.parachutists[replacement];
}

static void spawnResidents(uint8_t count, float targetX, float targetY) {
  if (count > 4) {
    count = 4;
  }
  for (uint8_t index = 0; index < count; ++index) {
    Parachutist *person = allocateParachutist();
    const bool fromLeft = (randomGenerator.next() & 1U) == 0;
    person->x = fromLeft ? -8.0f : SCREEN_W + 8.0f;
    person->y = randomGenerator.range(105.0f, 180.0f);
    person->targetX = targetX + randomGenerator.range(-21.0f, 21.0f);
    person->targetY = targetY + randomGenerator.range(4.0f, 18.0f);
    person->phase = randomGenerator.range(0.0f, 6.28f);
    person->umbrellaColour =
        index & 1 ? C_RED : (index == 2 ? C_GOLD : C_CYAN);
  }
}

static void clearEffects() {
  for (uint8_t index = 0; index < MAX_PARTICLES; ++index) {
    city.particles[index].active = false;
  }
  for (uint8_t index = 0; index < MAX_PARACHUTISTS; ++index) {
    city.parachutists[index].active = false;
  }
}

static void updateEffects(float deltaSeconds) {
  for (uint8_t index = 0; index < MAX_PARTICLES; ++index) {
    Particle &particle = city.particles[index];
    if (!particle.active) {
      continue;
    }
    particle.life -= deltaSeconds;
    if (particle.life <= 0.0f) {
      particle.active = false;
      continue;
    }
    particle.velocityY += 95.0f * deltaSeconds;
    particle.x += particle.velocityX * deltaSeconds;
    particle.y += particle.velocityY * deltaSeconds;
  }

  for (uint8_t index = 0; index < MAX_PARACHUTISTS; ++index) {
    Parachutist &person = city.parachutists[index];
    if (!person.active) {
      continue;
    }
    person.phase += deltaSeconds * 5.0f;
    const float follow = 1.0f - expf(-1.65f * deltaSeconds);
    person.x = lerpFloat(person.x, person.targetX, follow);
    person.y = lerpFloat(person.y, person.targetY, follow * 0.78f);
    person.x += sinf(person.phase) * 7.0f * deltaSeconds;
    if (fabsf(person.x - person.targetX) < 2.2f &&
        fabsf(person.y - person.targetY) < 3.2f) {
      person.active = false;
    }
  }
}

static void loadProgress() {
  pocketgame::PocketStorage &storage = pocketSystem->storage();
  city.bestScore = storage.getUInt32("bestScore", 0);
  city.bestFloors = storage.getUInt8("bestFloors", 0);
  city.lifetimePopulation = storage.getUInt32("population", 0);
}

static void saveProgress() {
  pocketgame::PocketStorage &storage = pocketSystem->storage();
  city.newBest = city.score > city.bestScore;
  if (city.newBest) {
    city.bestScore = city.score;
    storage.putUInt32("bestScore", city.bestScore);
  }
  if (city.floorCount > city.bestFloors) {
    city.bestFloors = city.floorCount;
    storage.putUInt8("bestFloors", city.bestFloors);
  }
  city.lifetimePopulation += city.population;
  storage.putUInt32("population", city.lifetimePopulation);
}

static void updateHangingPosition(uint32_t now) {
  ActiveBlock &block = city.active;
  block.anchorX = currentAnchorX(now);
  const float angle = sinf(block.swingPhase) * block.swingAmplitude;
  block.hookX = block.anchorX + sinf(angle) * block.ropeLength;
  block.hookY = 42.0f + cosf(angle) * block.ropeLength;
  block.centreX = block.hookX;
  block.topY = block.hookY + 8.0f;
}

static void spawnBlock(uint32_t now) {
  ActiveBlock &block = city.active;
  block.mode = BlockMode::HANGING;
  block.swingPhase = randomGenerator.range(-1.1f, 1.1f);
  block.swingSpeed = 1.75f + city.floorCount * 0.047f;
  block.swingAmplitude =
      clampFloat(0.43f + city.floorCount * 0.009f, 0.43f, 0.72f);
  block.ropeLength =
      clampFloat(61.0f - city.floorCount * 0.45f, 44.0f, 61.0f);
  block.style = (city.floorCount / 6 + city.floorCount) % 4;
  block.seed = randomGenerator.next();
  block.velocityX = 0.0f;
  block.velocityY = 0.0f;
  updateHangingPosition(now);
}

static void releaseBlock() {
  if (city.active.mode != BlockMode::HANGING) {
    return;
  }

  ActiveBlock &block = city.active;
  const float phaseVelocity = block.swingSpeed;
  const float angle = sinf(block.swingPhase) * block.swingAmplitude;
  const float angleVelocity =
      cosf(block.swingPhase) * block.swingAmplitude * phaseVelocity;
  block.velocityX = cosf(angle) * block.ropeLength * angleVelocity;
  block.velocityY = -sinf(angle) * block.ropeLength * angleVelocity + 8.0f;
  block.mode = BlockMode::DROPPING;
  setFeedback("DROP!", C_GOLD, 0.35f);
}

static void resetRun(uint32_t now) {
  city.floorCount = 0;
  city.lives = 3;
  city.combo = 0;
  city.bestCombo = 0;
  city.score = 0;
  city.population = 0;
  city.towerTopWorld = 0.0f;
  city.cameraWorld = 0.0f;
  city.cameraTarget = 0.0f;
  city.towerLean = 0.0f;
  city.feedbackLife = 0.0f;
  city.feedback[0] = '\0';
  city.completed = false;
  city.newBest = false;
  city.perfectFlashUntil = 0;
  clearEffects();
  spawnBlock(now);
  city.screens.goTo(GameScreen::PLAYING, now);
  city.lastFrameAt = now;
}

static void finishRun(uint32_t now, bool completed) {
  city.completed = completed;
  saveProgress();
  city.resultMenu.reset(3, 0);
  city.screens.goTo(GameScreen::RESULT, now);
  pocketSystem->led().off();
}

static void missBlock(uint32_t now) {
  city.combo = 0;
  city.active.mode = BlockMode::MISSED;
  city.active.velocityY = city.active.velocityY < 45.0f
                              ? 45.0f
                              : city.active.velocityY;
  setFeedback("MISSED!", C_RED, 1.0f);
  for (uint8_t index = 0; index < 8; ++index) {
    spawnParticle(city.active.centreX,
                  city.active.topY + FLOOR_HEIGHT * 0.5f,
                  randomGenerator.range(-60.0f, 60.0f),
                  randomGenerator.range(-35.0f, 20.0f), 0.7f, C_RED);
  }
  (void)now;
}

static void landBlock(uint32_t now) {
  const float supportCentre = city.floorCount == 0
                                  ? SCREEN_W * 0.5f
                                  : city.floors[city.floorCount - 1].centreX;
  const float supportWidth =
      city.floorCount == 0 ? 82.0f : city.floors[city.floorCount - 1].width;
  const float blockLeft = city.active.centreX - FLOOR_WIDTH * 0.5f;
  const float blockRight = city.active.centreX + FLOOR_WIDTH * 0.5f;
  const float supportLeft = supportCentre - supportWidth * 0.5f;
  const float supportRight = supportCentre + supportWidth * 0.5f;
  const float overlap =
      (blockRight < supportRight ? blockRight : supportRight) -
      (blockLeft > supportLeft ? blockLeft : supportLeft);

  if (overlap < 9.0f) {
    missBlock(now);
    return;
  }

  if (city.floorCount >= MAX_FLOORS) {
    finishRun(now, true);
    return;
  }

  const float centreError = fabsf(city.active.centreX - supportCentre);
  const float overlapRatio = clampFloat(overlap / FLOOR_WIDTH, 0.0f, 1.0f);
  LandingQuality quality = LandingQuality::ROUGH;

  if (centreError <= 2.6f) {
    quality = LandingQuality::PERFECT;
    city.combo = city.combo < 12 ? city.combo + 1 : 12;
    if (city.combo > city.bestCombo) {
      city.bestCombo = city.combo;
    }
    setFeedback(city.combo >= 3 ? "PERFECT COMBO!" : "PERFECT!", C_GREEN,
                1.15f);
    city.perfectFlashUntil = now + 170;
  } else if (overlapRatio >= 0.67f) {
    quality = LandingQuality::GOOD;
    if (city.combo > 0) {
      --city.combo;
    }
    setFeedback("GOOD DROP", C_CYAN, 0.9f);
  } else {
    city.combo = 0;
    setFeedback("WOBBLY", C_ORANGE, 0.95f);
  }

  Floor &floor = city.floors[city.floorCount];
  floor.centreX = city.active.centreX;
  floor.width = FLOOR_WIDTH;
  floor.bottomWorld = city.towerTopWorld;
  floor.style = city.active.style;
  floor.quality = quality;
  floor.seed = city.active.seed;
  ++city.floorCount;
  city.towerTopWorld += FLOOR_HEIGHT;

  const uint32_t base = 90U + (uint32_t)(overlapRatio * 170.0f);
  const uint32_t comboBonus =
      quality == LandingQuality::PERFECT ? city.combo * 85U : city.combo * 20U;
  city.score += base + comboBonus;

  const uint32_t residents =
      5U + (uint32_t)(overlapRatio * 7.0f) +
      (quality == LandingQuality::PERFECT ? city.combo * 2U : 0U);
  city.population += residents;

  const float landingY = towerTopScreenY();
  spawnLandingBurst(city.active.centreX, landingY,
                    quality == LandingQuality::PERFECT);
  if (quality != LandingQuality::ROUGH) {
    spawnResidents(quality == LandingQuality::PERFECT ? 4 : 2,
                   city.active.centreX, landingY + 4.0f);
  }

  const float offset = city.active.centreX - supportCentre;
  city.towerLean = clampFloat(city.towerLean + offset * 0.075f, -9.0f, 9.0f);
  city.cameraTarget =
      city.towerTopWorld > 118.0f ? city.towerTopWorld - 118.0f : 0.0f;
  city.active.mode = BlockMode::WAITING;
  city.nextBlockAt = now + (quality == LandingQuality::PERFECT ? 570U : 430U);

  if (city.floorCount >= MAX_FLOORS) {
    finishRun(now + 1, true);
  }
}

static void updatePlaying(float deltaSeconds, uint32_t now) {
  const float cameraFollow = 1.0f - expf(-5.2f * deltaSeconds);
  city.cameraWorld =
      lerpFloat(city.cameraWorld, city.cameraTarget, cameraFollow);

  ActiveBlock &block = city.active;
  if (block.mode == BlockMode::HANGING) {
    block.swingPhase += block.swingSpeed * deltaSeconds;
    updateHangingPosition(now);
  } else if (block.mode == BlockMode::DROPPING ||
             block.mode == BlockMode::MISSED) {
    block.velocityY += GRAVITY * deltaSeconds;
    block.centreX += block.velocityX * deltaSeconds;
    block.topY += block.velocityY * deltaSeconds;
    block.velocityX *= 0.998f;

    if (block.mode == BlockMode::DROPPING &&
        block.topY + FLOOR_HEIGHT >= towerTopScreenY() &&
        block.velocityY > 0.0f) {
      block.topY = towerTopScreenY() - FLOOR_HEIGHT;
      landBlock(now);
    }

    if (block.mode == BlockMode::MISSED && block.topY > SCREEN_H + 22) {
      block.mode = BlockMode::WAITING;
      if (city.lives > 0) {
        --city.lives;
      }
      if (city.lives == 0) {
        finishRun(now, false);
      } else {
        city.nextBlockAt = now + 480;
      }
    }
  } else if (block.mode == BlockMode::WAITING && now >= city.nextBlockAt &&
             city.screens.is(GameScreen::PLAYING)) {
    spawnBlock(now);
  }

  city.feedbackLife =
      city.feedbackLife > deltaSeconds ? city.feedbackLife - deltaSeconds : 0.0f;
  updateEffects(deltaSeconds);
}

static void handleInput(uint32_t now) {
  const pocketgame::ControlEvents &controls = pocketSystem->controls().events();

  if (city.screens.is(GameScreen::MENU)) {
    const MenuAction action = city.titleMenu.update(controls);
    if (action == MenuAction::SELECTED) {
      if (city.titleMenu.selected() == 0) {
        resetRun(now);
      } else if (city.titleMenu.selected() == 1) {
        city.howToPage = 0;
        city.screens.goTo(GameScreen::HOW_TO, now);
      } else {
        pocketSystem->requestLauncher();
      }
    }
    return;
  }

  if (city.screens.is(GameScreen::HOW_TO)) {
    if (controls.cycle) {
      city.howToPage = (city.howToPage + 1) % 2;
      city.screens.refresh(now);
    }
    if (controls.select) {
      city.screens.goTo(GameScreen::MENU, now);
    }
    return;
  }

  if (city.screens.is(GameScreen::PLAYING)) {
    if (controls.select) {
      city.pauseMenu.reset(3, 0);
      city.screens.goTo(GameScreen::PAUSED, now);
      return;
    }
    if (controls.cycle && city.active.mode == BlockMode::HANGING) {
      releaseBlock();
    }
    return;
  }

  if (city.screens.is(GameScreen::PAUSED)) {
    const MenuAction action = city.pauseMenu.update(controls);
    if (action == MenuAction::SELECTED) {
      if (city.pauseMenu.selected() == 0) {
        city.screens.goTo(GameScreen::PLAYING, now);
        city.lastFrameAt = now;
      } else if (city.pauseMenu.selected() == 1) {
        resetRun(now);
      } else {
        pocketSystem->requestLauncher();
      }
    }
    return;
  }

  if (city.screens.is(GameScreen::RESULT)) {
    const MenuAction action = city.resultMenu.update(controls);
    if (action == MenuAction::SELECTED) {
      if (city.resultMenu.selected() == 0) {
        resetRun(now);
      } else if (city.resultMenu.selected() == 1) {
        city.titleMenu.reset(3, 0);
        city.screens.goTo(GameScreen::MENU, now);
      } else {
        pocketSystem->requestLauncher();
      }
    }
  }
}

static void drawSky(uint32_t now, bool gameScene) {
  const float nightAmount = gameScene
                                ? clampFloat((city.floorCount - 12.0f) / 18.0f,
                                             0.0f, 1.0f)
                                : 0.0f;
  for (int16_t y = 0; y < SCREEN_H; y += 12) {
    const float vertical = y / (float)SCREEN_H;
    uint16_t day = blend565(C_SKY_HIGH, C_SKY, (uint8_t)(vertical * 220.0f));
    uint16_t night = blend565(C_NIGHT, C_DUSK,
                              (uint8_t)(vertical * 150.0f));
    frame.fillRect(0, y, SCREEN_W, 12,
                   blend565(day, night, (uint8_t)(nightAmount * 255.0f)));
  }

  if (nightAmount < 0.55f) {
    frame.fillCircle(140, 74, 14, C_GOLD);
    frame.fillCircle(136, 70, 9, C565(255, 227, 123));
  } else {
    frame.fillCircle(139, 68, 11, C_WHITE);
    frame.fillCircle(134, 64, 10, C_NIGHT);
    for (uint8_t index = 0; index < 18; ++index) {
      const int16_t x = (index * 43 + 11) % SCREEN_W;
      const int16_t y = (index * 61 + 19) % 185;
      if (((now / 280U + index) & 3U) != 0) {
        frame.drawPixel(x, y, C_WHITE);
      }
    }
  }

  const int16_t drift = (int16_t)((now / 90U) % 230U) - 30;
  for (uint8_t index = 0; index < 4; ++index) {
    int16_t x = (drift + index * 68) % 230 - 30;
    int16_t y = 88 + index * 27;
    frame.fillCircle(x, y, 8, C_CLOUD_SHADE);
    frame.fillCircle(x + 8, y - 3, 10, C_CLOUD);
    frame.fillCircle(x + 19, y + 1, 7, C_CLOUD);
    frame.fillRect(x, y, 21, 7, C_CLOUD);
  }
}

static void drawSkylineLayer(int16_t baseY, uint8_t layer, uint16_t colour,
                             uint16_t windowColour, uint32_t now) {
  int16_t x = -(layer * 7);
  uint8_t index = 0;
  while (x < SCREEN_W) {
    const uint8_t width = 13 + ((index * 11 + layer * 7) % 19);
    const uint8_t height = 28 + ((index * 29 + layer * 17) % 77);
    const int16_t y = baseY - height;
    frame.fillRect(x, y, width, height, colour);
    if (((index + layer) % 3) == 0) {
      frame.fillRect(x + width / 2 - 1, y - 8, 3, 8, colour);
    }
    for (int16_t wy = y + 7; wy < baseY - 5; wy += 12) {
      for (int16_t wx = x + 4; wx < x + width - 3; wx += 8) {
        if (((wx + wy + index + now / 1200U) % 5U) == 0) {
          frame.fillRect(wx, wy, 2, 3, windowColour);
        }
      }
    }
    x += width + 4;
    ++index;
  }
}

static void drawCityBackground(uint32_t now, bool gameScene) {
  drawSky(now, gameScene);
  const int16_t cameraShift = gameScene ? (int16_t)(city.cameraWorld * 0.10f) : 0;
  drawSkylineLayer(269 + cameraShift, 0, C565(83, 122, 143),
                   C565(178, 208, 203), now);
  drawSkylineLayer(287 + cameraShift, 1, C565(53, 83, 104), C_GOLD, now);
}

static void drawGround() {
  const int16_t ground = (int16_t)roundf(GROUND_Y + city.cameraWorld);
  if (ground >= SCREEN_H) {
    return;
  }
  frame.fillRect(0, ground, SCREEN_W, SCREEN_H - ground, C_GROUND);
  frame.fillRect(0, ground + 8, SCREEN_W, SCREEN_H - ground - 8, C_ROAD);
  frame.drawFastHLine(0, ground + 7, SCREEN_W, C_STONE);
  for (int16_t x = 3; x < SCREEN_W; x += 25) {
    frame.fillRect(x, ground + 21, 13, 2, C_GOLD);
  }
  frame.fillRect(8, ground - 7, 156, 7, C_STONE_DARK);
  frame.drawFastHLine(8, ground - 7, 156, C_WHITE);
}

static void floorColours(uint8_t style, uint16_t &body, uint16_t &dark,
                         uint16_t &trim) {
  switch (style % 4) {
  case 0:
    body = C_BRICK;
    dark = C_BRICK_DARK;
    trim = C_STONE;
    break;
  case 1:
    body = C_BLUE;
    dark = C_BLUE_DARK;
    trim = C_CYAN;
    break;
  case 2:
    body = C_STONE;
    dark = C_STONE_DARK;
    trim = C_BRICK;
    break;
  default:
    body = C_GLASS;
    dark = C_GLASS_DARK;
    trim = C_GOLD;
    break;
  }
}

static void drawFloorBlock(float centreX, float topY, float width,
                           uint8_t style, uint32_t seed, bool hanging,
                           LandingQuality quality) {
  const int16_t x = (int16_t)roundf(centreX - width * 0.5f);
  const int16_t y = (int16_t)roundf(topY);
  const int16_t w = (int16_t)roundf(width);
  const int16_t h = (int16_t)FLOOR_HEIGHT;
  if (y > SCREEN_H || y + h < 0 || x > SCREEN_W || x + w < 0) {
    return;
  }

  uint16_t body;
  uint16_t dark;
  uint16_t trim;
  floorColours(style, body, dark, trim);

  frame.fillRect(x + 3, y + 4, w, h, C_INK);
  frame.fillRect(x, y, w, h, dark);
  frame.fillRect(x + 3, y + 2, w - 7, h - 4, body);
  frame.fillRect(x + 1, y, w - 2, 3, trim);
  frame.drawRect(x, y, w, h, C_INK);
  frame.drawFastVLine(x + w - 4, y + 3, h - 6,
                      blend565(dark, C_BLACK, 80));

  const uint8_t windowColumns = 4;
  for (uint8_t column = 0; column < windowColumns; ++column) {
    const int16_t wx = x + 6 + column * ((w - 12) / windowColumns);
    const bool lit = ((seed >> (column * 3)) & 3U) != 0;
    frame.fillRect(wx, y + 7, 7, 8, C_INK);
    frame.fillRect(wx + 1, y + 8, 5, 6,
                   lit ? C_GOLD : blend565(C_GLASS_DARK, C_SKY, 80));
    frame.drawFastVLine(wx + 3, y + 8, 6, dark);
  }

  frame.drawFastHLine(x + 3, y + 18, w - 7, trim);
  frame.fillRect(x + w / 2 - 4, y + 17, 8, 6, C_INK);
  frame.drawPixel(x + w / 2 + 2, y + 20, C_GOLD);

  if (quality == LandingQuality::ROUGH && !hanging) {
    frame.drawFastHLine(x + 7, y + 3, 9, C_ORANGE);
  } else if (quality == LandingQuality::PERFECT && !hanging) {
    frame.drawPixel(x + 2, y + 1, C_WHITE);
    frame.drawPixel(x + w - 3, y + 1, C_WHITE);
  }

  if (hanging) {
    frame.fillRect(x + 8, y - 3, w - 16, 3, C_GOLD);
    frame.drawFastVLine(x + 8, y - 7, 5, C_INK);
    frame.drawFastVLine(x + w - 9, y - 7, 5, C_INK);
  }
}

static float floorSwayOffset(uint8_t index, uint32_t now) {
  if (city.floorCount < 2) {
    return 0.0f;
  }
  const float heightAmount = (index + 1.0f) / city.floorCount;
  const float wind = sinf(now * 0.0021f + index * 0.21f) *
                     heightAmount * (0.45f + city.floorCount * 0.045f);
  const float lean = city.towerLean * heightAmount * heightAmount;
  return wind + lean;
}

static void drawTower(uint32_t now) {
  drawGround();
  for (uint8_t index = 0; index < city.floorCount; ++index) {
    const Floor &floor = city.floors[index];
    drawFloorBlock(floor.centreX + floorSwayOffset(index, now),
                   floorTopScreenY(floor), floor.width, floor.style, floor.seed,
                   false, floor.quality);
  }

  if (city.floorCount == 0) {
    const int16_t baseY = (int16_t)roundf(GROUND_Y + city.cameraWorld - 10);
    frame.fillRect(43, baseY, 86, 10, C_STONE_DARK);
    frame.fillRect(47, baseY - 4, 78, 5, C_STONE);
    frame.drawFastHLine(47, baseY - 4, 78, C_WHITE);
  }
}

static void drawCrane(uint32_t now) {
  const int16_t anchorX = (int16_t)roundf(currentAnchorX(now));
  frame.fillRect(9, 36, 154, 5, C_INK);
  frame.fillRect(12, 32, 147, 5, C_GOLD);
  for (int16_t x = 14; x < 154; x += 16) {
    frame.drawLine(x, 32, x + 8, 23, C_ORANGE);
    frame.drawLine(x + 8, 23, x + 16, 32, C_ORANGE);
  }
  frame.fillRect(7, 27, 7, 19, C_STONE_DARK);
  frame.fillRect(156, 28, 7, 17, C_STONE_DARK);
  frame.fillCircle(anchorX, 37, 5, C_INK);
  frame.fillCircle(anchorX, 37, 3, C_CYAN);

  if (city.active.mode == BlockMode::HANGING) {
    frame.drawLine(anchorX, 40, (int16_t)city.active.hookX,
                   (int16_t)city.active.hookY, C_INK);
    frame.drawLine((int16_t)city.active.hookX,
                   (int16_t)city.active.hookY,
                   (int16_t)(city.active.centreX - FLOOR_WIDTH * 0.5f + 8),
                   (int16_t)city.active.topY, C_INK);
    frame.drawLine((int16_t)city.active.hookX,
                   (int16_t)city.active.hookY,
                   (int16_t)(city.active.centreX + FLOOR_WIDTH * 0.5f - 8),
                   (int16_t)city.active.topY, C_INK);
    frame.fillCircle((int16_t)city.active.hookX,
                     (int16_t)city.active.hookY, 3, C_GOLD);
  }
}

static void drawActiveBlock() {
  if (city.active.mode == BlockMode::WAITING) {
    return;
  }
  drawFloorBlock(city.active.centreX, city.active.topY, FLOOR_WIDTH,
                 city.active.style, city.active.seed,
                 city.active.mode == BlockMode::HANGING,
                 LandingQuality::GOOD);
}

static void drawEffects() {
  for (uint8_t index = 0; index < MAX_PARTICLES; ++index) {
    const Particle &particle = city.particles[index];
    if (!particle.active) {
      continue;
    }
    const int16_t x = (int16_t)roundf(particle.x);
    const int16_t y = (int16_t)roundf(particle.y);
    frame.fillRect(x - 1, y - 1, 3, 3, particle.colour);
  }

  for (uint8_t index = 0; index < MAX_PARACHUTISTS; ++index) {
    const Parachutist &person = city.parachutists[index];
    if (!person.active) {
      continue;
    }
    const int16_t x = (int16_t)roundf(person.x);
    const int16_t y = (int16_t)roundf(person.y);
    frame.fillCircle(x, y, 5, person.umbrellaColour);
    frame.fillRect(x - 5, y, 11, 4, C_SKY);
    frame.drawLine(x - 4, y + 1, x, y + 7, C_INK);
    frame.drawLine(x + 4, y + 1, x, y + 7, C_INK);
    frame.fillCircle(x, y + 8, 2, C565(224, 173, 122));
    frame.drawFastVLine(x, y + 10, 5, C_RED);
    frame.drawLine(x, y + 14, x - 2, y + 18, C_INK);
    frame.drawLine(x, y + 14, x + 2, y + 18, C_INK);
  }
}

static void drawHud() {
  frame.fillRoundRect(4, 4, 164, 24, 5, C_PANEL);
  frame.drawRoundRect(4, 4, 164, 24, 5, C_BORDER);

  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)city.score);
  textLeft("SCORE", 9, 8, 1, C_MUTED);
  textLeft(buffer, 9, 17, 1, C_GOLD);

  snprintf(buffer, sizeof(buffer), "%uF", city.floorCount);
  centered(buffer, 9, 1, C_TEXT);
  snprintf(buffer, sizeof(buffer), "%luP", (unsigned long)city.population);
  centered(buffer, 18, 1, C_CYAN);

  textRight("LIFE", 163, 8, 1, C_MUTED);
  for (uint8_t life = 0; life < 3; ++life) {
    const int16_t x = 142 + life * 7;
    frame.fillRect(x, 18, 5, 5, life < city.lives ? C_RED : C_BORDER);
  }

  if (city.combo >= 2) {
    snprintf(buffer, sizeof(buffer), "COMBO X%u", city.combo);
    frame.fillRoundRect(48, 31, 76, 16, 6, C_PANEL);
    frame.drawRoundRect(48, 31, 76, 16, 6, C_GOLD);
    centered(buffer, 36, 1, C_GOLD);
  }
}

static void drawGameplay(uint32_t now) {
  drawCityBackground(now, true);
  drawTower(now);
  drawCrane(now);
  drawActiveBlock();
  drawEffects();
  drawHud();

  if (city.feedbackLife > 0.0f) {
    const int16_t y = city.combo >= 2 ? 52 : 36;
    frame.fillRoundRect(29, y, 114, 21, 6, C_PANEL);
    frame.drawRoundRect(29, y, 114, 21, 6, city.feedbackColour);
    centered(city.feedback, y + 7, 1, city.feedbackColour);
  }

  if (city.floorCount == 0 && city.active.mode == BlockMode::HANGING) {
    centered("CLICK TO DROP", 252, 1, C_TEXT);
    centered("HOLD TO PAUSE", 268, 1, C_MUTED);
  }

  if (now < city.perfectFlashUntil) {
    frame.drawRect(0, 0, SCREEN_W, SCREEN_H, C_GOLD);
    frame.drawRect(1, 1, SCREEN_W - 2, SCREEN_H - 2, C_GREEN);
  }
}

static void drawMenuOption(const char *label, int16_t y, bool selected) {
  frame.fillRoundRect(26, y, 120, 27, 7, selected ? C_PANEL_2 : C_PANEL);
  frame.drawRoundRect(26, y, 120, 27, 7, selected ? C_GOLD : C_BORDER);
  centered(label, y + 9, 1, selected ? C_WHITE : C_MUTED);
}

static void drawMiniTower(uint32_t now) {
  const int16_t baseY = 168;
  for (uint8_t index = 0; index < 4; ++index) {
    const float sway = sinf(now * 0.002f + index * 0.35f) * index * 0.8f;
    drawFloorBlock(86 + sway, baseY - (index + 1) * 22, 58, index,
                   0x7412U + index * 331U, false,
                   index == 3 ? LandingQuality::PERFECT
                              : LandingQuality::GOOD);
  }
  const int16_t hookX = 86 + (int16_t)(sinf(now * 0.003f) * 28.0f);
  frame.drawFastHLine(24, 51, 124, C_GOLD);
  frame.drawLine(86, 52, hookX, 82, C_INK);
  drawFloorBlock(hookX, 87, 58, 0, 0x9911U, true, LandingQuality::GOOD);
}

static void drawTitleMenu(uint32_t now) {
  drawCityBackground(now, false);
  frame.fillRoundRect(8, 12, 156, 48, 10, C_PANEL);
  frame.drawRoundRect(8, 12, 156, 48, 10, C_CYAN);
  centered("CITY", 20, 2, C_WHITE);
  centered("TOWER", 41, 2, C_GOLD);
  drawMiniTower(now);

  frame.fillRoundRect(11, 180, 150, 129, 11, C_PANEL);
  frame.drawRoundRect(11, 180, 150, 129, 11, C_BORDER);
  drawMenuOption("BUILD CITY", 188, city.titleMenu.selected() == 0);
  drawMenuOption("HOW TO PLAY", 222, city.titleMenu.selected() == 1);
  drawMenuOption("POCKETGAME", 256, city.titleMenu.selected() == 2);

  char best[40];
  snprintf(best, sizeof(best), "BEST %lu  /  %u FLOORS",
           (unsigned long)city.bestScore, city.bestFloors);
  centered(best, 292, 1, C_MUTED);
}

static void drawHowTo(uint32_t now) {
  drawCityBackground(now, false);
  frame.fillRoundRect(9, 12, 154, 296, 12, C_PANEL);
  frame.drawRoundRect(9, 12, 154, 296, 12, C_BORDER);
  centered("HOW TO BUILD", 24, 1, C_GOLD);

  if (city.howToPage == 0) {
    frame.drawFastHLine(31, 65, 110, C_GOLD);
    const int16_t hookX = 86 + (int16_t)(sinf(now * 0.003f) * 27.0f);
    frame.drawLine(86, 66, hookX, 101, C_INK);
    drawFloorBlock(hookX, 106, 58, 1, 0x1234U, true,
                   LandingQuality::GOOD);
    drawFloorBlock(86, 183, 58, 0, 0x5678U, false,
                   LandingQuality::GOOD);
    frame.drawLine(hookX, 137, 86, 178, C_CYAN);
    centered("1. WATCH THE SWING", 222, 1, C_TEXT);
    centered("2. CLICK TO DROP", 240, 1, C_TEXT);
    centered("ALIGN THE FLOOR EDGES", 263, 1, C_MUTED);
  } else {
    drawFloorBlock(86, 70, 58, 3, 0x1111U, false,
                   LandingQuality::PERFECT);
    drawFloorBlock(86, 94, 58, 2, 0x2222U, false,
                   LandingQuality::PERFECT);
    drawFloorBlock(86, 118, 58, 1, 0x3333U, false,
                   LandingQuality::PERFECT);
    centered("PERFECT DROPS", 161, 1, C_GREEN);
    centered("BUILD COMBOS + RESIDENTS", 181, 1, C_TEXT);
    centered("MISSES COST A LIFE", 207, 1, C_RED);
    centered("HOLD DURING PLAY TO PAUSE", 232, 1, C_CYAN);
    centered("THE CITY CHANGES AT NIGHT", 255, 1, C_MUTED);
  }

  char page[12];
  snprintf(page, sizeof(page), "%u / 2", city.howToPage + 1);
  centered(page, 280, 1, C_GOLD);
  centered("CLICK PAGE  HOLD BACK", 296, 1, C_MUTED);
}

static void drawOverlayPanel(const char *title) {
  frame.fillRoundRect(18, 62, 136, 210, 12, C_PANEL);
  frame.drawRoundRect(18, 62, 136, 210, 12, C_GOLD);
  centered(title, 77, 2, C_WHITE);
  frame.drawFastHLine(31, 105, 110, C_BORDER);
}

static void drawPaused(uint32_t now) {
  drawGameplay(now);
  drawOverlayPanel("PAUSED");
  drawMenuOption("RESUME", 124, city.pauseMenu.selected() == 0);
  drawMenuOption("RESTART", 163, city.pauseMenu.selected() == 1);
  drawMenuOption("POCKETGAME", 202, city.pauseMenu.selected() == 2);
  centered("CLICK NEXT  HOLD SELECT", 244, 1, C_MUTED);
}

static void drawResult(uint32_t now) {
  drawCityBackground(now, true);
  drawTower(now);
  frame.fillRoundRect(10, 28, 152, 278, 12, C_PANEL);
  frame.drawRoundRect(10, 28, 152, 278, 12,
                      city.completed ? C_GREEN : C_RED);
  centered(city.completed ? "CITY COMPLETE" : "BUILDING CLOSED", 43, 1,
           city.completed ? C_GREEN : C_RED);

  char buffer[26];
  textLeft("SCORE", 25, 75, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)city.score);
  textRight(buffer, 147, 73, 2, C_GOLD);

  textLeft("FLOORS", 25, 105, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%u", city.floorCount);
  textRight(buffer, 147, 104, 1, C_TEXT);

  textLeft("RESIDENTS", 25, 126, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)city.population);
  textRight(buffer, 147, 126, 1, C_CYAN);

  textLeft("BEST COMBO", 25, 147, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "X%u", city.bestCombo);
  textRight(buffer, 147, 147, 1, C_GREEN);

  if (city.newBest) {
    centered("NEW HIGH SCORE!", 169, 1, C_GOLD);
  }

  drawMenuOption("BUILD AGAIN", 190, city.resultMenu.selected() == 0);
  drawMenuOption("GAME MENU", 224, city.resultMenu.selected() == 1);
  drawMenuOption("POCKETGAME", 258, city.resultMenu.selected() == 2);
  centered("CLICK NEXT  HOLD SELECT", 291, 1, C_MUTED);
}

static void renderFrame(uint32_t now) {
  switch (city.screens.current()) {
  case GameScreen::MENU:
    drawTitleMenu(now);
    break;
  case GameScreen::HOW_TO:
    drawHowTo(now);
    break;
  case GameScreen::PLAYING:
    drawGameplay(now);
    break;
  case GameScreen::PAUSED:
    drawPaused(now);
    break;
  case GameScreen::RESULT:
    drawResult(now);
    break;
  }
  pocketSystem->display().present();
}

static void updateLed(uint32_t now) {
  PocketLed &led = pocketSystem->led();
  if (city.screens.is(GameScreen::MENU) ||
      city.screens.is(GameScreen::HOW_TO)) {
    const uint8_t pulse = PocketLed::breatheValue(now, 1450, 20, 125);
    led.set(0, pulse / 2, pulse);
    return;
  }
  if (city.screens.is(GameScreen::PAUSED)) {
    const uint8_t pulse = PocketLed::breatheValue(now, 1800, 18, 95);
    led.set(pulse, pulse / 2, 0);
    return;
  }
  if (city.screens.is(GameScreen::RESULT)) {
    const bool lit = PocketLed::doubleBlink(now, 950);
    if (city.completed || city.newBest) {
      led.set(0, lit ? 180 : 20, lit ? 60 : 5);
    } else {
      led.set(lit ? 220 : 18, 0, 0);
    }
    return;
  }
  if (now < city.perfectFlashUntil) {
    led.set(0, 235, 75);
  } else if (city.feedbackLife > 0.0f && city.feedbackColour == C_RED) {
    const bool lit = ((now / 85U) & 1U) == 0;
    led.set(lit ? 235 : 20, 0, 0);
  } else {
    const uint8_t pulse = PocketLed::breatheValue(now, 900, 35, 135);
    led.set(0, pulse, pulse + 35);
  }
}

} // namespace

void CityTowerGame::begin(PocketGameSystem &system) {
  pocketSystem = &system;
  randomGenerator.state = esp_random() ^ 0x9E3779B9UL;
  frame.setTextWrap(false);
  loadProgress();
  city.titleMenu.reset(3, 0);
  city.screens.goTo(GameScreen::MENU, millis());
  city.lastFrameAt = millis();
  city.feedbackLife = 0.0f;
  clearEffects();
  renderFrame(millis());
  Serial.println("CITY TOWER started.");
}

void CityTowerGame::loop(PocketGameSystem &system, uint32_t now) {
  pocketSystem = &system;
  handleInput(now);
  if (system.activeGame() != this) {
    return;
  }

  if (now - city.lastFrameAt < FRAME_INTERVAL_MS) {
    updateLed(now);
    delay(1);
    return;
  }

  float deltaSeconds = (now - city.lastFrameAt) / 1000.0f;
  if (deltaSeconds > MAX_DELTA_SECONDS) {
    deltaSeconds = MAX_DELTA_SECONDS;
  }
  city.lastFrameAt = now;

  if (city.screens.is(GameScreen::PLAYING)) {
    updatePlaying(deltaSeconds, now);
  }

  renderFrame(now);
  updateLed(now);
}
