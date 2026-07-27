#include "PocketCricketGame.h"

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
static constexpr uint8_t MAX_PARTICLES = 44;
static constexpr uint8_t BALLS_PER_INNINGS = 12;
static constexpr uint8_t MAX_WICKETS = 3;

static constexpr float RUNUP_SECONDS = 0.58f;
static constexpr float SWING_SECONDS = 0.31f;
static constexpr float FAR_CREASE_Y = 148.0f;
static constexpr float NEAR_CREASE_Y = 258.0f;
static constexpr float BATTER_BASE_Y = 259.0f;

static constexpr uint16_t C565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((uint16_t)(red & 0xF8) << 8) |
         ((uint16_t)(green & 0xFC) << 3) |
         ((uint16_t)blue >> 3);
}

static constexpr uint16_t C_BLACK = C565(5, 9, 15);
static constexpr uint16_t C_INK = C565(18, 28, 34);
static constexpr uint16_t C_NAVY = C565(8, 22, 43);
static constexpr uint16_t C_PANEL = C565(13, 34, 56);
static constexpr uint16_t C_PANEL_2 = C565(22, 51, 82);
static constexpr uint16_t C_BORDER = C565(55, 107, 164);
static constexpr uint16_t C_TEXT = C565(241, 246, 247);
static constexpr uint16_t C_MUTED = C565(137, 167, 187);
static constexpr uint16_t C_CYAN = C565(62, 207, 235);
static constexpr uint16_t C_BLUE = C565(57, 126, 224);
static constexpr uint16_t C_BLUE_DARK = C565(28, 73, 143);
static constexpr uint16_t C_GOLD = C565(255, 207, 72);
static constexpr uint16_t C_ORANGE = C565(246, 134, 51);
static constexpr uint16_t C_RED = C565(232, 63, 60);
static constexpr uint16_t C_GREEN = C565(87, 207, 93);
static constexpr uint16_t C_GREEN_DARK = C565(39, 137, 57);
static constexpr uint16_t C_LIME = C565(139, 230, 59);
static constexpr uint16_t C_PURPLE = C565(126, 92, 211);
static constexpr uint16_t C_WHITE = C565(255, 255, 255);

static constexpr uint16_t C_SKY = C565(30, 171, 224);
static constexpr uint16_t C_SKY_HIGH = C565(102, 221, 251);
static constexpr uint16_t C_DUSK = C565(91, 82, 155);
static constexpr uint16_t C_NIGHT = C565(12, 25, 58);
static constexpr uint16_t C_CLOUD = C565(242, 248, 241);
static constexpr uint16_t C_CLOUD_SHADE = C565(189, 216, 218);
static constexpr uint16_t C_STAND = C565(54, 77, 136);
static constexpr uint16_t C_STAND_DARK = C565(28, 44, 91);
static constexpr uint16_t C_GRASS = C565(104, 204, 85);
static constexpr uint16_t C_GRASS_DARK = C565(65, 166, 72);
static constexpr uint16_t C_PITCH = C565(194, 156, 103);
static constexpr uint16_t C_PITCH_DARK = C565(156, 119, 78);
static constexpr uint16_t C_PITCH_LINE = C565(235, 218, 182);
static constexpr uint16_t C_BROWN = C565(149, 91, 52);
static constexpr uint16_t C_BAT = C565(238, 219, 157);
static constexpr uint16_t C_SKIN = C565(222, 167, 116);
static constexpr uint16_t C_SHADOW = C565(35, 102, 49);

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
  textLeft(text, (SCREEN_W - textWidth(text, size)) / 2, y, size, colour);
}

struct RandomGenerator {
  uint32_t state = 0x9E3779B9UL;

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
enum class DeliveryType : uint8_t { FAST, SLOW, SPIN, YORKER, BOUNCER, SWINGER };
enum class BallPhase : uint8_t { WAITING, RUNUP, TRAVELLING, HIT, RESOLVED };
enum class OutcomeType : uint8_t { NONE, DOT, SINGLE, DOUBLE, FOUR, SIX, WICKET };

struct BallState {
  BallPhase phase = BallPhase::WAITING;
  DeliveryType type = DeliveryType::FAST;
  OutcomeType outcome = OutcomeType::NONE;

  float age = 0.0f;
  float x = 86.0f;
  float y = FAR_CREASE_Y;
  float z = 0.0f;
  float vx = 0.0f;
  float vy = 0.0f;
  float vz = 0.0f;
  float bounceY = 199.0f;
  float bounceLift = 25.0f;
  float driftAcceleration = 0.0f;
  float targetX = 86.0f;

  bool bounced = false;
  bool counted = false;
};

struct Particle {
  bool active = false;
  float x = 0.0f;
  float y = 0.0f;
  float vx = 0.0f;
  float vy = 0.0f;
  float life = 0.0f;
  uint16_t colour = C_WHITE;
};

struct BatGeometry {
  float handleX = 0.0f;
  float handleY = 0.0f;
  float bladeX = 0.0f;
  float bladeY = 0.0f;
};

struct CricketState {
  ScreenNavigator<GameScreen> screens{GameScreen::MENU};
  MenuNavigator titleMenu{3, 0};
  MenuNavigator pauseMenu{3, 0};
  MenuNavigator resultMenu{3, 0};

  uint8_t howToPage = 0;
  BallState ball;
  Particle particles[MAX_PARTICLES];

  float batterX = 86.0f;
  float batterTargetX = 86.0f;
  float batterDepth = 0.0f;
  float batterDepthTarget = 0.0f;
  float swingProgress = 0.0f;
  bool swinging = false;
  bool shotResolved = false;

  uint32_t runs = 0;
  uint8_t wickets = 0;
  uint8_t ballsFaced = 0;
  uint8_t fours = 0;
  uint8_t sixes = 0;

  uint32_t bestScore = 0;
  uint8_t bestFours = 0;
  uint8_t bestSixes = 0;

  uint32_t nextBallAt = 0;
  uint32_t resultUntil = 0;
  uint32_t lastFrameAt = 0;

  char feedback[22] = {};
  uint16_t feedbackColour = C_TEXT;
  float feedbackLife = 0.0f;

