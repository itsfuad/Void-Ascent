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
static constexpr uint8_t MAX_PARTICLES = 40;

// Gameplay blocks are intentionally almost square. The old 64 x 24 blocks
// looked like slabs and also allowed unrealistic staircase towers.
static constexpr float FLOOR_HEIGHT = 44.0f;
static constexpr float FLOOR_WIDTH = 44.0f;
static constexpr float FOUNDATION_WIDTH = 76.0f;
static constexpr float MIN_CONTACT_WIDTH = 9.0f;
static constexpr float STABILITY_MARGIN = 2.0f;

static constexpr float GRAVITY = 310.0f;
static constexpr float COLLAPSE_GRAVITY = 265.0f;

// The built tower behaves like a damped flexible mast. These limits keep the
// motion readable without letting the screen-space animation become chaotic.
static constexpr float TOWER_MAX_ANGLE = 0.075f;
static constexpr float TOWER_MAX_ANGULAR_SPEED = 0.28f;
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
  uint16_t residents = 0;

  // Detached floors use screen-space motion during a collapse. Keeping the
  // collapse animation on the floor itself avoids another large object pool.
  bool detached = false;
  float detachedX = SCREEN_W * 0.5f;
  float detachedY = 0.0f;
  float velocityX = 0.0f;
  float velocityY = 0.0f;
};

struct ActiveBlock {
  BlockMode mode = BlockMode::HANGING;
  float centreX = SCREEN_W * 0.5f;
  float topY = 95.0f;
  float velocityX = 0.0f;
  float velocityY = 0.0f;
  float swingPhase = 0.0f;
  float swingSpeed = 1.35f;
  float swingAmplitude = 0.20f;
  float ropeLength = 86.0f;
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
  float towerAngle = 0.0f;
  float towerAngularVelocity = 0.0f;

  bool collapsing = false;
  uint8_t collapseStartIndex = 0;
  uint8_t collapseOriginalCount = 0;
  uint32_t collapseStartedAt = 0;
  bool progressSaved = false;

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
  // Keep the crane trolley fixed. Only the cable and suspended block swing,
  // which is easier to read and gives the player one predictable motion.
  (void)now;
  return SCREEN_W * 0.5f;
}

static float towerSwayOffset(uint8_t index, uint8_t count, uint32_t now);

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

