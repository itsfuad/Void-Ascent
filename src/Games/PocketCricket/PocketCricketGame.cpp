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
static constexpr uint8_t MAX_PARTICLES = 42;
static constexpr uint8_t BALLS_PER_INNINGS = 12;
static constexpr uint8_t MAX_WICKETS = 3;

static constexpr uint16_t C565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((uint16_t)(red & 0xF8) << 8) |
         ((uint16_t)(green & 0xFC) << 3) |
         ((uint16_t)blue >> 3);
}

static constexpr uint16_t C_BLACK = C565(7, 11, 19);
static constexpr uint16_t C_NAVY = C565(9, 23, 46);
static constexpr uint16_t C_PANEL = C565(14, 35, 57);
static constexpr uint16_t C_PANEL_2 = C565(24, 51, 84);
static constexpr uint16_t C_BORDER = C565(56, 109, 168);
static constexpr uint16_t C_TEXT = C565(241, 246, 248);
static constexpr uint16_t C_MUTED = C565(140, 168, 188);
static constexpr uint16_t C_CYAN = C565(69, 212, 236);
static constexpr uint16_t C_BLUE = C565(60, 135, 238);
static constexpr uint16_t C_GOLD = C565(255, 208, 76);
static constexpr uint16_t C_GREEN = C565(92, 210, 96);
static constexpr uint16_t C_GREEN_DARK = C565(45, 144, 67);
static constexpr uint16_t C_GRASS = C565(117, 210, 97);
static constexpr uint16_t C_GRASS_2 = C565(92, 187, 84);
static constexpr uint16_t C_SKY = C565(29, 170, 221);
static constexpr uint16_t C_SKY_2 = C565(104, 222, 254);
static constexpr uint16_t C_STAND = C565(53, 72, 136);
static constexpr uint16_t C_STAND_DARK = C565(30, 46, 97);
static constexpr uint16_t C_PITCH = C565(198, 163, 110);
static constexpr uint16_t C_PITCH_LINE = C565(233, 214, 181);
static constexpr uint16_t C_BROWN = C565(150, 94, 55);
static constexpr uint16_t C_BAT = C565(243, 225, 164);
static constexpr uint16_t C_BALL = C565(235, 65, 59);
static constexpr uint16_t C_WHITE = C565(255, 255, 255);
static constexpr uint16_t C_SHADOW = C565(18, 52, 25);
static constexpr uint16_t C_LIME = C565(144, 231, 56);
static constexpr uint16_t C_ORANGE = C565(247, 140, 54);

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
enum class BallPhase : uint8_t { WAITING, TRAVELLING, HIT, RESOLVED };
enum class OutcomeType : uint8_t { NONE, DOT, SINGLE, DOUBLE, FOUR, SIX, WICKET };

struct BallState {
  BallPhase phase = BallPhase::WAITING;
  DeliveryType type = DeliveryType::FAST;
  OutcomeType outcome = OutcomeType::NONE;

  float x = 86.0f;
  float y = 98.0f;
  float z = 0.0f;
  float vx = 0.0f;
  float vy = 0.0f;
  float vz = 0.0f;
  float bounceY = 198.0f;
  float bounceLift = 26.0f;
  float drift = 0.0f;

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
  float stanceLift = 0.0f;
  float stanceTarget = 0.0f;
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

  char feedback[20] = {};
  uint16_t feedbackColour = C_TEXT;
  float feedbackLife = 0.0f;

  bool inningsOver = false;
  bool newBest = false;
};

static CricketState cricket;