  bool endPending = false;
  bool progressSaved = false;
  bool newBest = false;
};

static CricketState cricket;

static const char *deliveryName(DeliveryType type) {
  switch (type) {
  case DeliveryType::FAST:
    return "FAST";
  case DeliveryType::SLOW:
    return "SLOWER";
  case DeliveryType::SPIN:
    return "SPIN";
  case DeliveryType::YORKER:
    return "YORKER";
  case DeliveryType::BOUNCER:
    return "BOUNCER";
  default:
    return "SWINGER";
  }
}

static void setFeedback(const char *text, uint16_t colour, float seconds) {
  strncpy(cricket.feedback, text, sizeof(cricket.feedback) - 1);
  cricket.feedback[sizeof(cricket.feedback) - 1] = '\0';
  cricket.feedbackColour = colour;
  cricket.feedbackLife = seconds;
}

static Particle *allocateParticle() {
  for (uint8_t index = 0; index < MAX_PARTICLES; ++index) {
    if (!cricket.particles[index].active) {
      cricket.particles[index].active = true;
      return &cricket.particles[index];
    }
  }
  const uint8_t replacement = randomGenerator.next() % MAX_PARTICLES;
  cricket.particles[replacement].active = true;
  return &cricket.particles[replacement];
}

static void spawnParticle(float x, float y, float vx, float vy,
                          float life, uint16_t colour) {
  Particle *particle = allocateParticle();
  particle->x = x;
  particle->y = y;
  particle->vx = vx;
  particle->vy = vy;
  particle->life = life;
  particle->colour = colour;
}

static void spawnBurst(float x, float y, uint8_t count,
                       uint16_t first, uint16_t second) {
  for (uint8_t index = 0; index < count; ++index) {
    const float angle = randomGenerator.range(3.3f, 6.1f);
    const float speed = randomGenerator.range(20.0f, 95.0f);
    spawnParticle(x, y, cosf(angle) * speed, sinf(angle) * speed,
                  randomGenerator.range(0.30f, 0.78f),
                  index & 1U ? first : second);
  }
}

static void clearParticles() {
  for (uint8_t index = 0; index < MAX_PARTICLES; ++index) {
    cricket.particles[index].active = false;
  }
}

static void updateParticles(float deltaSeconds) {
  for (uint8_t index = 0; index < MAX_PARTICLES; ++index) {
    Particle &particle = cricket.particles[index];
    if (!particle.active) {
      continue;
    }

    particle.life -= deltaSeconds;
    if (particle.life <= 0.0f) {
      particle.active = false;
      continue;
    }

    particle.vy += 90.0f * deltaSeconds;
    particle.x += particle.vx * deltaSeconds;
    particle.y += particle.vy * deltaSeconds;
  }
}

static void thickLine(float x1, float y1, float x2, float y2,
                      uint8_t width, uint16_t colour) {
  const float dx = x2 - x1;
  const float dy = y2 - y1;
  const int16_t steps =
      (int16_t)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));

  if (steps <= 0) {
    frame.fillCircle((int16_t)roundf(x1), (int16_t)roundf(y1), width / 2,
                     colour);
    return;
  }

  for (int16_t step = 0; step <= steps; ++step) {
    const float amount = step / (float)steps;
    frame.fillCircle((int16_t)roundf(x1 + dx * amount),
                     (int16_t)roundf(y1 + dy * amount), width / 2, colour);
  }
}

static void outlinedLine(float x1, float y1, float x2, float y2,
                         uint8_t width, uint16_t colour) {
  thickLine(x1, y1, x2, y2, width + 2, C_INK);
  thickLine(x1, y1, x2, y2, width, colour);
}

static void drawWickets(float centreX, float groundY, float scale,
                        bool broken = false) {
  const int16_t height = (int16_t)roundf(17.0f * scale);
  const int16_t gap = (int16_t)roundf(5.0f * scale);
  const int16_t x = (int16_t)roundf(centreX);
  const int16_t y = (int16_t)roundf(groundY);

  for (int8_t stump = -1; stump <= 1; ++stump) {
    const int16_t stumpX = x + stump * gap;
    if (broken && stump == 1) {
      frame.drawLine(stumpX, y - height, stumpX + 5, y - 2, C_BAT);
    } else {
      frame.drawFastVLine(stumpX, y - height, height, C_BAT);
      frame.drawFastVLine(stumpX + 1, y - height, height, C_BROWN);
    }
  }

  if (broken) {
    frame.drawLine(x - gap - 2, y - height - 2, x - 1, y - height - 5,
                   C_WHITE);
    frame.drawLine(x + 1, y - height - 4, x + gap + 5, y - height - 7,
                   C_WHITE);
  } else {
    frame.drawFastHLine(x - gap - 2, y - height, gap + 3, C_WHITE);
    frame.drawFastHLine(x + 1, y - height, gap + 3, C_WHITE);
  }
}

static void drawSmallFielder(float x, float y, float scale,
                             uint16_t shirt, bool crouching) {
  const int16_t headRadius = (int16_t)roundf(4.0f * scale);
  const int16_t bodyWidth = (int16_t)roundf(9.0f * scale);
  const int16_t bodyHeight = (int16_t)roundf(11.0f * scale);
  const int16_t ix = (int16_t)roundf(x);
  const int16_t iy = (int16_t)roundf(y);

  frame.fillRoundRect(ix - bodyWidth, iy + 9, bodyWidth * 2, 3, 2, C_SHADOW);
  frame.fillCircle(ix, iy - bodyHeight, headRadius + 1, C_INK);
  frame.fillCircle(ix, iy - bodyHeight, headRadius, C_SKIN);
  frame.fillRoundRect(ix - bodyWidth / 2, iy - bodyHeight + headRadius,
                      bodyWidth, bodyHeight, 2, C_INK);
  frame.fillRoundRect(ix - bodyWidth / 2 + 1, iy - bodyHeight + headRadius + 1,
                      bodyWidth - 2, bodyHeight - 2, 2, shirt);

  if (crouching) {
    outlinedLine(x - 2.0f * scale, y + 1.0f * scale,
                 x - 7.0f * scale, y + 6.0f * scale, 2, C_WHITE);
    outlinedLine(x + 2.0f * scale, y + 1.0f * scale,
                 x + 7.0f * scale, y + 6.0f * scale, 2, C_WHITE);
  } else {
    outlinedLine(x - 2.0f * scale, y,
                 x - 3.0f * scale, y + 8.0f * scale, 2, C_WHITE);
    outlinedLine(x + 2.0f * scale, y,
                 x + 3.0f * scale, y + 8.0f * scale, 2, C_WHITE);
  }
}

static float runupAmount() {
  if (cricket.ball.phase == BallPhase::RUNUP) {
    return clampFloat(cricket.ball.age / RUNUP_SECONDS, 0.0f, 1.0f);
  }
  if (cricket.ball.phase == BallPhase::TRAVELLING ||
      cricket.ball.phase == BallPhase::HIT ||
      cricket.ball.phase == BallPhase::RESOLVED) {
    return 1.0f;
  }
  return 0.0f;
}

static void bowlerHandAtRelease(float &x, float &y) {
  // Matches the raised bowling hand at the end of the run-up animation.
  x = 91.0f;
  y = 123.0f;
}