static void clearEffects() {
  for (uint8_t index = 0; index < MAX_PARTICLES; ++index) {
    city.particles[index].active = false;
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
  block.hookY = 40.0f + cosf(angle) * block.ropeLength;
  block.centreX = block.hookX;
  block.topY = block.hookY + 8.0f;
}

static void spawnBlock(uint32_t now) {
  ActiveBlock &block = city.active;
  block.mode = BlockMode::HANGING;
  block.swingPhase = randomGenerator.range(-0.9f, 0.9f);
  block.swingSpeed =
      clampFloat(1.30f + city.floorCount * 0.026f, 1.30f, 2.10f);
  block.swingAmplitude =
      clampFloat(0.18f + city.floorCount * 0.0045f, 0.18f, 0.34f);
  block.ropeLength =
      clampFloat(88.0f - city.floorCount * 0.08f, 84.0f, 88.0f);
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
  // Releasing a crane cable should always start a drop. The old tangent
  // velocity could launch the block upward, which felt random on one button.
  block.velocityY = clampFloat(
      -sinf(angle) * block.ropeLength * angleVelocity + 12.0f, 10.0f, 36.0f);
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
  city.towerAngle = 0.0f;
  city.towerAngularVelocity = 0.0f;
  city.collapsing = false;
  city.collapseStartIndex = 0;
  city.collapseOriginalCount = 0;
  city.collapseStartedAt = 0;
  city.progressSaved = false;

  for (uint8_t index = 0; index < MAX_FLOORS; ++index) {
    city.floors[index].residents = 0;
    city.floors[index].detached = false;
    city.floors[index].velocityX = 0.0f;
    city.floors[index].velocityY = 0.0f;
  }

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
  if (city.progressSaved) {
    return;
  }

  city.completed = completed;
  city.progressSaved = true;
  saveProgress();
  city.resultMenu.reset(3, 0);
  city.screens.goTo(GameScreen::RESULT, now);
  pocketSystem->led().off();
}


static bool centreOfMassHasSupport(float centreOfMass, float lowerCentre,
                                   float lowerWidth, float upperCentre,
                                   float upperWidth) {
  const float lowerLeft = lowerCentre - lowerWidth * 0.5f;
  const float lowerRight = lowerCentre + lowerWidth * 0.5f;
  const float upperLeft = upperCentre - upperWidth * 0.5f;
  const float upperRight = upperCentre + upperWidth * 0.5f;

  const float contactLeft = lowerLeft > upperLeft ? lowerLeft : upperLeft;
  const float contactRight = lowerRight < upperRight ? lowerRight : upperRight;
  const float contactWidth = contactRight - contactLeft;

  if (contactWidth < MIN_CONTACT_WIDTH) {
    return false;
  }

  const float margin =
      STABILITY_MARGIN < contactWidth * 0.18f
          ? STABILITY_MARGIN
          : contactWidth * 0.18f;

  return centreOfMass >= contactLeft + margin &&
         centreOfMass <= contactRight - margin;
}

// Returns the first floor in the unsupported upper section. A return value of
// -1 means every interface can carry the centre of mass above it.
//
// This is the key difference from the old overlap-only model: a staircase can
// have local overlap at every floor while the combined upper mass is already
// outside a lower contact patch.
static int8_t firstUnstableFloor() {
  if (city.floorCount == 0) {
    return -1;
  }

  float wholeTowerSum = 0.0f;
  for (uint8_t index = 0; index < city.floorCount; ++index) {
    wholeTowerSum += city.floors[index].centreX;
  }

  const float wholeTowerCentre = wholeTowerSum / city.floorCount;
  if (!centreOfMassHasSupport(
          wholeTowerCentre, SCREEN_W * 0.5f, FOUNDATION_WIDTH,
          city.floors[0].centreX, city.floors[0].width)) {
    return 0;
  }

  for (uint8_t lowerIndex = 0; lowerIndex + 1 < city.floorCount;
       ++lowerIndex) {
    float upperSum = 0.0f;
    uint8_t upperCount = 0;

    for (uint8_t upperIndex = lowerIndex + 1;
         upperIndex < city.floorCount; ++upperIndex) {
      upperSum += city.floors[upperIndex].centreX;
      ++upperCount;
    }

    const float upperCentreOfMass = upperSum / upperCount;
    const Floor &lower = city.floors[lowerIndex];
    const Floor &upper = city.floors[lowerIndex + 1];

    if (!centreOfMassHasSupport(upperCentreOfMass, lower.centreX, lower.width,
                                upper.centreX, upper.width)) {
      return (int8_t)(lowerIndex + 1);
    }
  }

  return -1;
}

static float towerCentreOfMass() {
  if (city.floorCount == 0) {
    return SCREEN_W * 0.5f;
  }

  float sum = 0.0f;
  for (uint8_t index = 0; index < city.floorCount; ++index) {
    sum += city.floors[index].centreX;
  }
  return sum / city.floorCount;
}

static float towerCrookedness() {
  if (city.floorCount < 2) {
    return 0.0f;
  }

  float maximumStep = 0.0f;
  for (uint8_t index = 1; index < city.floorCount; ++index) {
    const float step =
        fabsf(city.floors[index].centreX - city.floors[index - 1].centreX);
    if (step > maximumStep) {
      maximumStep = step;
    }
  }

  const float centreOffset =
      fabsf(towerCentreOfMass() - SCREEN_W * 0.5f);
  const float stepStress = maximumStep / (FLOOR_WIDTH * 0.34f);
  const float centreStress = centreOffset / (FOUNDATION_WIDTH * 0.18f);
  return clampFloat(stepStress > centreStress ? stepStress : centreStress,
                    0.0f, 1.6f);
}

static void updateTowerMotion(float deltaSeconds, uint32_t now) {
  if (city.floorCount == 0 || city.collapsing) {
    const float settle = 1.0f - expf(-7.0f * deltaSeconds);
    city.towerAngle = lerpFloat(city.towerAngle, 0.0f, settle);
    city.towerAngularVelocity *= expf(-6.0f * deltaSeconds);
    return;
  }

  const float floorFactor = clampFloat(city.floorCount / 12.0f, 0.12f, 1.7f);
  const float crookedness = towerCrookedness();
  const float centreOffset = towerCentreOfMass() - SCREEN_W * 0.5f;

  // A crooked stack has a biased resting angle. Taller towers have a softer
  // spring and therefore visibly swing for longer after each landing.
  const float equilibriumAngle =
      clampFloat(centreOffset * 0.0035f, -0.052f, 0.052f);
  const float springStrength = 8.2f / (1.0f + city.floorCount * 0.055f);
  const float damping = clampFloat(3.15f - floorFactor * 0.72f, 1.75f, 3.0f);

  // Wind is intentionally weak on a straight short tower, but crookedness
  // increases the visible oscillation and warns the player before collapse.
  const float wind =
      sinf(now * 0.00108f + 0.65f) *
      (0.0020f + floorFactor * 0.0032f) * (0.40f + crookedness * 0.75f);

  const float angularAcceleration =
      (equilibriumAngle - city.towerAngle) * springStrength + wind -
      city.towerAngularVelocity * damping;

  city.towerAngularVelocity += angularAcceleration * deltaSeconds;
  city.towerAngularVelocity =
      clampFloat(city.towerAngularVelocity, -TOWER_MAX_ANGULAR_SPEED,
                 TOWER_MAX_ANGULAR_SPEED);
  city.towerAngle += city.towerAngularVelocity * deltaSeconds;

  if (city.towerAngle > TOWER_MAX_ANGLE) {
    city.towerAngle = TOWER_MAX_ANGLE;
    if (city.towerAngularVelocity > 0.0f) {
      city.towerAngularVelocity *= -0.28f;
    }
  } else if (city.towerAngle < -TOWER_MAX_ANGLE) {
    city.towerAngle = -TOWER_MAX_ANGLE;
    if (city.towerAngularVelocity < 0.0f) {
      city.towerAngularVelocity *= -0.28f;
    }
  }
}

// Uses the instantaneous tower angle to test whether oscillation has moved an
// upper section's effective centre of mass outside its supporting contact.
// A statically valid but badly crooked tower can therefore sway itself down.
static int8_t firstDynamicallyUnstableFloor() {
  if (city.floorCount < 3 || fabsf(city.towerAngle) < 0.008f) {
    return -1;
  }

  const float horizontalPerHeight = sinf(city.towerAngle);

  float wholeX = 0.0f;
  float wholeHeight = 0.0f;
  for (uint8_t index = 0; index < city.floorCount; ++index) {
    wholeX += city.floors[index].centreX;
    wholeHeight += (index + 0.5f) * FLOOR_HEIGHT;
  }

  const float wholeCentre =
      wholeX / city.floorCount +
      horizontalPerHeight * (wholeHeight / city.floorCount);
  if (!centreOfMassHasSupport(
          wholeCentre, SCREEN_W * 0.5f, FOUNDATION_WIDTH,
          city.floors[0].centreX, city.floors[0].width)) {
    return 0;
  }

  for (uint8_t lowerIndex = 0; lowerIndex + 1 < city.floorCount;
       ++lowerIndex) {
    float upperX = 0.0f;
    float upperRelativeHeight = 0.0f;
    uint8_t upperCount = 0;
    const float contactHeight = (lowerIndex + 1.0f) * FLOOR_HEIGHT;

    for (uint8_t upperIndex = lowerIndex + 1;
         upperIndex < city.floorCount; ++upperIndex) {
      upperX += city.floors[upperIndex].centreX;
      upperRelativeHeight +=
          (upperIndex + 0.5f) * FLOOR_HEIGHT - contactHeight;
      ++upperCount;
    }

    const float effectiveUpperCentre =
        upperX / upperCount +
        horizontalPerHeight * (upperRelativeHeight / upperCount);
    const Floor &lower = city.floors[lowerIndex];
    const Floor &upper = city.floors[lowerIndex + 1];

    if (!centreOfMassHasSupport(effectiveUpperCentre, lower.centreX,
                                lower.width, upper.centreX, upper.width)) {
      return (int8_t)(lowerIndex + 1);
    }
  }

  return -1;
}

static void beginTowerCollapse(uint8_t startIndex, uint32_t now) {
  if (startIndex >= city.floorCount) {
    return;
  }

  city.collapsing = true;
  city.collapseStartIndex = startIndex;
  city.collapseOriginalCount = city.floorCount;
  city.collapseStartedAt = now;
  city.active.mode = BlockMode::WAITING;
  city.combo = 0;

  const float supportCentre =
      startIndex == 0
          ? SCREEN_W * 0.5f
          : city.floors[startIndex - 1].centreX +
                towerSwayOffset(startIndex - 1, city.floorCount, now);

  float fallingCentre = 0.0f;
  for (uint8_t index = startIndex; index < city.floorCount; ++index) {
    fallingCentre += city.floors[index].centreX +
                     towerSwayOffset(index, city.floorCount, now);
  }
  fallingCentre /= (city.floorCount - startIndex);

  float direction = fallingCentre >= supportCentre ? 1.0f : -1.0f;
  if (fabsf(fallingCentre - supportCentre) < 0.5f) {
    direction = (randomGenerator.next() & 1U) ? 1.0f : -1.0f;
  }

  for (uint8_t index = startIndex; index < city.floorCount; ++index) {
    Floor &floor = city.floors[index];
    const float heightAmount = (index - startIndex) + 1.0f;
    floor.detached = true;
    floor.detachedX =
        floor.centreX + towerSwayOffset(index, city.floorCount, now);
    floor.detachedY = floorTopScreenY(floor);
    floor.velocityX =
        direction * (24.0f + heightAmount * 5.0f) +
        randomGenerator.range(-5.0f, 5.0f);
    floor.velocityY = -18.0f - heightAmount * 2.5f;
  }

  const float seamY =
      startIndex == 0
          ? GROUND_Y + city.cameraWorld
          : floorTopScreenY(city.floors[startIndex - 1]);
  const float seamX =
      startIndex == 0
          ? SCREEN_W * 0.5f
          : city.floors[startIndex - 1].centreX +
                towerSwayOffset(startIndex - 1, city.floorCount, now);

  for (uint8_t index = 0; index < 18; ++index) {
    spawnParticle(seamX, seamY, randomGenerator.range(-75.0f, 75.0f),
                  randomGenerator.range(-80.0f, 5.0f),
                  randomGenerator.range(0.45f, 0.95f),
                  index & 1U ? C_STONE : C_ORANGE);
  }

  setFeedback("TOWER UNSTABLE!", C_RED, 1.45f);
}

static void updateTowerCollapse(float deltaSeconds, uint32_t now) {
  bool allGone = true;

  for (uint8_t index = city.collapseStartIndex;
       index < city.collapseOriginalCount; ++index) {
    Floor &floor = city.floors[index];
    if (!floor.detached) {
      continue;
    }

    floor.velocityY += COLLAPSE_GRAVITY * deltaSeconds;
    floor.detachedX += floor.velocityX * deltaSeconds;
    floor.detachedY += floor.velocityY * deltaSeconds;
    floor.velocityX *= 0.996f;

    if (floor.detachedY < SCREEN_H + FLOOR_HEIGHT + 20.0f &&
        floor.detachedX > -FLOOR_WIDTH - 20.0f &&
        floor.detachedX < SCREEN_W + FLOOR_WIDTH + 20.0f) {
      allGone = false;
    }
  }

  if (!allGone && now - city.collapseStartedAt < 1850U) {
    return;
  }

  uint32_t displacedResidents = 0;
  for (uint8_t index = city.collapseStartIndex;
       index < city.collapseOriginalCount; ++index) {
    displacedResidents += city.floors[index].residents;
    city.floors[index].detached = false;
  }

  city.population = displacedResidents < city.population
                        ? city.population - displacedResidents
                        : 0U;
  city.floorCount = city.collapseStartIndex;
  city.towerTopWorld = city.floorCount * FLOOR_HEIGHT;
  city.collapsing = false;
  city.towerLean = clampFloat(
      (towerCentreOfMass() - SCREEN_W * 0.5f) * 0.16f, -5.0f, 5.0f);
  city.towerAngle *= city.floorCount == 0 ? 0.0f : 0.38f;
  city.towerAngularVelocity *= -0.22f;

  const float desiredCamera =
      city.towerTopWorld > 76.0f ? city.towerTopWorld - 76.0f : 0.0f;
  city.cameraTarget = desiredCamera;

  if (city.lives > 0) {
    --city.lives;
  }

  if (city.lives == 0) {
    finishRun(now, false);
    return;
  }

  city.active.mode = BlockMode::WAITING;
  city.nextBlockAt = now + 520U;
  setFeedback("STRUCTURE LOST", C_ORANGE, 0.9f);
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
  const uint8_t previousFloorCount = city.floorCount;
  const float supportRestCentre =
      previousFloorCount == 0
          ? SCREEN_W * 0.5f
          : city.floors[previousFloorCount - 1].centreX;
  const float supportSway =
      previousFloorCount == 0
          ? 0.0f
          : towerSwayOffset(previousFloorCount - 1, previousFloorCount, now);
  const float supportCentre = supportRestCentre + supportSway;
  const float supportWidth =
      previousFloorCount == 0
          ? FOUNDATION_WIDTH
          : city.floors[previousFloorCount - 1].width;

  const float blockLeft = city.active.centreX - FLOOR_WIDTH * 0.5f;
  const float blockRight = city.active.centreX + FLOOR_WIDTH * 0.5f;
  const float supportLeft = supportCentre - supportWidth * 0.5f;
  const float supportRight = supportCentre + supportWidth * 0.5f;
  const float overlap =
      (blockRight < supportRight ? blockRight : supportRight) -
      (blockLeft > supportLeft ? blockLeft : supportLeft);

  if (overlap < MIN_CONTACT_WIDTH) {
    missBlock(now);
    return;
  }

  if (previousFloorCount >= MAX_FLOORS) {
    finishRun(now, true);
    return;
  }

  const float landingOffset = city.active.centreX - supportCentre;
  const float centreError = fabsf(landingOffset);
  const float overlapRatio = clampFloat(overlap / FLOOR_WIDTH, 0.0f, 1.0f);
  LandingQuality quality = LandingQuality::ROUGH;

  if (centreError <= 2.4f) {
    quality = LandingQuality::PERFECT;
  } else if (overlapRatio >= 0.70f) {
    quality = LandingQuality::GOOD;
  }

  const uint8_t futureFloorCount = previousFloorCount + 1;
  const float newFloorSway =
      towerSwayOffset(previousFloorCount, futureFloorCount, now);

  Floor &floor = city.floors[previousFloorCount];
  // Store the unswung structural position. Subtracting the current animation
  // offset prevents the new module from snapping sideways after attachment.
  floor.centreX = city.active.centreX - newFloorSway;
  floor.width = FLOOR_WIDTH;
  floor.bottomWorld = city.towerTopWorld;
  floor.style = city.active.style;
  floor.quality = quality;
  floor.seed = city.active.seed;
  floor.residents = 0;
  floor.detached = false;
  floor.velocityX = 0.0f;
  floor.velocityY = 0.0f;

  city.floorCount = futureFloorCount;
  city.towerTopWorld += FLOOR_HEIGHT;

  // Validate the complete tower after the new load is applied. Direct overlap
  // alone is not enough: every lower contact must contain the centre of mass
  // of all floors above it.
  const int8_t unstableFloor = firstUnstableFloor();
  if (unstableFloor >= 0) {
    city.perfectFlashUntil = 0;
    beginTowerCollapse((uint8_t)unstableFloor, now);
    return;
  }

  if (quality == LandingQuality::PERFECT) {
    city.combo = city.combo < 12 ? city.combo + 1 : 12;
    if (city.combo > city.bestCombo) {
      city.bestCombo = city.combo;
    }
    setFeedback(city.combo >= 3 ? "PERFECT COMBO!" : "PERFECT!", C_GREEN,
                1.15f);
    city.perfectFlashUntil = now + 170U;
  } else if (quality == LandingQuality::GOOD) {
    if (city.combo > 0) {
      --city.combo;
    }
    setFeedback("GOOD DROP", C_CYAN, 0.9f);
  } else {
    city.combo = 0;
    setFeedback("ROUGH LANDING", C_ORANGE, 0.95f);
  }

  const uint32_t base = 90U + (uint32_t)(overlapRatio * 170.0f);
  const uint32_t comboBonus =
      quality == LandingQuality::PERFECT ? city.combo * 85U : city.combo * 20U;
  city.score += base + comboBonus;

  const uint32_t residents =
      5U + (uint32_t)(overlapRatio * 7.0f) +
      (quality == LandingQuality::PERFECT ? city.combo * 2U : 0U);
  floor.residents = residents > 65535U ? 65535U : (uint16_t)residents;
  city.population += floor.residents;

  const float landingY = towerTopScreenY();
  spawnLandingBurst(city.active.centreX, landingY,
                    quality == LandingQuality::PERFECT);

  city.towerLean = clampFloat(
      (towerCentreOfMass() - SCREEN_W * 0.5f) * 0.16f, -5.0f, 5.0f);

  // Landing offset and inherited horizontal speed inject momentum into the
  // tower. Off-centre drops therefore produce an immediately visible swing.
  const float impactKick =
      landingOffset * 0.0042f + city.active.velocityX * 0.00042f;
  city.towerAngularVelocity =
      clampFloat(city.towerAngularVelocity + impactKick,
                 -TOWER_MAX_ANGULAR_SPEED, TOWER_MAX_ANGULAR_SPEED);

  // Keep the top near y=210. This leaves a visible drop gap below the longer
  // cable while still showing several square floors.
  city.cameraTarget =
      city.towerTopWorld > 76.0f ? city.towerTopWorld - 76.0f : 0.0f;

  city.active.mode = BlockMode::WAITING;
  city.nextBlockAt = now + (quality == LandingQuality::PERFECT ? 570U : 430U);

  if (city.floorCount >= MAX_FLOORS) {
    finishRun(now, true);
  }
}

static void updatePlaying(float deltaSeconds, uint32_t now) {
  const float cameraFollow = 1.0f - expf(-5.2f * deltaSeconds);
  city.cameraWorld =
      lerpFloat(city.cameraWorld, city.cameraTarget, cameraFollow);

  updateTowerMotion(deltaSeconds, now);

  if (!city.collapsing) {
    const int8_t dynamicFailure = firstDynamicallyUnstableFloor();
    if (dynamicFailure >= 0) {
      beginTowerCollapse((uint8_t)dynamicFailure, now);
    }
  }

  if (city.collapsing) {
    updateTowerCollapse(deltaSeconds, now);
    city.feedbackLife = city.feedbackLife > deltaSeconds
                            ? city.feedbackLife - deltaSeconds
                            : 0.0f;
    updateEffects(deltaSeconds);
    return;
  }

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

    if (block.mode == BlockMode::MISSED && block.topY > SCREEN_H + FLOOR_HEIGHT + 12.0f) {
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
                           LandingQuality quality,
                           float height = FLOOR_HEIGHT) {
  const int16_t x = (int16_t)roundf(centreX - width * 0.5f);
  const int16_t y = (int16_t)roundf(topY);
  const int16_t w = (int16_t)roundf(width);
  const int16_t h = (int16_t)roundf(height);

  if (w < 12 || h < 12 || y > SCREEN_H || y + h < 0 || x > SCREEN_W ||
      x + w < 0) {
    return;
  }

  uint16_t body;
  uint16_t dark;
  uint16_t trim;
  floorColours(style, body, dark, trim);

  // Drop shadow, outer shell and inset facade.
  frame.fillRect(x + 3, y + 4, w, h, C_INK);
  frame.fillRect(x, y, w, h, dark);
  frame.fillRect(x + 3, y + 3, w - 7, h - 6, body);
  frame.fillRect(x + 1, y, w - 2, 3, trim);
  frame.fillRect(x + 2, y + h - 4, w - 5, 3, trim);
  frame.drawRect(x, y, w, h, C_INK);
  frame.drawFastVLine(x + w - 4, y + 3, h - 6,
                      blend565(dark, C_BLACK, 80));

  if (h >= 34) {
    // Two rows of windows make the square modules read as complete rooms
    // instead of stretched floor slabs.
    static constexpr uint8_t WINDOW_COLUMNS = 3;
    static constexpr uint8_t WINDOW_ROWS = 2;
    const int16_t windowW = 8;
    const int16_t windowH = 9;
    const int16_t usableW = w - 10;
    const int16_t columnStep = usableW / WINDOW_COLUMNS;
    const int16_t firstY = y + 7;
    const int16_t rowStep = (h - 15) / WINDOW_ROWS;

    for (uint8_t row = 0; row < WINDOW_ROWS; ++row) {
      for (uint8_t column = 0; column < WINDOW_COLUMNS; ++column) {
        const int16_t wx =
            x + 5 + column * columnStep + (columnStep - windowW) / 2;
        const int16_t wy = firstY + row * rowStep;
        const uint8_t bitIndex = (row * WINDOW_COLUMNS + column) * 3;
        const bool lit = ((seed >> bitIndex) & 3U) != 0;

        frame.fillRect(wx, wy, windowW, windowH, C_INK);
        frame.fillRect(wx + 1, wy + 1, windowW - 2, windowH - 2,
                       lit ? C_GOLD
                           : blend565(C_GLASS_DARK, C_SKY, 80));
        frame.drawFastVLine(wx + windowW / 2, wy + 1, windowH - 2, dark);
        frame.drawFastHLine(wx + 1, wy + windowH / 2, windowW - 2, dark);
      }
    }

    frame.drawFastHLine(x + 3, y + h / 2, w - 7,
                        blend565(trim, body, 95));

    // Small utility hatch adds detail without implying that every module has
    // a ground-floor entrance.
    frame.fillRect(x + w / 2 - 4, y + h - 11, 8, 7, C_INK);
    frame.drawPixel(x + w / 2 + 2, y + h - 8, C_GOLD);
  } else {
    // Compact rendering used by menu illustrations.
    for (uint8_t column = 0; column < 3; ++column) {
      const int16_t wx = x + 5 + column * ((w - 10) / 3);
      const bool lit = ((seed >> (column * 3)) & 3U) != 0;
      frame.fillRect(wx, y + 6, 6, 7, C_INK);
      frame.fillRect(wx + 1, y + 7, 4, 5, lit ? C_GOLD : C_GLASS_DARK);
    }
  }

  if (style % 4 == 0 && h >= 34) {
    // Sparse brick seams.
    frame.drawFastHLine(x + 4, y + 5, w - 9,
                        blend565(body, dark, 75));
    frame.drawFastHLine(x + 4, y + h - 14, w - 9,
                        blend565(body, dark, 75));
  } else if (style % 4 == 3 && h >= 34) {
    // Glass facade mullions.
    frame.drawFastVLine(x + w / 2, y + 4, h - 9,
                        blend565(trim, dark, 110));
  }

  if (quality == LandingQuality::ROUGH && !hanging) {
    frame.drawLine(x + 6, y + 4, x + 13, y + 9, C_ORANGE);
    frame.drawLine(x + 13, y + 9, x + 9, y + 15, C_ORANGE);
  } else if (quality == LandingQuality::PERFECT && !hanging) {
    frame.drawPixel(x + 2, y + 1, C_WHITE);
    frame.drawPixel(x + w - 3, y + 1, C_WHITE);
  }

  if (hanging) {
    frame.fillRect(x + 7, y - 3, w - 14, 3, C_GOLD);
    frame.drawFastVLine(x + 7, y - 8, 6, C_INK);
    frame.drawFastVLine(x + w - 8, y - 8, 6, C_INK);
  }
}

static float towerSwayOffset(uint8_t index, uint8_t count, uint32_t now) {
  if (count < 2 || index >= count) {
    return 0.0f;
  }

  const float height = (index + 1.0f) * FLOOR_HEIGHT;
  const float heightAmount = (index + 1.0f) / count;
  const float bend = heightAmount * heightAmount;

  // The shared angle is the actual damped tower motion. Multiplying by height
  // anchors the base while making upper floors travel farther, as a tall mast
  // should. A very small flex term keeps the structure from looking rigid.
  const float rigidSwing = sinf(city.towerAngle) * height;
  const float floorFactor = count < 24 ? count : 24;
  const float flex = sinf(now * 0.00131f + 0.22f) * bend *
                     (0.12f + floorFactor * 0.012f) *
                     (0.55f + towerCrookedness() * 0.35f);
  return rigidSwing + flex;
}

static float floorSwayOffset(uint8_t index, uint32_t now) {
  return towerSwayOffset(index, city.floorCount, now);
}

static void drawTower(uint32_t now) {
  drawGround();

  // The foundation remains visible under the first module instead of
  // disappearing as soon as construction starts.
  const int16_t baseY =
      (int16_t)roundf(GROUND_Y + city.cameraWorld - 10);
  const int16_t foundationX =
      (int16_t)roundf(SCREEN_W * 0.5f - FOUNDATION_WIDTH * 0.5f);
  const int16_t foundationW = (int16_t)roundf(FOUNDATION_WIDTH);

  frame.fillRect(foundationX - 4, baseY, foundationW + 8, 10,
                 C_STONE_DARK);
  frame.fillRect(foundationX, baseY - 4, foundationW, 5, C_STONE);
  frame.drawFastHLine(foundationX, baseY - 4, foundationW, C_WHITE);

  for (uint8_t index = 0; index < city.floorCount; ++index) {
    const Floor &floor = city.floors[index];

    if (floor.detached) {
      drawFloorBlock(floor.detachedX, floor.detachedY, floor.width,
                     floor.style, floor.seed, false, floor.quality);
      continue;
    }

    drawFloorBlock(floor.centreX + floorSwayOffset(index, now),
                   floorTopScreenY(floor), floor.width, floor.style, floor.seed,
                   false, floor.quality);
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

  if (city.active.mode != BlockMode::HANGING) {
    return;
  }

  const int16_t hookX = (int16_t)roundf(city.active.hookX);
  const int16_t hookY = (int16_t)roundf(city.active.hookY);

  // Two-tone cable gives the long rope a readable chain-like edge on the
  // narrow display.
  frame.drawLine(anchorX, 40, hookX, hookY, C_INK);
  frame.drawLine(anchorX + 1, 40, hookX + 1, hookY, C_STONE);

  const int16_t cableHeight = hookY - 40;
  if (cableHeight > 12) {
    for (int16_t step = 8; step < cableHeight; step += 9) {
      const float amount = step / (float)cableHeight;
      const int16_t linkX =
          (int16_t)roundf(anchorX + (hookX - anchorX) * amount);
      const int16_t linkY = 40 + step;
      frame.drawPixel(linkX, linkY, C_WHITE);
    }
  }

  const int16_t leftAttach =
      (int16_t)roundf(city.active.centreX - FLOOR_WIDTH * 0.5f + 7.0f);
  const int16_t rightAttach =
      (int16_t)roundf(city.active.centreX + FLOOR_WIDTH * 0.5f - 7.0f);
  const int16_t blockTop = (int16_t)roundf(city.active.topY);

  frame.drawLine(hookX, hookY, leftAttach, blockTop, C_INK);
  frame.drawLine(hookX + 1, hookY, leftAttach + 1, blockTop, C_STONE);
  frame.drawLine(hookX, hookY, rightAttach, blockTop, C_INK);
  frame.drawLine(hookX + 1, hookY, rightAttach + 1, blockTop, C_STONE);
  frame.fillCircle(hookX, hookY, 3, C_GOLD);
  frame.drawPixel(hookX, hookY, C_WHITE);
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
  const int16_t baseY = 174;

  for (uint8_t index = 0; index < 2; ++index) {
    const float heightAmount = (index + 1.0f) / 2.0f;
    const float sway = sinf(now * 0.00155f) * heightAmount * heightAmount * 1.0f;
    drawFloorBlock(86 + sway, baseY - (index + 1) * 26, 34, index,
                   0x7412U + index * 331U, false, LandingQuality::GOOD,
                   26.0f);
  }

  const int16_t hookX = 86 + (int16_t)(sinf(now * 0.0022f) * 18.0f);
  frame.drawFastHLine(24, 51, 124, C_GOLD);
  frame.drawLine(86, 52, hookX, 84, C_INK);
  drawFloorBlock(hookX, 89, 34, 2, 0x9911U, true, LandingQuality::PERFECT,
                 26.0f);
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
    drawFloorBlock(hookX, 101, 44, 1, 0x1234U, true,
                   LandingQuality::GOOD);
    drawFloorBlock(86, 184, 44, 0, 0x5678U, false,
                   LandingQuality::GOOD);
    frame.drawLine(hookX, 146, 86, 179, C_CYAN);
    centered("1. WATCH THE SWING", 222, 1, C_TEXT);
    centered("2. CLICK TO DROP", 240, 1, C_TEXT);
    centered("ALIGN THE FLOOR EDGES", 263, 1, C_MUTED);
  } else {
    drawFloorBlock(86, 58, 44, 3, 0x1111U, false,
                   LandingQuality::PERFECT);
    drawFloorBlock(86, 102, 44, 2, 0x2222U, false,
                   LandingQuality::PERFECT);
    drawFloorBlock(86, 146, 44, 1, 0x3333U, false,
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