static const char *deliveryName(DeliveryType type) {
  switch (type) {
  case DeliveryType::FAST:
    return "FAST BALL";
  case DeliveryType::SLOW:
    return "SLOW BALL";
  case DeliveryType::SPIN:
    return "SPIN BALL";
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
  cricket.particles[0].active = true;
  return &cricket.particles[0];
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

static void spawnBurst(float x, float y, uint8_t count, uint16_t c1, uint16_t c2) {
  for (uint8_t i = 0; i < count; ++i) {
    const float angle = randomGenerator.range(3.3f, 6.2f);
    const float speed = randomGenerator.range(20.0f, 100.0f);
    spawnParticle(x, y, cosf(angle) * speed, sinf(angle) * speed,
                  randomGenerator.range(0.3f, 0.8f), (i & 1) ? c1 : c2);
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

static void clearParticles() {
  for (uint8_t i = 0; i < MAX_PARTICLES; ++i) {
    cricket.particles[i].active = false;
  }
}

static void strokeLine(float x1, float y1, float x2, float y2,
                       uint8_t thickness, uint16_t colour) {
  const int16_t steps = (int16_t)(fabsf(x2 - x1) > fabsf(y2 - y1)
                                      ? fabsf(x2 - x1)
                                      : fabsf(y2 - y1));
  if (steps <= 0) {
    frame.fillCircle((int16_t)roundf(x1), (int16_t)roundf(y1), thickness / 2,
                     colour);
    return;
  }
  for (int16_t step = 0; step <= steps; ++step) {
    const float amount = step / (float)steps;
    frame.fillCircle((int16_t)roundf(lerpFloat(x1, x2, amount)),
                     (int16_t)roundf(lerpFloat(y1, y2, amount)),
                     thickness / 2, colour);
  }
}

static void drawStump(float x, float y) {
  strokeLine(x, y, x, y + 14, 2, C_BROWN);
}

static void drawWickets(float x, float y) {
  drawStump(x - 4, y);
  drawStump(x, y);
  drawStump(x + 4, y);
  frame.drawFastHLine((int16_t)x - 6, (int16_t)y + 2, 4, C_WHITE);
  frame.drawFastHLine((int16_t)x + 2, (int16_t)y + 2, 4, C_WHITE);
}

static void drawBallShadow(float x, float y, float z) {
  const int16_t width = (int16_t)clampFloat(4.0f - z * 0.05f, 1.0f, 4.0f);
  frame.fillRoundRect((int16_t)roundf(x) - width, (int16_t)roundf(y) + 2,
                      width * 2, 3, 1, C_SHADOW);
}

static void drawBowler(uint32_t now) {
  const float cycle = sinf(now * 0.0036f) * 0.5f + 0.5f;
  const float x = 86.0f;
  const float y = 112.0f;
  frame.fillRoundRect((int16_t)x - 10, (int16_t)y + 13, 20, 4, 2, C_SHADOW);
  frame.fillCircle((int16_t)x, (int16_t)y - 18, 6, C_WHITE);
  frame.fillCircle((int16_t)x - 1, (int16_t)y - 19, 2, C_NAVY);
  frame.fillRoundRect((int16_t)x - 6, (int16_t)y - 11, 12, 16, 3, C_BLUE);
  strokeLine(x, y + 4, x - 5, y + 18, 3, C_BLUE);
  strokeLine(x, y + 4, x + 5, y + 18, 3, C_BLUE);
  strokeLine(x - 2, y - 4, x - 10 + cycle * 5.0f, y + 3, 3, C_GREEN);
  strokeLine(x + 2, y - 4, x + 8 + cycle * 5.0f, y - 14 + cycle * 8.0f, 3,
             C_GREEN);
}

static void drawBatter() {
  const float x = cricket.batterX;
  const float y = 240.0f;
  const float stance = cricket.stanceLift;
  const float s = cricket.swinging ? cricket.swingProgress : 0.0f;
  const float batX = x + 16.0f + s * 22.0f - s * s * 12.0f;
  const float batY = y - 12.0f - stance * 8.0f - s * 20.0f + s * s * 8.0f;
  const float batTipX = batX + 16.0f + s * 12.0f;
  const float batTipY = batY - 22.0f + s * 10.0f;

  frame.fillRoundRect((int16_t)x - 12, (int16_t)y + 23, 24, 5, 2, C_SHADOW);
  drawWickets(95.0f, 235.0f);

  frame.fillCircle((int16_t)x, (int16_t)y - 30, 8, C_LIME);
  frame.fillCircle((int16_t)x - 2, (int16_t)y - 31, 2, C_NAVY);
  frame.fillCircle((int16_t)x + 3, (int16_t)y - 31, 2, C_NAVY);

  frame.fillRoundRect((int16_t)x - 7, (int16_t)y - 20, 14, 22, 4, C_GREEN);
  strokeLine(x - 2, y + 2, x - 7 - stance * 3.0f, y + 22, 4, C_GREEN_DARK);
  strokeLine(x + 3, y + 2, x + 8 + stance * 2.0f, y + 22, 4, C_GREEN_DARK);

  strokeLine(x - 4, y - 11, x - 14, y - 2 + stance * 2.0f, 4, C_GREEN);
  strokeLine(x + 4, y - 10, batX, batY, 4, C_GREEN);
  strokeLine(batX, batY, batTipX, batTipY, 5, C_BAT);
  strokeLine(batX, batY, batTipX, batTipY, 2, C_BROWN);
}

static void drawSkyAndGround(uint32_t now, bool night) {
  for (int16_t y = 0; y < 136; ++y) {
    const float amount = y / 135.0f;
    const uint16_t top = night ? C565(17, 34, 84) : C_SKY;
    const uint16_t bottom = night ? C565(96, 92, 168) : C_SKY_2;
    const uint8_t r = (uint8_t)((((top >> 11) & 31) << 3) * (1.0f - amount) +
                                (((bottom >> 11) & 31) << 3) * amount);
    const uint8_t g = (uint8_t)((((top >> 5) & 63) << 2) * (1.0f - amount) +
                                (((bottom >> 5) & 63) << 2) * amount);
    const uint8_t b = (uint8_t)(((top & 31) << 3) * (1.0f - amount) +
                                ((bottom & 31) << 3) * amount);
    frame.drawFastHLine(0, y, SCREEN_W, C565(r, g, b));
  }

  if (!night) {
    frame.fillCircle(143, 36, 13, C_GOLD);
    frame.fillCircle(139, 32, 8, C565(255, 238, 169));
  } else {
    frame.fillCircle(143, 36, 10, C_WHITE);
    frame.fillCircle(139, 33, 9, C565(56, 78, 152));
  }

  for (uint8_t i = 0; i < 3; ++i) {
    const int16_t x = (int16_t)((now / 70U + i * 57U) % 210U) - 25;
    const int16_t y = 44 + i * 18;
    frame.fillCircle(x, y, 8, C_WHITE);
    frame.fillCircle(x + 9, y - 2, 10, C_WHITE);
    frame.fillCircle(x + 19, y, 8, C_WHITE);
    frame.fillRect(x, y, 19, 7, C_WHITE);
  }

  frame.fillRect(0, 101, SCREEN_W, 24, C_STAND);
  frame.fillRect(0, 118, SCREEN_W, 18, C_STAND_DARK);
  for (uint8_t i = 0; i < 22; ++i) {
    const int16_t cx = 5 + i * 8;
    frame.fillCircle(cx, 109 + (i & 1), 2, (i & 2) ? C_GOLD : C_CYAN);
    frame.fillCircle(cx + 2, 111 + (i & 1), 2, C_TEXT);
  }
  frame.fillRoundRect(62, 78, 48, 26, 3, C_BLUE);
  frame.drawRoundRect(62, 78, 48, 26, 3, C_TEXT);
  textLeft("019", 76, 87, 1, C_TEXT);
  textLeft("4", 31, 78, 1, C_LIME);
  textLeft("4", 125, 78, 1, C_LIME);

  frame.fillRect(0, 136, SCREEN_W, 184, C_GRASS);
  frame.fillRect(0, 216, SCREEN_W, 104, C_GRASS_2);

  frame.fillTriangle(56, 298, 86, 143, 116, 298, C_PITCH);
  frame.drawLine(72, 185, 60, 282, C_PITCH_LINE);
  frame.drawLine(100, 185, 112, 282, C_PITCH_LINE);
  frame.drawFastHLine(76, 228, 20, C_PITCH_LINE);
  frame.drawFastHLine(72, 258, 28, C_PITCH_LINE);
  frame.drawFastHLine(68, 287, 36, C_PITCH_LINE);
}

static void drawScoreHud() {
  frame.fillRoundRect(6, 6, 160, 30, 6, C_PANEL);
  frame.drawRoundRect(6, 6, 160, 30, 6, C_BORDER);
  char buffer[22];
  textLeft("RUNS", 12, 10, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)cricket.runs);
  textLeft(buffer, 12, 20, 1, C_GOLD);
  textLeft("WKTS", 72, 10, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%u", cricket.wickets);
  textLeft(buffer, 72, 20, 1, C_TEXT);
  textLeft("BALL", 122, 10, 1, C_MUTED);
  snprintf(buffer, sizeof(buffer), "%u/%u", cricket.ballsFaced, BALLS_PER_INNINGS);
  textLeft(buffer, 122, 20, 1, C_CYAN);
}

static void drawDeliveryBadge() {
  frame.fillRoundRect(47, 38, 78, 18, 5, C_PANEL);
  frame.drawRoundRect(47, 38, 78, 18, 5, C_BORDER);
  centered(deliveryName(cricket.ball.type), 44, 1, C_TEXT);
}

static void drawParticles() {
  for (uint8_t i = 0; i < MAX_PARTICLES; ++i) {
    const Particle &particle = cricket.particles[i];
    if (!particle.active) {
      continue;
    }
    frame.fillRect((int16_t)roundf(particle.x), (int16_t)roundf(particle.y), 2,
                   2, particle.colour);
  }
}

static void drawBall() {
  if (cricket.ball.phase == BallPhase::WAITING) {
    return;
  }
  const float screenY = cricket.ball.y - cricket.ball.z;
  drawBallShadow(cricket.ball.x, cricket.ball.y, cricket.ball.z);
  frame.fillCircle((int16_t)roundf(cricket.ball.x), (int16_t)roundf(screenY), 3,
                   C_BALL);
  frame.drawPixel((int16_t)roundf(cricket.ball.x) - 1,
                  (int16_t)roundf(screenY) - 1, C_WHITE);
}

static void drawFeedback() {
  if (cricket.feedbackLife <= 0.0f) {
    return;
  }
  frame.fillRoundRect(38, 262, 96, 21, 5, C_PANEL);
  frame.drawRoundRect(38, 262, 96, 21, 5, cricket.feedbackColour);
  centered(cricket.feedback, 269, 1, cricket.feedbackColour);
}

static void loadProgress() {
  pocketgame::PocketStorage &storage = pocketSystem->storage();
  cricket.bestScore = storage.getUInt32("best", 0);
  cricket.bestFours = storage.getUInt8("fours", 0);
  cricket.bestSixes = storage.getUInt8("sixes", 0);
}

static void saveProgress() {
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
  cricket.stanceLift = 0.0f;
  cricket.stanceTarget = 0.0f;
  cricket.swingProgress = 0.0f;
  cricket.swinging = false;
  cricket.shotResolved = false;
  cricket.ball = BallState{};
  cricket.nextBallAt = now + 650U;
  cricket.resultUntil = 0;
  cricket.feedback[0] = '\0';
  cricket.feedbackLife = 0.0f;
  cricket.inningsOver = false;
  cricket.newBest = false;
  clearParticles();
  cricket.screens.goTo(GameScreen::PLAYING, now);
  cricket.lastFrameAt = now;
}

static void finishMatch(uint32_t now) {
  if (cricket.inningsOver) {
    return;
  }
  cricket.inningsOver = true;
  saveProgress();
  cricket.resultMenu.reset(3, 0);
  cricket.screens.goTo(GameScreen::RESULT, now);
}

static void startDelivery() {
  cricket.ball = BallState{};
  cricket.ball.phase = BallPhase::TRAVELLING;
  cricket.ball.type = (DeliveryType)(randomGenerator.rangeInt(0, 5));
  cricket.ball.x = 86.0f + randomGenerator.range(-7.0f, 7.0f);
  cricket.ball.y = 112.0f;
  cricket.ball.z = 0.0f;
  cricket.ball.bounced = false;
  cricket.shotResolved = false;
  cricket.swinging = false;
  cricket.swingProgress = 0.0f;

  float targetX = 86.0f + randomGenerator.range(-20.0f, 18.0f);
  cricket.batterTargetX = 86.0f + (targetX - 86.0f) * 0.45f;
  cricket.stanceTarget = 0.0f;

  switch (cricket.ball.type) {
  case DeliveryType::FAST:
    cricket.ball.vy = 124.0f;
    cricket.ball.vx = (targetX - cricket.ball.x) / 1.10f;
    cricket.ball.bounceY = 202.0f;
    cricket.ball.bounceLift = 18.0f;
    cricket.ball.drift = 0.0f;
    break;
  case DeliveryType::SLOW:
    cricket.ball.vy = 84.0f;
    cricket.ball.vx = (targetX - cricket.ball.x) / 1.48f;
    cricket.ball.bounceY = 198.0f;
    cricket.ball.bounceLift = 17.0f;
    cricket.ball.drift = 0.0f;
    break;
  case DeliveryType::SPIN:
    cricket.ball.vy = 98.0f;
    cricket.ball.vx = (targetX - cricket.ball.x) / 1.35f;
    cricket.ball.bounceY = 198.0f;
    cricket.ball.bounceLift = 22.0f;
    cricket.ball.drift = randomGenerator.range(-7.0f, 7.0f);
    break;
  case DeliveryType::YORKER:
    cricket.ball.vy = 112.0f;
    cricket.ball.vx = (targetX - cricket.ball.x) / 1.18f;
    cricket.ball.bounceY = 236.0f;
    cricket.ball.bounceLift = 10.0f;
    cricket.stanceTarget = 1.0f;
    break;
  case DeliveryType::BOUNCER:
    cricket.ball.vy = 116.0f;
    cricket.ball.vx = (targetX - cricket.ball.x) / 1.15f;
    cricket.ball.bounceY = 176.0f;
    cricket.ball.bounceLift = 34.0f;
    cricket.stanceTarget = -1.0f;
    break;
  case DeliveryType::SWINGER:
  default:
    cricket.ball.vy = 105.0f;
    cricket.ball.vx = (targetX - cricket.ball.x) / 1.25f;
    cricket.ball.bounceY = 200.0f;
    cricket.ball.bounceLift = 20.0f;
    cricket.ball.drift = randomGenerator.range(-3.5f, 3.5f);
    break;
  }

  setFeedback(deliveryName(cricket.ball.type), C_CYAN, 0.45f);
}

static void startSwing() {
  if (cricket.ball.phase != BallPhase::TRAVELLING || cricket.shotResolved) {
    return;
  }
  cricket.swinging = true;
  cricket.swingProgress = 0.0f;
}

static void resolveBall(OutcomeType outcome, uint8_t runsAwarded,
                        const char *label, uint16_t colour, uint32_t now) {
  cricket.ball.outcome = outcome;
  cricket.shotResolved = true;
  cricket.resultUntil = now + 780U;
  setFeedback(label, colour, 1.0f);

  if (!cricket.ball.counted) {
    cricket.ball.counted = true;
    ++cricket.ballsFaced;
  }

  switch (outcome) {
  case OutcomeType::SINGLE:
    cricket.runs += runsAwarded;
    break;
  case OutcomeType::DOUBLE:
    cricket.runs += runsAwarded;
    break;
  case OutcomeType::FOUR:
    cricket.runs += runsAwarded;
    ++cricket.fours;
    break;
  case OutcomeType::SIX:
    cricket.runs += runsAwarded;
    ++cricket.sixes;
    break;
  case OutcomeType::WICKET:
    ++cricket.wickets;
    break;
  default:
    cricket.runs += runsAwarded;
    break;
  }

  if (cricket.wickets >= MAX_WICKETS || cricket.ballsFaced >= BALLS_PER_INNINGS) {
    finishMatch(now + 10U);
  } else {
    cricket.nextBallAt = now + 860U;
  }
}

static void contactBall(uint32_t now) {
  if (cricket.shotResolved || cricket.ball.phase != BallPhase::TRAVELLING) {
    return;
  }
  const float screenY = cricket.ball.y - cricket.ball.z;
  const float hitX = cricket.batterX + 14.0f + cricket.swingProgress * 8.0f;
  const float hitY = 220.0f - cricket.stanceLift * 10.0f - cricket.swingProgress * 8.0f;
  const float timingError = fabsf(screenY - hitY);
  const float xError = fabsf(cricket.ball.x - hitX);

  if (timingError > 16.0f || xError > 20.0f || cricket.swingProgress < 0.20f ||
      cricket.swingProgress > 0.82f) {
    return;
  }

  const float timingScore = 1.0f - clampFloat(timingError / 16.0f, 0.0f, 1.0f);
  const float placementScore =
      1.0f - clampFloat(xError / 20.0f, 0.0f, 1.0f);
  const float quality = timingScore * 0.65f + placementScore * 0.35f;

  cricket.ball.phase = BallPhase::HIT;
  cricket.ball.vy = -(88.0f + quality * 52.0f);
  cricket.ball.vx = (cricket.ball.x - cricket.batterX) * 0.9f +
                    randomGenerator.range(-24.0f, 24.0f);
  cricket.ball.vz = 84.0f + quality * 60.0f;
  cricket.ball.drift = 0.0f;

  if (quality > 0.84f) {
    const bool loft = cricket.ball.z > 5.0f || cricket.ball.type == DeliveryType::BOUNCER ||
                      randomGenerator.rangeInt(0, 1) == 0;
    resolveBall(loft ? OutcomeType::SIX : OutcomeType::FOUR,
                loft ? 6 : 4, loft ? "SIX!" : "FOUR!",
                C_GOLD, now);
    spawnBurst(cricket.ball.x, screenY, 18, C_GOLD, C_CYAN);
  } else if (quality > 0.62f) {
    resolveBall(OutcomeType::FOUR, 4, "DRIVE!", C_GREEN, now);
    spawnBurst(cricket.ball.x, screenY, 12, C_GREEN, C_CYAN);
  } else if (quality > 0.42f) {
    resolveBall(OutcomeType::DOUBLE, 2, "TWO RUNS", C_CYAN, now);
    spawnBurst(cricket.ball.x, screenY, 8, C_CYAN, C_WHITE);
  } else {
    resolveBall(OutcomeType::SINGLE, 1, "SINGLE", C_TEXT, now);
    spawnBurst(cricket.ball.x, screenY, 6, C_WHITE, C_CYAN);
  }
}

static void updateGameplay(float deltaSeconds, uint32_t now) {
  cricket.batterX = lerpFloat(cricket.batterX, cricket.batterTargetX,
                              1.0f - expf(-5.0f * deltaSeconds));
  cricket.stanceLift = lerpFloat(cricket.stanceLift, cricket.stanceTarget,
                                 1.0f - expf(-5.0f * deltaSeconds));

  if (cricket.ball.phase == BallPhase::WAITING && now >= cricket.nextBallAt &&
      !cricket.inningsOver) {
    startDelivery();
  }

  if (cricket.swinging) {
    cricket.swingProgress += deltaSeconds / 0.26f;
    if (cricket.swingProgress >= 1.0f) {
      cricket.swingProgress = 0.0f;
      cricket.swinging = false;
    }
  }

  if (cricket.ball.phase == BallPhase::TRAVELLING) {
    cricket.ball.y += cricket.ball.vy * deltaSeconds;
    cricket.ball.x += cricket.ball.vx * deltaSeconds;
    if (cricket.ball.bounced) {
      cricket.ball.vx += cricket.ball.drift * deltaSeconds;
    }

    if (!cricket.ball.bounced && cricket.ball.y >= cricket.ball.bounceY) {
      cricket.ball.bounced = true;
      cricket.ball.z = 0.0f;
      cricket.ball.vz = cricket.ball.bounceLift;
      if (cricket.ball.type == DeliveryType::SLOW) {
        cricket.ball.vy *= 0.92f;
      }
      if (cricket.ball.type == DeliveryType::SPIN) {
        cricket.ball.drift += randomGenerator.range(-30.0f, 30.0f);
      }
      if (cricket.ball.type == DeliveryType::SWINGER) {
        cricket.ball.drift += randomGenerator.range(-12.0f, 12.0f);
      }
      spawnBurst(cricket.ball.x, cricket.ball.y, 4, C_PITCH_LINE, C_PITCH);
    }

    if (cricket.ball.bounced) {
      cricket.ball.z += cricket.ball.vz * deltaSeconds;
      cricket.ball.vz -= 140.0f * deltaSeconds;
      if (cricket.ball.z < 0.0f) {
        cricket.ball.z = 0.0f;
        cricket.ball.vz = 0.0f;
      }
    }

    if (cricket.swinging) {
      contactBall(now);
    }

    if (!cricket.shotResolved && cricket.ball.y >= 244.0f) {
      if (fabsf(cricket.ball.x - 95.0f) < 10.0f && cricket.ball.z < 8.0f) {
        resolveBall(OutcomeType::WICKET, 0, "BOWLED!", C_ORANGE, now);
        spawnBurst(95.0f, 236.0f, 12, C_ORANGE, C_WHITE);
      } else {
        resolveBall(OutcomeType::DOT, 0, "DOT BALL", C_MUTED, now);
      }
      cricket.ball.phase = BallPhase::RESOLVED;
    }
  } else if (cricket.ball.phase == BallPhase::HIT) {
    cricket.ball.y += cricket.ball.vy * deltaSeconds;
    cricket.ball.x += cricket.ball.vx * deltaSeconds;
    cricket.ball.z += cricket.ball.vz * deltaSeconds;
    cricket.ball.vz -= 175.0f * deltaSeconds;
    if (cricket.ball.z < 0.0f) {
      cricket.ball.z = 0.0f;
    }
    if (cricket.ball.y < 92.0f || now >= cricket.resultUntil) {
      cricket.ball.phase = BallPhase::WAITING;
    }
  } else if (cricket.ball.phase == BallPhase::RESOLVED) {
    if (now >= cricket.resultUntil) {
      cricket.ball.phase = BallPhase::WAITING;
    }
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
  drawSkyAndGround(now, false);
  drawBowler(now);
  drawBatter();
  drawWickets(95.0f, 235.0f);

  const int16_t ballX = 86 + (int16_t)(sinf(now * 0.004f) * 9.0f);
  frame.fillCircle(ballX, 165, 3, C_BALL);
  frame.drawPixel(ballX - 1, 164, C_WHITE);

  frame.fillRoundRect(10, 12, 152, 46, 10, C_PANEL);
  frame.drawRoundRect(10, 12, 152, 46, 10, C_BORDER);
  centered("POCKET", 20, 2, C_TEXT);
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
  drawSkyAndGround(now, false);
  frame.fillRoundRect(9, 12, 154, 296, 12, C_PANEL);
  frame.drawRoundRect(9, 12, 154, 296, 12, C_BORDER);
  centered("HOW TO BAT", 24, 1, C_GOLD);

  if (cricket.howToPage == 0) {
    drawBowler(now);
    drawBatter();
    frame.fillCircle(86, 190, 3, C_BALL);
    centered("1. WATCH BALL SPEED", 214, 1, C_TEXT);
    centered("2. BATTER AUTO-MOVES", 232, 1, C_TEXT);
    centered("3. CLICK TO SWING", 250, 1, C_CYAN);
  } else {
    frame.fillCircle(86, 108, 15, C_GOLD);
    centered("PERFECT TIMING = 4 OR 6", 142, 1, C_GOLD);
    centered("EARLY OR LATE = 1 OR 2", 162, 1, C_TEXT);
    centered("MISS STRAIGHT BALLS = WICKET", 182, 1, C_ORANGE);
    centered("12 BALL MINI INNINGS", 202, 1, C_CYAN);
    centered("HOLD DURING PLAY TO PAUSE", 222, 1, C_MUTED);
  }

  char page[12];
  snprintf(page, sizeof(page), "%u / 2", cricket.howToPage + 1);
  centered(page, 280, 1, C_GOLD);
  centered("CLICK PAGE  HOLD BACK", 296, 1, C_MUTED);
}

static void drawGameplay(uint32_t now) {
  drawSkyAndGround(now, cricket.ballsFaced >= 8);
  drawScoreHud();
  if (cricket.ball.phase != BallPhase::WAITING) {
    drawDeliveryBadge();
  }
  drawBowler(now);
  drawBatter();
  drawBall();
  drawParticles();
  drawFeedback();

  if (cricket.ballsFaced == 0 && cricket.ball.phase == BallPhase::WAITING) {
    centered("CLICK TO SWING", 294, 1, C_TEXT);
  } else if (cricket.ball.phase == BallPhase::WAITING && !cricket.inningsOver) {
    centered("NEXT BALL", 294, 1, C_MUTED);
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
  drawSkyAndGround(now, cricket.ballsFaced >= 8);
  drawBatter();
  frame.fillRoundRect(10, 28, 152, 278, 12, C_PANEL);
  frame.drawRoundRect(10, 28, 152, 278, 12, cricket.newBest ? C_GOLD : C_CYAN);

  centered(cricket.wickets >= MAX_WICKETS ? "ALL OUT" : "INNINGS OVER", 43, 1,
           cricket.wickets >= MAX_WICKETS ? C_ORANGE : C_CYAN);

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
  if (cricket.screens.is(GameScreen::MENU) || cricket.screens.is(GameScreen::HOW_TO)) {
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
    led.set(lit ? 0 : 0, lit ? 145 : 20, lit ? 45 : 8);
    return;
  }

  if (cricket.feedbackLife > 0.0f && cricket.feedbackColour == C_GOLD) {
    led.set(0, 220, 48);
  } else if (cricket.feedbackLife > 0.0f && cricket.feedbackColour == C_ORANGE) {
    const bool lit = ((now / 90U) & 1U) == 0;
    led.set(lit ? 230 : 18, lit ? 60 : 0, 0);
  } else {
    const uint8_t pulse = PocketLed::breatheValue(now, 980, 28, 132);
    led.set(0, pulse, 20);
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