static void drawBowler() {
  const float run = runupAmount();
  const float stride = sinf(run * 3.1415926f);
  const float x = 86.0f + sinf(run * 6.2831853f) * 1.5f;
  const float y = 126.0f + run * 20.0f;
  const float scale = 0.72f;

  frame.fillRoundRect((int16_t)roundf(x - 8.0f * scale),
                      (int16_t)roundf(y + 8.0f * scale),
                      (int16_t)roundf(16.0f * scale), 3, 2, C_SHADOW);

  frame.fillCircle((int16_t)roundf(x),
                   (int16_t)roundf(y - 18.0f * scale),
                   (int16_t)roundf(5.0f * scale) + 1, C_INK);
  frame.fillCircle((int16_t)roundf(x),
                   (int16_t)roundf(y - 18.0f * scale),
                   (int16_t)roundf(5.0f * scale), C_SKIN);
  frame.fillRect((int16_t)roundf(x - 5.0f * scale),
                 (int16_t)roundf(y - 23.0f * scale),
                 (int16_t)roundf(10.0f * scale), 3, C_RED);

  frame.fillRoundRect((int16_t)roundf(x - 6.0f * scale),
                      (int16_t)roundf(y - 12.0f * scale),
                      (int16_t)roundf(12.0f * scale),
                      (int16_t)roundf(16.0f * scale), 3, C_INK);
  frame.fillRoundRect((int16_t)roundf(x - 5.0f * scale),
                      (int16_t)roundf(y - 11.0f * scale),
                      (int16_t)roundf(10.0f * scale),
                      (int16_t)roundf(14.0f * scale), 3, C_RED);

  outlinedLine(x - 2.0f, y + 2.0f,
               x - 5.0f - stride * 4.0f, y + 12.0f, 2, C_WHITE);
  outlinedLine(x + 2.0f, y + 2.0f,
               x + 6.0f + stride * 3.0f, y + 11.0f, 2, C_WHITE);

  const float bowlingArmX = lerpFloat(x + 7.0f, x + 5.0f, run);
  const float bowlingArmY = lerpFloat(y - 2.0f, y - 23.0f, run);
  outlinedLine(x + 3.0f, y - 8.0f, bowlingArmX, bowlingArmY, 3, C_SKIN);

  const float guideArmX = lerpFloat(x - 8.0f, x - 5.0f, run);
  const float guideArmY = lerpFloat(y - 2.0f, y - 10.0f, run);
  outlinedLine(x - 3.0f, y - 8.0f, guideArmX, guideArmY, 3, C_SKIN);

  if (cricket.ball.phase == BallPhase::RUNUP && run < 0.94f) {
    frame.fillCircle((int16_t)roundf(bowlingArmX),
                     (int16_t)roundf(bowlingArmY), 2, C_RED);
    frame.drawPixel((int16_t)roundf(bowlingArmX) - 1,
                    (int16_t)roundf(bowlingArmY) - 1, C_WHITE);
  }
}

static BatGeometry batGeometry() {
  const float progress = cricket.swinging ? cricket.swingProgress : 0.0f;
  const float eased = progress * progress * (3.0f - 2.0f * progress);
  const float angle = lerpFloat(-1.25f, 1.05f, eased);

  BatGeometry geometry;
  geometry.handleX = cricket.batterX + 5.0f;
  geometry.handleY = BATTER_BASE_Y - 31.0f - cricket.batterDepth * 4.0f;
  geometry.bladeX = geometry.handleX + cosf(angle) * 31.0f;
  geometry.bladeY = geometry.handleY + sinf(angle) * 31.0f;
  return geometry;
}

static void drawBatter() {
  const float x = cricket.batterX;
  const float y = BATTER_BASE_Y + cricket.batterDepth * 5.0f;
  const BatGeometry bat = batGeometry();

  frame.fillRoundRect((int16_t)roundf(x - 14.0f),
                      (int16_t)roundf(y + 13.0f), 28, 5, 2, C_SHADOW);

  drawWickets(86.0f, NEAR_CREASE_Y + 8.0f, 1.0f,
              cricket.ball.outcome == OutcomeType::WICKET &&
                  cricket.feedbackLife > 0.0f);

  // Legs and protective pads.
  outlinedLine(x - 3.0f, y - 2.0f, x - 7.0f, y + 14.0f, 4, C_GREEN_DARK);
  outlinedLine(x + 3.0f, y - 2.0f, x + 7.0f, y + 14.0f, 4, C_GREEN_DARK);
  frame.fillRoundRect((int16_t)roundf(x - 11.0f),
                      (int16_t)roundf(y + 2.0f), 7, 16, 2, C_TEXT);
  frame.fillRoundRect((int16_t)roundf(x + 4.0f),
                      (int16_t)roundf(y + 2.0f), 7, 16, 2, C_TEXT);
  frame.drawFastHLine((int16_t)roundf(x - 10.0f),
                      (int16_t)roundf(y + 8.0f), 5, C_BORDER);
  frame.drawFastHLine((int16_t)roundf(x + 5.0f),
                      (int16_t)roundf(y + 8.0f), 5, C_BORDER);

  // Body and jersey trim.
  frame.fillRoundRect((int16_t)roundf(x - 8.0f),
                      (int16_t)roundf(y - 24.0f), 16, 24, 5, C_INK);
  frame.fillRoundRect((int16_t)roundf(x - 7.0f),
                      (int16_t)roundf(y - 23.0f), 14, 22, 4, C_GREEN);
  frame.drawFastHLine((int16_t)roundf(x - 6.0f),
                      (int16_t)roundf(y - 6.0f), 12, C_GOLD);

  // Head, helmet and face.
  frame.fillCircle((int16_t)roundf(x), (int16_t)roundf(y - 34.0f), 9, C_INK);
  frame.fillCircle((int16_t)roundf(x), (int16_t)roundf(y - 34.0f), 7, C_SKIN);
  frame.fillCircle((int16_t)roundf(x - 1.0f),
                   (int16_t)roundf(y - 37.0f), 7, C_GREEN_DARK);
  frame.fillRect((int16_t)roundf(x - 7.0f),
                 (int16_t)roundf(y - 36.0f), 14, 3, C_GREEN_DARK);
  frame.drawFastHLine((int16_t)roundf(x + 2.0f),
                      (int16_t)roundf(y - 34.0f), 7, C_INK);
  frame.drawPixel((int16_t)roundf(x + 3.0f),
                  (int16_t)roundf(y - 35.0f), C_WHITE);

  // Arms and gloves follow the bat handle.
  outlinedLine(x - 4.0f, y - 17.0f,
               bat.handleX - 4.0f, bat.handleY + 2.0f, 4, C_GREEN);
  outlinedLine(x + 4.0f, y - 16.0f,
               bat.handleX + 2.0f, bat.handleY, 4, C_GREEN);
  frame.fillCircle((int16_t)roundf(bat.handleX - 3.0f),
                   (int16_t)roundf(bat.handleY + 1.0f), 3, C_WHITE);
  frame.fillCircle((int16_t)roundf(bat.handleX + 2.0f),
                   (int16_t)roundf(bat.handleY), 3, C_WHITE);

  // Bat is outlined and has a wider blade tip.
  outlinedLine(bat.handleX, bat.handleY, bat.bladeX, bat.bladeY, 4, C_BAT);
  frame.fillRoundRect((int16_t)roundf(bat.bladeX - 4.0f),
                      (int16_t)roundf(bat.bladeY - 7.0f), 9, 15, 3, C_INK);
  frame.fillRoundRect((int16_t)roundf(bat.bladeX - 3.0f),
                      (int16_t)roundf(bat.bladeY - 6.0f), 7, 13, 2, C_BAT);
  frame.drawFastVLine((int16_t)roundf(bat.bladeX),
                      (int16_t)roundf(bat.bladeY - 5.0f), 11, C_BROWN);
}

static void drawWicketKeeper() {
  const float x = 111.0f;
  const float y = 288.0f;

  frame.fillRoundRect(98, 299, 27, 4, 2, C_SHADOW);
  frame.fillCircle((int16_t)x, (int16_t)y - 25, 7, C_INK);
  frame.fillCircle((int16_t)x, (int16_t)y - 25, 5, C_SKIN);
  frame.fillRect((int16_t)x - 6, (int16_t)y - 31, 12, 3, C_BLUE_DARK);
  frame.fillRoundRect((int16_t)x - 7, (int16_t)y - 19, 14, 17, 4, C_INK);
  frame.fillRoundRect((int16_t)x - 6, (int16_t)y - 18, 12, 15, 3, C_BLUE);

  outlinedLine(x - 4, y - 4, x - 12, y + 7, 4, C_WHITE);
  outlinedLine(x + 4, y - 4, x + 12, y + 7, 4, C_WHITE);
  outlinedLine(x - 5, y - 13, x - 13, y - 4, 3, C_BLUE);
  outlinedLine(x + 5, y - 13, x + 13, y - 4, 3, C_BLUE);
  frame.fillCircle((int16_t)x - 13, (int16_t)y - 4, 3, C_WHITE);
  frame.fillCircle((int16_t)x + 13, (int16_t)y - 4, 3, C_WHITE);
}

static void drawUmpire() {
  const int16_t x = 103;
  const int16_t y = 132;
  frame.fillCircle(x, y - 14, 4, C_SKIN);
  frame.fillRect(x - 6, y - 19, 12, 3, C_WHITE);
  frame.fillRoundRect(x - 5, y - 9, 10, 13, 2, C_INK);
  frame.drawFastVLine(x - 2, y + 3, 8, C_BLACK);
  frame.drawFastVLine(x + 2, y + 3, 8, C_BLACK);
}

static float dayNightAmount() {
  float activeProgress = 0.0f;
  if (cricket.ball.phase == BallPhase::RUNUP ||
      cricket.ball.phase == BallPhase::TRAVELLING ||
      cricket.ball.phase == BallPhase::HIT ||
      cricket.ball.phase == BallPhase::RESOLVED) {
    activeProgress = 0.5f;
  }
  const float inningsProgress =
      (cricket.ballsFaced + activeProgress) / (float)BALLS_PER_INNINGS;
  return clampFloat((inningsProgress - 0.35f) / 0.65f, 0.0f, 1.0f);
}

static void drawSkyAndField(uint32_t now, float nightAmount) {
  for (int16_t y = 0; y < 112; y += 8) {
    const float vertical = y / 111.0f;
    const uint16_t day =
        blend565(C_SKY, C_SKY_HIGH, (uint8_t)(vertical * 210.0f));
    const uint16_t night =
        blend565(C_NIGHT, C_DUSK, (uint8_t)(vertical * 140.0f));
    frame.fillRect(0, y, SCREEN_W, 8,
                   blend565(day, night, (uint8_t)(nightAmount * 255.0f)));
  }

  const float sunTravel = clampFloat(nightAmount / 0.68f, 0.0f, 1.0f);
  const float sunVisibility =
      1.0f - clampFloat((nightAmount - 0.32f) / 0.35f, 0.0f, 1.0f);
  const int16_t sunX = (int16_t)roundf(145.0f - sunTravel * 72.0f);
  const int16_t sunY =
      (int16_t)roundf(43.0f + sunTravel * sunTravel * 86.0f);
  if (sunVisibility > 0.02f) {
    frame.fillCircle(sunX, sunY, 13,
                     blend565(C_SKY, C_GOLD,
                              (uint8_t)(sunVisibility * 255.0f)));
    frame.fillCircle(sunX - 4, sunY - 4, 8,
                     blend565(C_SKY, C565(255, 239, 164),
                              (uint8_t)(sunVisibility * 255.0f)));
  }

  const float moonRise =
      clampFloat((nightAmount - 0.22f) / 0.78f, 0.0f, 1.0f);
  const float moonVisibility =
      clampFloat((nightAmount - 0.22f) / 0.34f, 0.0f, 1.0f);
  const int16_t moonX = (int16_t)roundf(177.0f - moonRise * 43.0f);
  const int16_t moonY =
      (int16_t)roundf(118.0f - sinf(moonRise * 1.5707963f) * 75.0f);
  if (moonVisibility > 0.02f) {
    const uint16_t moon =
        blend565(C_DUSK, C_WHITE, (uint8_t)(moonVisibility * 255.0f));
    const uint16_t cutout =
        blend565(C_DUSK, C_NIGHT, (uint8_t)(moonVisibility * 255.0f));
    frame.fillCircle(moonX, moonY, 10, moon);
    frame.fillCircle(moonX - 5, moonY - 4, 9, cutout);
  }

  const uint8_t starVisibility =
      (uint8_t)(clampFloat((nightAmount - 0.34f) / 0.42f, 0.0f, 1.0f) *
                255.0f);
  if (starVisibility > 0) {
    for (uint8_t index = 0; index < 16; ++index) {
      const int16_t x = (index * 47 + 9) % SCREEN_W;
      const int16_t y = (index * 29 + 17) % 89;
      if (((now / 260U + index) & 3U) != 0) {
        frame.drawPixel(x, y, blend565(C_NIGHT, C_WHITE, starVisibility));
      }
    }
  }

  const int16_t drift = (int16_t)((now / 85U) % 220U) - 25;
  for (uint8_t index = 0; index < 3; ++index) {
    const int16_t x = (drift + index * 76) % 220 - 24;
    const int16_t y = 52 + index * 17;
    const uint16_t shade =
        blend565(C_CLOUD_SHADE, C_DUSK,
                 (uint8_t)(nightAmount * 180.0f));
    const uint16_t cloud =
        blend565(C_CLOUD, C_DUSK, (uint8_t)(nightAmount * 150.0f));
    frame.fillCircle(x, y, 7, shade);
    frame.fillCircle(x + 8, y - 3, 9, cloud);
    frame.fillCircle(x + 17, y, 7, cloud);
    frame.fillRect(x, y, 18, 6, cloud);
  }

  // Stadium, crowd and boundary boards sit behind the actual playing field.
  frame.fillRect(0, 99, SCREEN_W, 18, C_STAND);
  frame.fillRect(0, 116, SCREEN_W, 18, C_STAND_DARK);
  for (uint8_t index = 0; index < 23; ++index) {
    const int16_t x = 3 + index * 8;
    const int16_t y = 105 + (index & 1U) * 5;
    const uint16_t colour =
        index % 3 == 0 ? C_GOLD : (index % 3 == 1 ? C_CYAN : C_TEXT);
    frame.fillCircle(x, y, 2, colour);
    frame.drawFastVLine(x, y + 2, 4, colour);
  }
  frame.fillRect(0, 130, SCREEN_W, 5, C_WHITE);
  frame.fillRect(0, 135, SCREEN_W, 4, C_BLUE_DARK);

  frame.fillRect(0, 139, SCREEN_W, SCREEN_H - 139, C_GRASS);
  frame.fillRect(0, 235, SCREEN_W, SCREEN_H - 235, C_GRASS_DARK);

  // Proper perspective pitch: narrow at the bowler, wide near the batter.
  frame.fillTriangle(73, 132, 99, 132, 42, SCREEN_H, C_PITCH);
  frame.fillTriangle(99, 132, 130, SCREEN_H, 42, SCREEN_H, C_PITCH);
  frame.drawLine(73, 132, 42, SCREEN_H, C_PITCH_DARK);
  frame.drawLine(99, 132, 130, SCREEN_H, C_PITCH_DARK);

  // Mowing stripes reinforce the field perspective without clutter.
  frame.drawLine(20, 149, 0, 222, blend565(C_GRASS, C_WHITE, 24));
  frame.drawLine(152, 149, 171, 222, blend565(C_GRASS, C_WHITE, 24));
  frame.drawLine(32, 178, 8, 286, blend565(C_GRASS_DARK, C_WHITE, 20));
  frame.drawLine(140, 178, 164, 286, blend565(C_GRASS_DARK, C_WHITE, 20));

  // Far and near creases, scaled by perspective.
  frame.drawFastHLine(70, (int16_t)FAR_CREASE_Y, 32, C_PITCH_LINE);
  frame.drawFastHLine(67, (int16_t)FAR_CREASE_Y + 6, 38, C_PITCH_LINE);
  frame.drawFastHLine(50, (int16_t)NEAR_CREASE_Y, 72, C_PITCH_LINE);
  frame.drawFastHLine(46, (int16_t)NEAR_CREASE_Y + 12, 80, C_PITCH_LINE);

  drawWickets(86.0f, FAR_CREASE_Y + 4.0f, 0.55f);
}

static void drawFieldPlayers() {
  drawSmallFielder(25, 170, 0.62f, C_BLUE, false);
  drawSmallFielder(148, 176, 0.62f, C_BLUE, true);
  drawSmallFielder(25, 238, 0.78f, C_PURPLE, true);
  drawSmallFielder(149, 239, 0.78f, C_PURPLE, false);
  drawSmallFielder(67, 148, 0.52f, C_GREEN_DARK, false); // non-striker
  drawUmpire();
}

static void drawScoreHud() {
  frame.fillRoundRect(5, 5, 162, 32, 7, C_PANEL);
  frame.drawRoundRect(5, 5, 162, 32, 7, C_BORDER);

  char buffer[20];
  textLeft("RUNS", 11, 9, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)cricket.runs);
  textLeft(buffer, 11, 20, 1, C_GOLD);

  textLeft("WKTS", 69, 9, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%u", cricket.wickets);
  textLeft(buffer, 69, 20, 1, C_TEXT);

  textLeft("BALL", 123, 9, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%u/%u", cricket.ballsFaced,
           BALLS_PER_INNINGS);
  textLeft(buffer, 123, 20, 1, C_CYAN);
}

static void drawDeliveryBadge() {
  if (cricket.ball.phase == BallPhase::WAITING) {
    return;
  }
  frame.fillRoundRect(57, 42, 58, 17, 5, C_PANEL);
  frame.drawRoundRect(57, 42, 58, 17, 5, C_BORDER);
  centered(deliveryName(cricket.ball.type), 47, 1, C_TEXT);
}

static void drawParticles() {
  for (uint8_t index = 0; index < MAX_PARTICLES; ++index) {
    const Particle &particle = cricket.particles[index];
    if (!particle.active) {
      continue;
    }
    frame.fillRect((int16_t)roundf(particle.x) - 1,
                   (int16_t)roundf(particle.y) - 1, 3, 3,
                   particle.colour);
  }
}

static void drawBall() {
  if (cricket.ball.phase == BallPhase::WAITING ||
      cricket.ball.phase == BallPhase::RUNUP) {
    return;
  }

  const float screenY = cricket.ball.y - cricket.ball.z;
  const int16_t shadowWidth =
      (int16_t)clampFloat(5.0f - cricket.ball.z * 0.05f, 2.0f, 5.0f);
  frame.fillRoundRect((int16_t)roundf(cricket.ball.x) - shadowWidth,
                      (int16_t)roundf(cricket.ball.y) + 2,
                      shadowWidth * 2, 3, 1, C_SHADOW);
  frame.fillCircle((int16_t)roundf(cricket.ball.x),
                   (int16_t)roundf(screenY), 3, C_RED);
  frame.drawPixel((int16_t)roundf(cricket.ball.x) - 1,
                  (int16_t)roundf(screenY) - 1, C_WHITE);
}

static void drawFeedback() {
  if (cricket.feedbackLife <= 0.0f) {
    return;
  }
  frame.fillRoundRect(35, 292, 102, 21, 6, C_PANEL);
  frame.drawRoundRect(35, 292, 102, 21, 6, cricket.feedbackColour);
  centered(cricket.feedback, 299, 1, cricket.feedbackColour);
}

static void loadProgress() {
  pocketgame::PocketStorage &storage = pocketSystem->storage();
  cricket.bestScore = storage.getUInt32("best", 0);
  cricket.bestFours = storage.getUInt8("fours", 0);
  cricket.bestSixes = storage.getUInt8("sixes", 0);
}

static void saveProgress() {
  if (cricket.progressSaved) {
    return;
  }
  cricket.progressSaved = true;

  pocketgame::PocketStorage &storage = pocketSystem->storage();
  cricket.newBest = cricket.runs > cricket.bestScore;
  if (cricket.newBest) {
    cricket.bestScore = cricket.runs;
    storage.putUInt32("best", cricket.bestScore);
  }
  if (cricket.fours > cricket.bestFours) {
    cricket.bestFours = cricket.fours;
    storage.putUInt8("fours", cricket.bestFours);
  }
  if (cricket.sixes > cricket.bestSixes) {
    cricket.bestSixes = cricket.sixes;
    storage.putUInt8("sixes", cricket.bestSixes);
  }
}

static void resetMatch(uint32_t now) {
  cricket.runs = 0;
  cricket.wickets = 0;
  cricket.ballsFaced = 0;
  cricket.fours = 0;
  cricket.sixes = 0;

  cricket.batterX = 86.0f;
  cricket.batterTargetX = 86.0f;
  cricket.batterDepth = 0.0f;
  cricket.batterDepthTarget = 0.0f;
  cricket.swingProgress = 0.0f;
  cricket.swinging = false;
  cricket.shotResolved = false;

  cricket.ball = BallState{};
  cricket.nextBallAt = now + 650U;
  cricket.resultUntil = 0;
  cricket.feedback[0] = '\0';
  cricket.feedbackLife = 0.0f;
  cricket.endPending = false;
  cricket.progressSaved = false;
  cricket.newBest = false;

  clearParticles();
  cricket.screens.goTo(GameScreen::PLAYING, now);
  cricket.lastFrameAt = now;
}

static void finishMatch(uint32_t now) {
  saveProgress();
  cricket.resultMenu.reset(3, 0);
  cricket.screens.goTo(GameScreen::RESULT, now);
}

static void configureDelivery() {
  BallState &ball = cricket.ball;
  ball = BallState{};
  ball.phase = BallPhase::RUNUP;
  ball.type = (DeliveryType)randomGenerator.rangeInt(0, 5);
  ball.targetX = 86.0f + randomGenerator.range(-15.0f, 15.0f);
  ball.bounced = false;
  ball.counted = false;
  ball.age = 0.0f;

  // The bat sweeps in front of a right-handed batter, so the automatic
  // footwork places the body slightly to the left of the projected ball line.
  cricket.batterTargetX =
      clampFloat(ball.targetX - 11.0f, 63.0f, 91.0f);
  cricket.batterDepthTarget = 0.0f;
  cricket.shotResolved = false;
  cricket.swinging = false;
  cricket.swingProgress = 0.0f;

  switch (ball.type) {
  case DeliveryType::FAST:
    ball.vy = 137.0f;
    ball.bounceY = 200.0f;
    ball.bounceLift = 25.0f;
    break;
  case DeliveryType::SLOW:
    ball.vy = 91.0f;
    ball.bounceY = 199.0f;
    ball.bounceLift = 22.0f;
    break;
  case DeliveryType::SPIN:
    ball.vy = 103.0f;
    ball.bounceY = 197.0f;
    ball.bounceLift = 25.0f;
    ball.driftAcceleration = randomGenerator.range(-28.0f, 28.0f);
    break;
  case DeliveryType::YORKER:
    ball.vy = 123.0f;
    ball.bounceY = 241.0f;
    ball.bounceLift = 10.0f;
    cricket.batterDepthTarget = 1.0f;
    break;
  case DeliveryType::BOUNCER:
    ball.vy = 119.0f;
    ball.bounceY = 178.0f;
    ball.bounceLift = 48.0f;
    cricket.batterDepthTarget = -1.0f;
    break;
  case DeliveryType::SWINGER:
  default:
    ball.vy = 111.0f;
    ball.bounceY = 201.0f;
    ball.bounceLift = 24.0f;
    ball.driftAcceleration = randomGenerator.range(-13.0f, 13.0f);
    break;
  }

  setFeedback(deliveryName(ball.type), C_CYAN, 0.50f);
}

static void releaseDelivery() {
  BallState &ball = cricket.ball;
  float handX = 0.0f;
  float handY = 0.0f;
  bowlerHandAtRelease(handX, handY);

  ball.phase = BallPhase::TRAVELLING;
  ball.x = handX;
  ball.y = handY;
  ball.z = 10.0f;
  ball.vz = 0.0f;

  const float travelTime =
      (NEAR_CREASE_Y - 9.0f - ball.y) / ball.vy;
  ball.vx = (ball.targetX - ball.x) / clampFloat(travelTime, 0.50f, 1.80f);
}

static void startSwing() {
  if (cricket.ball.phase != BallPhase::TRAVELLING ||
      cricket.shotResolved || cricket.swinging) {
    return;
  }
  cricket.swinging = true;
  cricket.swingProgress = 0.0f;
}

static float pointSegmentDistance(float px, float py, float x1, float y1,
                                  float x2, float y2) {
  const float dx = x2 - x1;
  const float dy = y2 - y1;
  const float lengthSquared = dx * dx + dy * dy;
  if (lengthSquared <= 0.001f) {
    const float ex = px - x1;
    const float ey = py - y1;
    return sqrtf(ex * ex + ey * ey);
  }

  float amount = ((px - x1) * dx + (py - y1) * dy) / lengthSquared;
  amount = clampFloat(amount, 0.0f, 1.0f);
  const float nearestX = x1 + dx * amount;
  const float nearestY = y1 + dy * amount;
  const float ex = px - nearestX;
  const float ey = py - nearestY;
  return sqrtf(ex * ex + ey * ey);
}

static void resolveBall(OutcomeType outcome, uint8_t runsAwarded,
                        const char *label, uint16_t colour, uint32_t now) {
  BallState &ball = cricket.ball;
  ball.outcome = outcome;
  cricket.shotResolved = true;
  cricket.resultUntil = now + 820U;
  setFeedback(label, colour, 1.0f);

  if (!ball.counted) {
    ball.counted = true;
    ++cricket.ballsFaced;
  }

  if (outcome == OutcomeType::WICKET) {
    ++cricket.wickets;
  } else {
    cricket.runs += runsAwarded;
    if (outcome == OutcomeType::FOUR) {
      ++cricket.fours;
    } else if (outcome == OutcomeType::SIX) {
      ++cricket.sixes;
    }
  }

  cricket.endPending =
      cricket.wickets >= MAX_WICKETS ||
      cricket.ballsFaced >= BALLS_PER_INNINGS;

  if (!cricket.endPending) {
    cricket.nextBallAt = now + 900U;
  }
}

static void tryBatContact(uint32_t now) {
  if (cricket.shotResolved || !cricket.swinging ||
      cricket.ball.phase != BallPhase::TRAVELLING) {
    return;
  }

  if (cricket.swingProgress < 0.18f || cricket.swingProgress > 0.84f) {
    return;
  }

  const BatGeometry bat = batGeometry();
  const float ballY = cricket.ball.y - cricket.ball.z;
  const float distance =
      pointSegmentDistance(cricket.ball.x, ballY,
                           bat.handleX, bat.handleY,
                           bat.bladeX, bat.bladeY);
  if (distance > 8.5f) {
    return;
  }

  const float distanceQuality =
      1.0f - clampFloat(distance / 8.5f, 0.0f, 1.0f);
  const float timingQuality =
      1.0f - clampFloat(fabsf(cricket.swingProgress - 0.50f) / 0.34f,
                        0.0f, 1.0f);
  const float quality = distanceQuality * 0.62f + timingQuality * 0.38f;

  cricket.ball.phase = BallPhase::HIT;
  cricket.ball.vy = -(94.0f + quality * 62.0f);
  cricket.ball.vx =
      (cricket.ball.x - cricket.batterX) * 1.25f +
      randomGenerator.range(-24.0f, 24.0f);
  cricket.ball.vz = 88.0f + quality * 72.0f;
  cricket.ball.driftAcceleration = 0.0f;

  const bool lofted = cricket.ball.type == DeliveryType::BOUNCER ||
                       cricket.ball.z > 7.0f ||
                       randomGenerator.rangeInt(0, 2) == 0;

  if (quality > 0.86f) {
    resolveBall(lofted ? OutcomeType::SIX : OutcomeType::FOUR,
                lofted ? 6 : 4,
                lofted ? "SIX!" : "FOUR!",
                C_GOLD, now);
    spawnBurst(cricket.ball.x, ballY, 20, C_GOLD, C_CYAN);
  } else if (quality > 0.64f) {
    resolveBall(OutcomeType::FOUR, 4, "BOUNDARY!", C_GREEN, now);
    spawnBurst(cricket.ball.x, ballY, 14, C_GREEN, C_CYAN);
  } else if (quality > 0.42f) {
    resolveBall(OutcomeType::DOUBLE, 2, "TWO RUNS", C_CYAN, now);
    spawnBurst(cricket.ball.x, ballY, 9, C_CYAN, C_WHITE);
  } else {
    resolveBall(OutcomeType::SINGLE, 1, "SINGLE", C_TEXT, now);
    spawnBurst(cricket.ball.x, ballY, 6, C_TEXT, C_CYAN);
  }
}

static void updateGameplay(float deltaSeconds, uint32_t now) {
  cricket.batterX =
      lerpFloat(cricket.batterX, cricket.batterTargetX,
                1.0f - expf(-5.0f * deltaSeconds));
  cricket.batterDepth =
      lerpFloat(cricket.batterDepth, cricket.batterDepthTarget,
                1.0f - expf(-5.0f * deltaSeconds));

  if (cricket.ball.phase == BallPhase::WAITING &&
      now >= cricket.nextBallAt && !cricket.endPending) {
    configureDelivery();
  }

  if (cricket.swinging) {
    cricket.swingProgress += deltaSeconds / SWING_SECONDS;
    if (cricket.swingProgress >= 1.0f) {
      cricket.swingProgress = 0.0f;
      cricket.swinging = false;
    }
  }

  BallState &ball = cricket.ball;
  if (ball.phase == BallPhase::RUNUP) {
    ball.age += deltaSeconds;
    if (ball.age >= RUNUP_SECONDS) {
      releaseDelivery();
    }
  } else if (ball.phase == BallPhase::TRAVELLING) {
    ball.y += ball.vy * deltaSeconds;
    ball.x += ball.vx * deltaSeconds;

    if (!ball.bounced) {
      const float descent =
          clampFloat((ball.y - FAR_CREASE_Y) /
                         clampFloat(ball.bounceY - FAR_CREASE_Y, 1.0f, 200.0f),
                     0.0f, 1.0f);
      ball.z = lerpFloat(10.0f, 0.0f, descent);
    }

    if (!ball.bounced && ball.y >= ball.bounceY) {
      ball.bounced = true;
      ball.z = 0.0f;
      ball.vz = ball.bounceLift;
      if (ball.type == DeliveryType::SLOW) {
        ball.vy *= 0.91f;
      }
      spawnBurst(ball.x, ball.y, 5, C_PITCH_LINE, C_PITCH_DARK);
    }

    if (ball.bounced) {
      ball.vx += ball.driftAcceleration * deltaSeconds;
      ball.z += ball.vz * deltaSeconds;
      ball.vz -= 145.0f * deltaSeconds;
      if (ball.z < 0.0f) {
        ball.z = 0.0f;
        ball.vz = 0.0f;
      }
    }

    tryBatContact(now);

    if (!cricket.shotResolved && ball.y >= NEAR_CREASE_Y + 4.0f) {
      const bool strikesStumps =
          fabsf(ball.x - 86.0f) < 10.5f && ball.z < 9.0f;
      if (strikesStumps) {
        resolveBall(OutcomeType::WICKET, 0, "BOWLED!", C_ORANGE, now);
        spawnBurst(86.0f, NEAR_CREASE_Y - 7.0f, 14, C_ORANGE, C_WHITE);
      } else {
        resolveBall(OutcomeType::DOT, 0, "DOT BALL", C_MUTED, now);
      }
      ball.phase = BallPhase::RESOLVED;
    }
  } else if (ball.phase == BallPhase::HIT) {
    ball.y += ball.vy * deltaSeconds;
    ball.x += ball.vx * deltaSeconds;
    ball.z += ball.vz * deltaSeconds;
    ball.vz -= 178.0f * deltaSeconds;
    if (ball.z < 0.0f) {
      ball.z = 0.0f;
    }

    if (now >= cricket.resultUntil || ball.y < 68.0f ||
        ball.x < -20.0f || ball.x > SCREEN_W + 20.0f) {
      ball.phase = BallPhase::RESOLVED;
    }
  }

  if (ball.phase == BallPhase::RESOLVED && now >= cricket.resultUntil) {
    if (cricket.endPending) {
      finishMatch(now);
      return;
    }
    ball.phase = BallPhase::WAITING;
  }

  if (cricket.feedbackLife > 0.0f) {
    cricket.feedbackLife -= deltaSeconds;
    if (cricket.feedbackLife < 0.0f) {
      cricket.feedbackLife = 0.0f;
    }
  }

  updateParticles(deltaSeconds);
}

static void handleInput(uint32_t now) {
  const pocketgame::ControlEvents &controls = pocketSystem->controls().events();

  if (cricket.screens.is(GameScreen::MENU)) {
    const MenuAction action = cricket.titleMenu.update(controls);
    if (action == MenuAction::SELECTED) {
      if (cricket.titleMenu.selected() == 0) {
        resetMatch(now);
      } else if (cricket.titleMenu.selected() == 1) {
        cricket.howToPage = 0;
        cricket.screens.goTo(GameScreen::HOW_TO, now);
      } else {
        pocketSystem->requestLauncher();
      }
    }
    return;
  }

  if (cricket.screens.is(GameScreen::HOW_TO)) {
    if (controls.cycle) {
      cricket.howToPage = (cricket.howToPage + 1) % 2;
      cricket.screens.refresh(now);
    }
    if (controls.select) {
      cricket.screens.goTo(GameScreen::MENU, now);
    }
    return;
  }

  if (cricket.screens.is(GameScreen::PLAYING)) {
    if (controls.select) {
      cricket.pauseMenu.reset(3, 0);
      cricket.screens.goTo(GameScreen::PAUSED, now);
      return;
    }
    if (controls.cycle) {
      startSwing();
    }
    return;
  }

  if (cricket.screens.is(GameScreen::PAUSED)) {
    const MenuAction action = cricket.pauseMenu.update(controls);
    if (action == MenuAction::SELECTED) {
      if (cricket.pauseMenu.selected() == 0) {
        cricket.screens.goTo(GameScreen::PLAYING, now);
        cricket.lastFrameAt = now;
      } else if (cricket.pauseMenu.selected() == 1) {
        resetMatch(now);
      } else {
        pocketSystem->requestLauncher();
      }
    }
    return;
  }

  if (cricket.screens.is(GameScreen::RESULT)) {
    const MenuAction action = cricket.resultMenu.update(controls);
    if (action == MenuAction::SELECTED) {
      if (cricket.resultMenu.selected() == 0) {
        resetMatch(now);
      } else if (cricket.resultMenu.selected() == 1) {
        cricket.titleMenu.reset(3, 0);
        cricket.screens.goTo(GameScreen::MENU, now);
      } else {
        pocketSystem->requestLauncher();
      }
    }
  }
}

static void drawMenuOption(const char *label, int16_t y, bool selected) {
  frame.fillRoundRect(26, y, 120, 27, 7, selected ? C_PANEL_2 : C_PANEL);
  frame.drawRoundRect(26, y, 120, 27, 7, selected ? C_GOLD : C_BORDER);
  centered(label, y + 9, 1, selected ? C_TEXT : C_MUTED);
}

static void drawTitleScene(uint32_t now) {
  drawSkyAndField(now, 0.0f);
  drawFieldPlayers();

  // Keep the menu illustration independent from the last match state.
  const BallState savedBall = cricket.ball;
  const float savedBatterX = cricket.batterX;
  const float savedDepth = cricket.batterDepth;
  const float savedSwing = cricket.swingProgress;
  const bool savedSwinging = cricket.swinging;

  cricket.ball.phase = BallPhase::WAITING;
  cricket.batterX = 86.0f;
  cricket.batterDepth = 0.0f;
  cricket.swingProgress = 0.0f;
  cricket.swinging = false;

  drawBowler();
  drawBatter();
  drawWicketKeeper();

  cricket.ball = savedBall;
  cricket.batterX = savedBatterX;
  cricket.batterDepth = savedDepth;
  cricket.swingProgress = savedSwing;
  cricket.swinging = savedSwinging;

  frame.fillRoundRect(9, 11, 154, 48, 10, C_PANEL);
  frame.drawRoundRect(9, 11, 154, 48, 10, C_BORDER);
  centered("POCKET", 19, 2, C_TEXT);
  centered("CRICKET", 40, 2, C_GOLD);
}

static void drawTitleMenu(uint32_t now) {
  drawTitleScene(now);
  frame.fillRoundRect(11, 180, 150, 129, 11, C_PANEL);
  frame.drawRoundRect(11, 180, 150, 129, 11, C_BORDER);
  drawMenuOption("PLAY MATCH", 188, cricket.titleMenu.selected() == 0);
  drawMenuOption("HOW TO PLAY", 222, cricket.titleMenu.selected() == 1);
  drawMenuOption("POCKETGAME", 256, cricket.titleMenu.selected() == 2);

  char best[42];
  snprintf(best, sizeof(best), "BEST %lu  /  4s %u  6s %u",
           (unsigned long)cricket.bestScore, cricket.bestFours,
           cricket.bestSixes);
  centered(best, 292, 1, C_MUTED);
}

static void drawHowTo(uint32_t now) {
  drawSkyAndField(now, 0.0f);
  frame.fillRoundRect(9, 12, 154, 296, 12, C_PANEL);
  frame.drawRoundRect(9, 12, 154, 296, 12, C_BORDER);
  centered("HOW TO BAT", 24, 1, C_GOLD);

  if (cricket.howToPage == 0) {
    drawSmallFielder(86, 113, 0.78f, C_RED, false);
    drawWickets(86, 188, 0.75f);
    frame.fillCircle(86, 160, 3, C_RED);
    centered("BATTER AUTO-COVERS LINE", 214, 1, C_TEXT);
    centered("WATCH SPEED AND BOUNCE", 232, 1, C_TEXT);
    centered("CLICK ONCE TO SWING", 250, 1, C_CYAN);
  } else {
    frame.fillCircle(86, 103, 15, C_GOLD);
    frame.drawCircle(86, 103, 18, C_CYAN);
    centered("PERFECT TIMING = 4 OR 6", 142, 1, C_GOLD);
    centered("EARLY / LATE = 1 OR 2", 162, 1, C_TEXT);
    centered("STRAIGHT MISS CAN BE BOWLED", 182, 1, C_ORANGE);
    centered("12 BALL MINI INNINGS", 202, 1, C_CYAN);
    centered("HOLD DURING PLAY TO PAUSE", 222, 1, C_MUTED);
  }

  char page[12];
  snprintf(page, sizeof(page), "%u / 2", cricket.howToPage + 1);
  centered(page, 280, 1, C_GOLD);
  centered("CLICK PAGE  HOLD BACK", 296, 1, C_MUTED);
}

static void drawGameplay(uint32_t now) {
  drawSkyAndField(now, dayNightAmount());
  drawFieldPlayers();
  drawBowler();
  drawBall();
  drawBatter();
  drawWicketKeeper();
  drawParticles();
  drawScoreHud();
  drawDeliveryBadge();
  drawFeedback();

  if (cricket.ball.phase == BallPhase::WAITING && !cricket.endPending) {
    centered(cricket.ballsFaced == 0 ? "GET READY" : "NEXT DELIVERY",
             278, 1, C_MUTED);
  }
}

static void drawOverlayPanel(const char *title) {
  frame.fillRoundRect(18, 62, 136, 210, 12, C_PANEL);
  frame.drawRoundRect(18, 62, 136, 210, 12, C_GOLD);
  centered(title, 77, 2, C_TEXT);
  frame.drawFastHLine(31, 105, 110, C_BORDER);
}

static void drawPaused(uint32_t now) {
  drawGameplay(now);
  drawOverlayPanel("PAUSED");
  drawMenuOption("RESUME", 124, cricket.pauseMenu.selected() == 0);
  drawMenuOption("RESTART", 163, cricket.pauseMenu.selected() == 1);
  drawMenuOption("POCKETGAME", 202, cricket.pauseMenu.selected() == 2);
  centered("CLICK NEXT  HOLD SELECT", 244, 1, C_MUTED);
}

static void drawResult(uint32_t now) {
  drawSkyAndField(now, 1.0f);
  drawFieldPlayers();
  drawBatter();

  frame.fillRoundRect(10, 28, 152, 278, 12, C_PANEL);
  frame.drawRoundRect(10, 28, 152, 278, 12,
                      cricket.newBest ? C_GOLD : C_CYAN);
  centered(cricket.wickets >= MAX_WICKETS ? "ALL OUT" : "INNINGS OVER",
           43, 1, cricket.wickets >= MAX_WICKETS ? C_ORANGE : C_CYAN);

  char buffer[28];
  textLeft("RUNS", 25, 75, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)cricket.runs);
  textRight(buffer, 147, 73, 2, C_GOLD);

  textLeft("BALLS", 25, 105, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%u", cricket.ballsFaced);
  textRight(buffer, 147, 105, 1, C_TEXT);

  textLeft("WICKETS", 25, 126, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%u", cricket.wickets);
  textRight(buffer, 147, 126, 1, C_TEXT);

  textLeft("4s / 6s", 25, 147, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%u / %u", cricket.fours, cricket.sixes);
  textRight(buffer, 147, 147, 1, C_GREEN);

  if (cricket.newBest) {
    centered("NEW BEST SCORE!", 169, 1, C_GOLD);
  }

  drawMenuOption("PLAY AGAIN", 190, cricket.resultMenu.selected() == 0);
  drawMenuOption("GAME MENU", 224, cricket.resultMenu.selected() == 1);
  drawMenuOption("POCKETGAME", 258, cricket.resultMenu.selected() == 2);
  centered("CLICK NEXT  HOLD SELECT", 291, 1, C_MUTED);
}

static void renderFrame(uint32_t now) {
  switch (cricket.screens.current()) {
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

  if (cricket.screens.is(GameScreen::MENU) ||
      cricket.screens.is(GameScreen::HOW_TO)) {
    const uint8_t pulse = PocketLed::breatheValue(now, 1450, 18, 125);
    led.set(0, pulse, pulse / 2);
    return;
  }

  if (cricket.screens.is(GameScreen::PAUSED)) {
    const uint8_t pulse = PocketLed::breatheValue(now, 1800, 18, 95);
    led.set(pulse, pulse / 2, 0);
    return;
  }

  if (cricket.screens.is(GameScreen::RESULT)) {
    const bool lit = PocketLed::doubleBlink(now, 950);
    led.set(0, lit ? 160 : 20, lit ? 55 : 7);
    return;
  }

  if (cricket.feedbackLife > 0.0f && cricket.feedbackColour == C_GOLD) {
    led.set(0, 220, 55);
  } else if (cricket.feedbackLife > 0.0f &&
             cricket.feedbackColour == C_ORANGE) {
    const bool lit = ((now / 90U) & 1U) == 0;
    led.set(lit ? 230 : 18, lit ? 55 : 0, 0);
  } else {
    const uint8_t pulse = PocketLed::breatheValue(now, 980, 28, 132);
    led.set(0, pulse, 22);
  }
}

} // namespace

void PocketCricketGame::begin(PocketGameSystem &system) {
  pocketSystem = &system;
  randomGenerator.state = esp_random() ^ 0xC2B2AE35UL;
  frame.setTextWrap(false);
  loadProgress();
  cricket.titleMenu.reset(3, 0);
  cricket.screens.goTo(GameScreen::MENU, millis());
  cricket.lastFrameAt = millis();
  clearParticles();
  renderFrame(millis());
  Serial.println("POCKET CRICKET started.");
}

void PocketCricketGame::loop(PocketGameSystem &system, uint32_t now) {
  pocketSystem = &system;
  handleInput(now);

  if (system.activeGame() != this) {
    return;
  }

  if (now - cricket.lastFrameAt < FRAME_INTERVAL_MS) {
    updateLed(now);
    delay(1);
    return;
  }

  float deltaSeconds = (now - cricket.lastFrameAt) / 1000.0f;
  if (deltaSeconds > MAX_DELTA_SECONDS) {
    deltaSeconds = MAX_DELTA_SECONDS;
  }
  cricket.lastFrameAt = now;

  if (cricket.screens.is(GameScreen::PLAYING)) {
    updateGameplay(deltaSeconds, now);
  } else if (cricket.feedbackLife > 0.0f) {
    cricket.feedbackLife -= deltaSeconds;
    if (cricket.feedbackLife < 0.0f) {
      cricket.feedbackLife = 0.0f;
    }
  }

  renderFrame(now);
  updateLed(now);
}
