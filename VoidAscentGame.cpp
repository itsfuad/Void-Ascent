#include "VoidAscentGame.h"

#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <esp_system.h>
#include <math.h>
#include <string.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// Arduino defines FALLING as an interrupt-mode macro.
#ifdef FALLING
#undef FALLING
#endif

// =============================================================================
// Hardware
// =============================================================================

#define PIN_LCD_MOSI 6
#define PIN_LCD_SCLK 7
#define PIN_LCD_CS 14
#define PIN_LCD_DC 15
#define PIN_LCD_RST 21
#define PIN_LCD_BL 22

#define PIN_RGB_LED 8
#define PIN_BUTTON 9

static constexpr int16_t SCREEN_W = 172;
static constexpr int16_t SCREEN_H = 320;

static constexpr int16_t LCD_NATIVE_W = 172;
static constexpr int16_t LCD_NATIVE_H = 320;

#define LCD_ROTATION 2
#define LCD_SPI_HZ 40000000UL

Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(
    PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, GFX_NOT_DEFINED);

Arduino_GFX *gfx = new Arduino_ST7789(lcdBus, PIN_LCD_RST, LCD_ROTATION, true,
                                      LCD_NATIVE_W, LCD_NATIVE_H, 34, 0, 34, 0);

static GFXcanvas16 frame(SCREEN_W, SCREEN_H);

static Adafruit_NeoPixel rgbLed(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);

static Preferences preferences;

// =============================================================================
// Timing and camera
// =============================================================================

static constexpr uint32_t FRAME_INTERVAL_MS = 33;

// Faster visual world movement.
static constexpr float WORLD_PIXELS_PER_ALTITUDE = 5.80f;

// Faster camera response makes acceleration clearly visible.
static constexpr float CAMERA_FOLLOW_SPEED = 9.0f;

// Prevents the camera from lagging too far behind.
static constexpr float CAMERA_MAX_LAG_ALTITUDE = 1.15f;

// =============================================================================
// Colours
// =============================================================================

static constexpr uint16_t C565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((uint16_t)(red & 0xF8) << 8) | ((uint16_t)(green & 0xFC) << 3) |
         ((uint16_t)blue >> 3);
}

static constexpr uint16_t C_BLACK = C565(2, 4, 8);
static constexpr uint16_t C_VOID = C565(5, 8, 17);
static constexpr uint16_t C_PANEL = C565(13, 19, 30);
static constexpr uint16_t C_PANEL_2 = C565(21, 29, 43);
static constexpr uint16_t C_BORDER = C565(43, 55, 73);

static constexpr uint16_t C_TEXT = C565(235, 241, 244);
static constexpr uint16_t C_WHITE = C565(255, 255, 250);
static constexpr uint16_t C_MUTED = C565(111, 124, 141);

static constexpr uint16_t C_GREEN = C565(83, 235, 166);
static constexpr uint16_t C_FUEL = C565(89, 246, 178);
static constexpr uint16_t C_CYAN = C565(66, 210, 242);
static constexpr uint16_t C_BLUE = C565(64, 123, 238);
static constexpr uint16_t C_PURPLE = C565(153, 91, 238);

static constexpr uint16_t C_YELLOW = C565(255, 213, 75);
static constexpr uint16_t C_ORANGE = C565(250, 125, 43);
static constexpr uint16_t C_RED = C565(242, 49, 61);
static constexpr uint16_t C_DARK_RED = C565(126, 34, 38);

static constexpr uint16_t C_PAPER = C565(218, 226, 226);
static constexpr uint16_t C_HIGHLIGHT = C565(246, 246, 239);
static constexpr uint16_t C_METAL = C565(139, 153, 164);
static constexpr uint16_t C_METAL_DARK = C565(42, 49, 60);

static constexpr uint16_t C_GLASS = C565(15, 40, 55);
static constexpr uint16_t C_GLASS_EDGE = C565(74, 108, 122);

static constexpr uint16_t C_SMOKE = C565(190, 192, 183);
static constexpr uint16_t C_SMOKE_DARK = C565(105, 108, 108);

static constexpr uint16_t C_TOWER = C565(118, 57, 48);
static constexpr uint16_t C_TOWER_LIGHT = C565(178, 76, 57);
static constexpr uint16_t C_GROUND = C565(54, 43, 40);
static constexpr uint16_t C_CONCRETE = C565(106, 88, 77);

static constexpr uint16_t C_OCEAN_DEEP = C565(11, 51, 112);
static constexpr uint16_t C_OCEAN = C565(28, 105, 194);
static constexpr uint16_t C_OCEAN_LIGHT = C565(54, 151, 224);

static constexpr uint16_t C_LAND_DARK = C565(40, 105, 65);
static constexpr uint16_t C_LAND = C565(66, 171, 91);
static constexpr uint16_t C_LAND_LIGHT = C565(121, 193, 102);

static constexpr uint16_t C_ATMOSPHERE = C565(177, 226, 247);
static constexpr uint16_t C_CLOUD = C565(227, 235, 235);

// =============================================================================
// General helpers
// =============================================================================

static float clampFloat(float value, float minimum, float maximum) {
  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

static float clamp01(float value) { return clampFloat(value, 0.0f, 1.0f); }

static float minFloat(float first, float second) {
  return first < second ? first : second;
}

static float maxFloat(float first, float second) {
  return first > second ? first : second;
}

static int16_t minInt16(int16_t first, int16_t second) {
  return first < second ? first : second;
}

static int16_t maxInt16(int16_t first, int16_t second) {
  return first > second ? first : second;
}

static float lerpFloat(float first, float second, float amount) {
  return first + (second - first) * amount;
}

static float smoothStep(float edge0, float edge1, float value) {
  float amount = clamp01((value - edge0) / (edge1 - edge0));

  return amount * amount * (3.0f - 2.0f * amount);
}

static uint16_t blend565(uint16_t first, uint16_t second, uint8_t amount) {
  uint8_t firstRed = ((first >> 11) & 31) << 3;

  uint8_t firstGreen = ((first >> 5) & 63) << 2;

  uint8_t firstBlue = (first & 31) << 3;

  uint8_t secondRed = ((second >> 11) & 31) << 3;

  uint8_t secondGreen = ((second >> 5) & 63) << 2;

  uint8_t secondBlue = (second & 31) << 3;

  uint16_t red = (firstRed * (255 - amount) + secondRed * amount) >> 8;

  uint16_t green = (firstGreen * (255 - amount) + secondGreen * amount) >> 8;

  uint16_t blue = (firstBlue * (255 - amount) + secondBlue * amount) >> 8;

  return C565((uint8_t)red, (uint8_t)green, (uint8_t)blue);
}

struct RandomGenerator {
  uint32_t state = 0x61C88647UL;

  uint32_t next() {
    state = state * 1664525UL + 1013904223UL;

    return state;
  }

  float range(float minimum, float maximum) {
    float fraction = (float)(next() & 0xFFFF) / 65535.0f;

    return minimum + (maximum - minimum) * fraction;
  }

  int rangeInt(int minimum, int maximum) {
    return minimum + (int)(next() % (uint32_t)(maximum - minimum + 1));
  }

  bool chance(float probability) { return range(0.0f, 1.0f) < probability; }
};

static RandomGenerator randomGenerator;

// =============================================================================
// Missions
// =============================================================================

struct StageDefinition {
  const char *name;

  uint8_t width;
  uint8_t height;

  float burnSeconds;
  float thrust;

  uint16_t bandColour;
};

struct MissionDefinition {
  const char *code;
  const char *name;

  const char *objectiveLine1;
  const char *objectiveLine2;

  uint16_t targetAltitude;
  bool recoverCapsule;

  uint8_t stageCount;
  StageDefinition stages[4];
};

// Longer burn times mean stage separation happens much higher.
// Higher thrust makes the rocket and world movement feel faster.
static const MissionDefinition MISSIONS[] = {
    {"T-01",
     "TEST FLIGHT",
     "VALIDATE THE STACK",
     "RECOVER THE CAPSULE",
     450,
     true,
     2,
     {{"BOOSTER", 31, 68, 7.40f, 7.00f, C_ORANGE},
      {"UPPER", 22, 50, 6.00f, 5.60f, C_CYAN},
      {},
      {}}},

    {"K-100",
     "KARMAN RUN",
     "CROSS OUTER SPACE",
     "PAYLOAD CONTINUES",
     535,
     false,
     2,
     {{"BOOSTER", 32, 70, 7.65f, 7.15f, C_ORANGE},
      {"UPPER", 23, 53, 6.25f, 5.75f, C_BLUE},
      {},
      {}}},

    {"P-180",
     "ORBITAL PROBE",
     "REACH HIGH SPACE",
     "DEPLOY THE PROBE",
     920,
     false,
     3,
     {{"BOOSTER", 33, 66, 7.80f, 7.30f, C_ORANGE},
      {"CORE", 25, 50, 6.40f, 6.15f, C_BLUE},
      {"UPPER", 18, 38, 4.75f, 5.15f, C_PURPLE},
      {}}},

    {"C-220",
     "CREW QUAL",
     "COMPLETE CREW ASCENT",
     "RECOVER THE CAPSULE",
     1080,
     true,
     3,
     {{"BOOSTER", 34, 68, 8.00f, 7.45f, C_ORANGE},
      {"CORE", 26, 52, 6.55f, 6.30f, C_BLUE},
      {"UPPER", 19, 39, 4.95f, 5.30f, C_GREEN},
      {}}},

    {"H-340",
     "HEAVY ASCENT",
     "FOUR-STAGE FLIGHT",
     "REACH DEEP SPACE",
     1540,
     false,
     4,
     {{"BOOSTER", 35, 58, 8.20f, 7.65f, C_ORANGE},
      {"CORE", 29, 46, 6.80f, 6.50f, C_BLUE},
      {"UPPER", 23, 36, 5.10f, 5.55f, C_GREEN},
      {"KICK", 17, 28, 3.85f, 4.85f, C_PURPLE}}}};

static constexpr uint8_t MISSION_COUNT = sizeof(MISSIONS) / sizeof(MISSIONS[0]);

// =============================================================================
// Runtime types
// =============================================================================

enum class ScreenMode : uint8_t { SPLASH, BRIEFING, FLIGHT, RESULT };

enum class FlightPhase : uint8_t {
  COUNTDOWN,
  BURNING,
  COASTING,
  DESCENDING,
  EXPLOSION,
  DEPLOYMENT,
  RECOVERY
};

enum class ParticleType : uint8_t { SMOKE, FIRE, SPARK };

enum class FragmentType : uint8_t {
  WHOLE_STAGE,
  STAGE_SLICE,
  SIDE_PANEL,
  ENGINE,
  CAPSULE
};

struct ButtonEvents {
  bool pressed = false;
  bool released = false;
  bool tapped = false;
  bool held = false;
};

struct OneButton {
  bool stableDown = false;
  bool rawDown = false;
  bool holdSent = false;

  uint32_t changedAt = 0;
  uint32_t pressedAt = 0;

  ButtonEvents events;

  void begin() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    stableDown = digitalRead(PIN_BUTTON) == LOW;

    rawDown = stableDown;
    changedAt = millis();
  }

  void clearEvents() {
    events.pressed = false;
    events.released = false;
    events.tapped = false;
    events.held = false;
  }

  void update(uint32_t now) {
    bool reading = digitalRead(PIN_BUTTON) == LOW;

    if (reading != rawDown) {
      rawDown = reading;
      changedAt = now;
    }

    if (reading != stableDown && now - changedAt >= 22) {
      stableDown = reading;

      if (stableDown) {
        pressedAt = now;
        holdSent = false;
        events.pressed = true;
      } else {
        events.released = true;

        if (!holdSent && now - pressedAt < 580) {
          events.tapped = true;
        }
      }
    }

    if (stableDown && !holdSent && now - pressedAt >= 580) {
      holdSent = true;
      events.held = true;
    }
  }
};

struct Particle {
  bool active = false;

  ParticleType type = ParticleType::SMOKE;

  float x = 0.0f;
  float y = 0.0f;

  float velocityX = 0.0f;
  float velocityY = 0.0f;

  float life = 0.0f;
  float maximumLife = 1.0f;

  float size = 1.0f;

  uint16_t colour = C_WHITE;
};

struct Fragment {
  bool active = false;

  FragmentType type = FragmentType::STAGE_SLICE;

  float x = 0.0f;
  float y = 0.0f;

  float velocityX = 0.0f;
  float velocityY = 0.0f;

  float rotation = 0.0f;
  float spin = 0.0f;

  float width = 0.0f;
  float height = 0.0f;

  float life = 0.0f;

  uint16_t bodyColour = C_PAPER;
  uint16_t bandColour = C_ORANGE;

  float fuelFraction = 0.0f;
  int8_t bandY = -1;
};

struct Star {
  int16_t x = 0;
  int16_t y = 0;

  uint8_t size = 1;
  float parallax = 1.0f;
};

struct Cloud {
  int16_t x = 0;
  float worldAltitude = 0.0f;

  uint8_t size = 10;
  uint8_t layer = 0;
};

struct StageGeometry {
  float x = 0.0f;
  float y = 0.0f;

  float width = 0.0f;
  float height = 0.0f;

  uint8_t sourceIndex = 0;
};

struct RocketGeometry {
  float noseY = 0.0f;
  float bottomY = 0.0f;

  float payloadX = 0.0f;
  float payloadY = 0.0f;
  float payloadWidth = 0.0f;
  float payloadHeight = 0.0f;

  uint8_t stageCount = 0;
  StageGeometry stages[4];
};

struct PointF {
  float x;
  float y;
};

struct GameState {
  ScreenMode screen = ScreenMode::SPLASH;

  FlightPhase phase = FlightPhase::COUNTDOWN;

  uint32_t screenStartedAt = 0;
  uint32_t phaseStartedAt = 0;

  uint8_t selectedMission = 0;
  uint8_t unlockedMissions = 1;
  uint8_t activeStage = 0;

  float stageElapsed = 0.0f;

  float altitude = 0.0f;
  float maximumAltitude = 0.0f;
  float velocity = 0.0f;

  float launchNoseY = 0.0f;
  float trackingNoseY = 0.0f;

  float cameraAltitude = 0.0f;
  float previousCameraAltitude = 0.0f;

  float shake = 0.0f;
  float flash = 0.0f;

  float feedbackRemaining = 0.0f;
  uint16_t feedbackColour = C_WHITE;

  int32_t score = 0;
  int32_t bestScores[MISSION_COUNT] = {};

  bool success = false;

  char feedback[20] = {};
  char failureReason[28] = {};

  float recoveryY = -35.0f;
  float parachuteOpen = 0.0f;
  float deploymentY = 165.0f;

  float explosionX = 86.0f;
  float explosionY = 150.0f;
};

static OneButton button;
static GameState game;

static constexpr uint8_t MAX_PARTICLES = 112;
static constexpr uint8_t MAX_FRAGMENTS = 44;

static Particle particles[MAX_PARTICLES];
static Fragment fragments[MAX_FRAGMENTS];

static Star stars[62];
static Cloud clouds[18];

static uint32_t lastFrameAt = 0;

// =============================================================================
// Text helpers
// =============================================================================

static void configureText(uint8_t size, uint16_t colour) {
  frame.setTextWrap(false);
  frame.setTextSize(size);
  frame.setTextColor(colour);
}

static int16_t textPixelWidth(const char *text, uint8_t size) {
  return (int16_t)(strlen(text) * 6 * size);
}

static void drawTextLeft(const char *text, int16_t x, int16_t y, uint8_t size,
                         uint16_t colour) {
  configureText(size, colour);

  frame.setCursor(x, y);
  frame.print(text);
}

static void drawTextCentered(const char *text, int16_t y, uint8_t size,
                             uint16_t colour) {
  configureText(size, colour);

  int16_t width = textPixelWidth(text, size);

  frame.setCursor((SCREEN_W - width) / 2, y);

  frame.print(text);
}

static void drawTextRight(const char *text, int16_t rightX, int16_t y,
                          uint8_t size, uint16_t colour) {
  configureText(size, colour);

  frame.setCursor(rightX - textPixelWidth(text, size), y);

  frame.print(text);
}

// =============================================================================
// Rotated drawing helpers
// =============================================================================

static PointF rotatePoint(float localX, float localY, float centreX,
                          float centreY, float angle) {
  float cosine = cosf(angle);
  float sine = sinf(angle);

  PointF point;

  point.x = centreX + localX * cosine - localY * sine;

  point.y = centreY + localX * sine + localY * cosine;

  return point;
}

static void fillRotatedRectangle(float centreX, float centreY, float width,
                                 float height, float angle, uint16_t colour) {
  float halfWidth = width * 0.5f;
  float halfHeight = height * 0.5f;

  PointF first = rotatePoint(-halfWidth, -halfHeight, centreX, centreY, angle);

  PointF second = rotatePoint(halfWidth, -halfHeight, centreX, centreY, angle);

  PointF third = rotatePoint(halfWidth, halfHeight, centreX, centreY, angle);

  PointF fourth = rotatePoint(-halfWidth, halfHeight, centreX, centreY, angle);

  frame.fillTriangle((int16_t)first.x, (int16_t)first.y, (int16_t)second.x,
                     (int16_t)second.y, (int16_t)third.x, (int16_t)third.y,
                     colour);

  frame.fillTriangle((int16_t)first.x, (int16_t)first.y, (int16_t)third.x,
                     (int16_t)third.y, (int16_t)fourth.x, (int16_t)fourth.y,
                     colour);
}

static void drawRotatedRectangleOutline(float centreX, float centreY,
                                        float width, float height, float angle,
                                        uint16_t colour) {
  float halfWidth = width * 0.5f;
  float halfHeight = height * 0.5f;

  PointF first = rotatePoint(-halfWidth, -halfHeight, centreX, centreY, angle);

  PointF second = rotatePoint(halfWidth, -halfHeight, centreX, centreY, angle);

  PointF third = rotatePoint(halfWidth, halfHeight, centreX, centreY, angle);

  PointF fourth = rotatePoint(-halfWidth, halfHeight, centreX, centreY, angle);

  frame.drawLine((int16_t)first.x, (int16_t)first.y, (int16_t)second.x,
                 (int16_t)second.y, colour);

  frame.drawLine((int16_t)second.x, (int16_t)second.y, (int16_t)third.x,
                 (int16_t)third.y, colour);

  frame.drawLine((int16_t)third.x, (int16_t)third.y, (int16_t)fourth.x,
                 (int16_t)fourth.y, colour);

  frame.drawLine((int16_t)fourth.x, (int16_t)fourth.y, (int16_t)first.x,
                 (int16_t)first.y, colour);
}

// =============================================================================
// Persistence and campaign progression
// =============================================================================

static void loadProgress() {
  preferences.begin("voidascent", false);

  game.unlockedMissions = preferences.getUChar("unlocked", 1);

  if (game.unlockedMissions < 1) {
    game.unlockedMissions = 1;
  }

  if (game.unlockedMissions > MISSION_COUNT) {
    game.unlockedMissions = MISSION_COUNT;
  }

  uint8_t savedMission = preferences.getUChar("current", 0);

  if (savedMission >= game.unlockedMissions) {
    savedMission = game.unlockedMissions - 1;
  }

  if (savedMission >= MISSION_COUNT) {
    savedMission = MISSION_COUNT - 1;
  }

  game.selectedMission = savedMission;

  for (uint8_t index = 0; index < MISSION_COUNT; index++) {
    char key[10];

    snprintf(key, sizeof(key), "best%u", index);

    game.bestScores[index] = preferences.getInt(key, 0);
  }
}

static void saveMissionProgress() {
  uint8_t missionIndex = game.selectedMission;

  if (game.score > game.bestScores[missionIndex]) {
    game.bestScores[missionIndex] = game.score;

    char key[10];

    snprintf(key, sizeof(key), "best%u", missionIndex);

    preferences.putInt(key, game.score);
  }

  if (!game.success) {
    preferences.putUChar("current", game.selectedMission);

    return;
  }

  if (missionIndex + 1 < MISSION_COUNT) {
    uint8_t nextMission = missionIndex + 1;

    uint8_t requiredUnlockCount = nextMission + 1;

    if (requiredUnlockCount > game.unlockedMissions) {
      game.unlockedMissions = requiredUnlockCount;

      preferences.putUChar("unlocked", game.unlockedMissions);
    }

    // Resume on the newly unlocked mission after reboot.
    preferences.putUChar("current", nextMission);
  } else {
    preferences.putUChar("current", missionIndex);
  }
}

// =============================================================================
// Scene initialization
// =============================================================================

static void initializeSceneSeeds() {
  randomGenerator.state = 0x7F4A7C15UL;

  for (uint8_t index = 0; index < 62; index++) {
    stars[index].x = randomGenerator.rangeInt(2, SCREEN_W - 3);

    stars[index].y = randomGenerator.rangeInt(0, SCREEN_H - 1);

    stars[index].size = randomGenerator.chance(0.13f) ? 2 : 1;

    stars[index].parallax = randomGenerator.range(0.23f, 1.0f);
  }

  for (uint8_t index = 0; index < 18; index++) {
    clouds[index].x = randomGenerator.rangeInt(-18, SCREEN_W + 18);

    clouds[index].worldAltitude =
        13.0f + index * 9.5f + randomGenerator.range(-3.5f, 3.5f);

    clouds[index].size = randomGenerator.rangeInt(8, 17);

    clouds[index].layer = index % 3;
  }
}

// =============================================================================
// Particle system
// =============================================================================

static Particle *allocateParticle() {
  for (uint8_t index = 0; index < MAX_PARTICLES; index++) {
    if (!particles[index].active) {
      particles[index].active = true;
      return &particles[index];
    }
  }

  uint8_t replacement = (uint8_t)randomGenerator.rangeInt(0, MAX_PARTICLES - 1);

  particles[replacement].active = true;

  return &particles[replacement];
}

static void spawnParticle(ParticleType type, float x, float y, float velocityX,
                          float velocityY, float life, float size,
                          uint16_t colour) {
  Particle *particle = allocateParticle();

  particle->type = type;

  particle->x = x;
  particle->y = y;

  particle->velocityX = velocityX;
  particle->velocityY = velocityY;

  particle->life = life;
  particle->maximumLife = life;

  particle->size = size;
  particle->colour = colour;
}

static void spawnSmokeBurst(float x, float y, uint8_t count, float speed) {
  for (uint8_t index = 0; index < count; index++) {
    float angle = randomGenerator.range(0.0f, 6.2831853f);

    float magnitude = randomGenerator.range(speed * 0.25f, speed);

    spawnParticle(ParticleType::SMOKE, x, y, cosf(angle) * magnitude,
                  sinf(angle) * magnitude, randomGenerator.range(0.7f, 1.8f),
                  randomGenerator.range(2.0f, 5.0f),
                  randomGenerator.chance(0.45f) ? C_SMOKE : C_SMOKE_DARK);
  }
}

static void spawnFireBurst(float x, float y, uint8_t count, float speed) {
  for (uint8_t index = 0; index < count; index++) {
    float angle = randomGenerator.range(0.0f, 6.2831853f);

    float magnitude = randomGenerator.range(speed * 0.3f, speed);

    uint16_t colour;

    int choice = randomGenerator.rangeInt(0, 2);

    if (choice == 0) {
      colour = C_YELLOW;
    } else if (choice == 1) {
      colour = C_ORANGE;
    } else {
      colour = C_RED;
    }

    spawnParticle(ParticleType::FIRE, x, y, cosf(angle) * magnitude,
                  sinf(angle) * magnitude, randomGenerator.range(0.25f, 0.85f),
                  randomGenerator.range(1.5f, 3.8f), colour);
  }
}

static void spawnSparkBurst(float x, float y, uint8_t count, uint16_t colour) {
  for (uint8_t index = 0; index < count; index++) {
    float angle = randomGenerator.range(0.0f, 6.2831853f);

    float speed = randomGenerator.range(30.0f, 115.0f);

    spawnParticle(ParticleType::SPARK, x, y, cosf(angle) * speed,
                  sinf(angle) * speed, randomGenerator.range(0.25f, 0.9f),
                  randomGenerator.range(1.0f, 2.0f), colour);
  }
}

static void clearParticles() {
  for (uint8_t index = 0; index < MAX_PARTICLES; index++) {
    particles[index].active = false;
  }
}

static void updateParticles(float deltaSeconds) {
  for (uint8_t index = 0; index < MAX_PARTICLES; index++) {
    Particle &particle = particles[index];

    if (!particle.active) {
      continue;
    }

    particle.life -= deltaSeconds;

    if (particle.life <= 0.0f) {
      particle.active = false;
      continue;
    }

    particle.x += particle.velocityX * deltaSeconds;

    particle.y += particle.velocityY * deltaSeconds;

    if (particle.type == ParticleType::SMOKE) {
      particle.velocityX *= 0.975f;
      particle.velocityY *= 0.965f;

      particle.velocityY -= 2.0f * deltaSeconds;
    } else {
      particle.velocityY += 27.0f * deltaSeconds;
    }
  }
}

static void drawParticles() {
  for (uint8_t index = 0; index < MAX_PARTICLES; index++) {
    const Particle &particle = particles[index];

    if (!particle.active) {
      continue;
    }

    float remaining = clamp01(particle.life / particle.maximumLife);

    int16_t x = (int16_t)roundf(particle.x);

    int16_t y = (int16_t)roundf(particle.y);

    if (particle.type == ParticleType::SMOKE) {
      float expansion = 1.0f + (1.0f - remaining) * 1.8f;

      int16_t radius = (int16_t)roundf(particle.size * expansion);

      radius = maxInt16((int16_t)1, radius);

      uint16_t smokeColour = blend565(C_SMOKE_DARK, particle.colour,
                                      (uint8_t)(remaining * 220.0f));

      frame.fillCircle(x, y, radius, smokeColour);

      if (radius > 2) {
        frame.fillCircle(x - radius / 3, y - radius / 4, radius / 2,
                         blend565(smokeColour, C_PAPER, 40));
      }
    } else if (particle.type == ParticleType::FIRE) {
      int16_t radius = (int16_t)roundf(particle.size * remaining);

      radius = maxInt16((int16_t)1, radius);

      frame.fillCircle(x, y, radius + 1, C_RED);

      frame.fillCircle(x, y, radius, particle.colour);

      if (remaining > 0.55f) {
        frame.drawPixel(x, y, C_WHITE);
      }
    } else {
      int16_t tailX = x - (int16_t)(particle.velocityX * 0.035f);

      int16_t tailY = y - (int16_t)(particle.velocityY * 0.035f);

      frame.drawLine(x, y, tailX, tailY, particle.colour);
    }
  }
}

// =============================================================================
// Fragment system
// =============================================================================

static Fragment *allocateFragment() {
  for (uint8_t index = 0; index < MAX_FRAGMENTS; index++) {
    if (!fragments[index].active) {
      fragments[index].active = true;
      return &fragments[index];
    }
  }

  uint8_t replacement = (uint8_t)randomGenerator.rangeInt(0, MAX_FRAGMENTS - 1);

  fragments[replacement].active = true;

  return &fragments[replacement];
}

static void createFragment(FragmentType type, float x, float y, float width,
                           float height, float velocityX, float velocityY,
                           float rotation, float spin, float life,
                           uint16_t bandColour, float fuelFraction,
                           int8_t bandY) {
  Fragment *fragment = allocateFragment();

  fragment->type = type;

  fragment->x = x;
  fragment->y = y;

  fragment->width = width;
  fragment->height = height;

  fragment->velocityX = velocityX;
  fragment->velocityY = velocityY;

  fragment->rotation = rotation;
  fragment->spin = spin;

  fragment->life = life;

  fragment->bodyColour = C_PAPER;
  fragment->bandColour = bandColour;

  fragment->fuelFraction = clamp01(fuelFraction);

  fragment->bandY = bandY;
}

static void clearFragments() {
  for (uint8_t index = 0; index < MAX_FRAGMENTS; index++) {
    fragments[index].active = false;
  }
}

static void updateFragments(float deltaSeconds) {
  for (uint8_t index = 0; index < MAX_FRAGMENTS; index++) {
    Fragment &fragment = fragments[index];

    if (!fragment.active) {
      continue;
    }

    fragment.life -= deltaSeconds;

    if (fragment.life <= 0.0f || fragment.y > SCREEN_H + 130.0f) {
      fragment.active = false;
      continue;
    }

    fragment.velocityY += 35.0f * deltaSeconds;

    fragment.x += fragment.velocityX * deltaSeconds;

    fragment.y += fragment.velocityY * deltaSeconds;

    fragment.rotation += fragment.spin * deltaSeconds;
  }
}

static void drawFragmentFuelWindow(const Fragment &fragment) {
  if (fragment.type == FragmentType::SIDE_PANEL ||
      fragment.type == FragmentType::ENGINE ||
      fragment.type == FragmentType::CAPSULE) {
    return;
  }

  float windowHeight = maxFloat(4.0f, fragment.height - 6.0f);

  fillRotatedRectangle(fragment.x, fragment.y, 5.0f, windowHeight,
                       fragment.rotation, C_GLASS_EDGE);

  fillRotatedRectangle(fragment.x, fragment.y, 3.0f, windowHeight - 2.0f,
                       fragment.rotation, C_GLASS);

  float fuelHeight = (windowHeight - 2.0f) * fragment.fuelFraction;

  if (fuelHeight > 0.5f) {
    float localCentreY = windowHeight * 0.5f - fuelHeight * 0.5f - 1.0f;

    PointF fuelCentre = rotatePoint(0.0f, localCentreY, fragment.x, fragment.y,
                                    fragment.rotation);

    fillRotatedRectangle(fuelCentre.x, fuelCentre.y, 3.0f, fuelHeight,
                         fragment.rotation, C_FUEL);
  }
}

static void drawFragment(const Fragment &fragment) {
  if (fragment.type == FragmentType::CAPSULE) {
    fillRotatedRectangle(fragment.x, fragment.y + 2.0f, fragment.width - 4.0f,
                         fragment.height - 7.0f, fragment.rotation, C_PAPER);

    PointF dome = rotatePoint(0.0f, -fragment.height * 0.34f, fragment.x,
                              fragment.y, fragment.rotation);

    int16_t domeRadius = (int16_t)(fragment.width * 0.35f);

    domeRadius = maxInt16((int16_t)3, domeRadius);

    frame.fillCircle((int16_t)dome.x, (int16_t)dome.y, domeRadius, C_PAPER);

    PointF band = rotatePoint(0.0f, fragment.height * 0.30f, fragment.x,
                              fragment.y, fragment.rotation);

    fillRotatedRectangle(band.x, band.y, fragment.width - 5.0f, 3.0f,
                         fragment.rotation, fragment.bandColour);

    return;
  }

  if (fragment.type == FragmentType::ENGINE) {
    fillRotatedRectangle(fragment.x, fragment.y, fragment.width,
                         fragment.height, fragment.rotation, C_METAL_DARK);

    drawRotatedRectangleOutline(fragment.x, fragment.y, fragment.width,
                                fragment.height, fragment.rotation, C_METAL);

    return;
  }

  uint16_t bodyColour =
      fragment.type == FragmentType::SIDE_PANEL ? C_METAL : fragment.bodyColour;

  fillRotatedRectangle(fragment.x + 1.0f, fragment.y + 1.0f, fragment.width,
                       fragment.height, fragment.rotation, C_BLACK);

  fillRotatedRectangle(fragment.x, fragment.y, fragment.width, fragment.height,
                       fragment.rotation, bodyColour);

  drawRotatedRectangleOutline(fragment.x, fragment.y, fragment.width,
                              fragment.height, fragment.rotation, C_METAL_DARK);

  if (fragment.bandY >= 0) {
    float localBandY = -fragment.height * 0.5f + fragment.bandY + 1.5f;

    PointF bandCentre = rotatePoint(0.0f, localBandY, fragment.x, fragment.y,
                                    fragment.rotation);

    fillRotatedRectangle(bandCentre.x, bandCentre.y, fragment.width, 3.0f,
                         fragment.rotation, fragment.bandColour);
  }

  drawFragmentFuelWindow(fragment);
}

static void drawFragments() {
  for (uint8_t index = 0; index < MAX_FRAGMENTS; index++) {
    if (fragments[index].active) {
      drawFragment(fragments[index]);
    }
  }
}

// =============================================================================
// Rocket geometry
// =============================================================================

static const MissionDefinition &currentMission() {
  return MISSIONS[game.selectedMission];
}

static const StageDefinition *currentStage() {
  const MissionDefinition &mission = currentMission();

  if (game.activeStage >= mission.stageCount) {
    return nullptr;
  }

  return &mission.stages[game.activeStage];
}

static float payloadHeight(const MissionDefinition &mission) {
  return mission.recoverCapsule ? 22.0f : 20.0f;
}

static float remainingRocketHeight(const MissionDefinition &mission,
                                   uint8_t activeStage) {
  float height = payloadHeight(mission);

  bool first = true;

  for (int8_t index = (int8_t)mission.stageCount - 1;
       index >= (int8_t)activeStage; index--) {
    if (!first) {
      height += 2.0f;
    }

    first = false;

    height += mission.stages[index].height;
  }

  return height;
}

static void buildRocketGeometryFor(const MissionDefinition &mission,
                                   uint8_t activeStage, float noseY,
                                   RocketGeometry &geometry) {
  geometry.noseY = noseY;

  geometry.payloadWidth = 20.0f;
  geometry.payloadHeight = payloadHeight(mission);

  geometry.payloadX = SCREEN_W * 0.5f - geometry.payloadWidth * 0.5f;

  geometry.payloadY = noseY;

  float y = noseY + geometry.payloadHeight;

  geometry.stageCount = 0;

  for (int8_t index = (int8_t)mission.stageCount - 1;
       index >= (int8_t)activeStage; index--) {
    if (geometry.stageCount >= 4) {
      break;
    }

    if (geometry.stageCount > 0) {
      y += 2.0f;
    }

    const StageDefinition &stage = mission.stages[index];

    StageGeometry &stageGeometry = geometry.stages[geometry.stageCount];

    geometry.stageCount++;

    stageGeometry.width = stage.width;

    stageGeometry.height = stage.height;

    stageGeometry.x = SCREEN_W * 0.5f - stage.width * 0.5f;

    stageGeometry.y = y;
    stageGeometry.sourceIndex = (uint8_t)index;

    y += stage.height;
  }

  geometry.bottomY = y;
}

static float currentRocketNoseY() {
  float relativeAltitude = game.altitude - game.cameraAltitude;

  return game.launchNoseY - relativeAltitude * WORLD_PIXELS_PER_ALTITUDE;
}

static void buildCurrentRocketGeometry(RocketGeometry &geometry) {
  buildRocketGeometryFor(currentMission(), game.activeStage,
                         currentRocketNoseY(), geometry);
}

static float currentFuelFraction() {
  const StageDefinition *stage = currentStage();

  if (stage == nullptr) {
    return 0.0f;
  }

  return clamp01(1.0f - game.stageElapsed / stage->burnSeconds);
}

// =============================================================================
// Shared world camera
// =============================================================================

static void shiftWorldEffectsForCamera(float downwardPixels) {
  if (downwardPixels <= 0.0f) {
    return;
  }

  for (uint8_t index = 0; index < MAX_PARTICLES; index++) {
    if (particles[index].active) {
      particles[index].y += downwardPixels;
    }
  }

  for (uint8_t index = 0; index < MAX_FRAGMENTS; index++) {
    if (fragments[index].active) {
      fragments[index].y += downwardPixels;
    }
  }
}

static void updateWorldCamera(float deltaSeconds) {
  game.previousCameraAltitude = game.cameraAltitude;

  bool cameraCanMove =
      game.phase == FlightPhase::BURNING || game.phase == FlightPhase::COASTING;

  if (!cameraCanMove) {
    return;
  }

  float freeRisePixels = game.launchNoseY - game.trackingNoseY;

  if (freeRisePixels < 0.0f) {
    freeRisePixels = 0.0f;
  }

  float cameraStartAltitude = freeRisePixels / WORLD_PIXELS_PER_ALTITUDE;

  float desiredCameraAltitude = game.altitude - cameraStartAltitude;

  if (desiredCameraAltitude < 0.0f) {
    desiredCameraAltitude = 0.0f;
  }

  if (desiredCameraAltitude > game.cameraAltitude) {
    float followAmount = 1.0f - expf(-CAMERA_FOLLOW_SPEED * deltaSeconds);

    float nextCameraAltitude =
        game.cameraAltitude +
        (desiredCameraAltitude - game.cameraAltitude) * followAmount;

    float remainingLag = desiredCameraAltitude - nextCameraAltitude;

    if (remainingLag > CAMERA_MAX_LAG_ALTITUDE) {
      nextCameraAltitude = desiredCameraAltitude - CAMERA_MAX_LAG_ALTITUDE;
    }

    if (nextCameraAltitude < game.cameraAltitude) {
      nextCameraAltitude = game.cameraAltitude;
    }

    game.cameraAltitude = nextCameraAltitude;
  }

  float cameraMovementPixels =
      (game.cameraAltitude - game.previousCameraAltitude) *
      WORLD_PIXELS_PER_ALTITUDE;

  shiftWorldEffectsForCamera(cameraMovementPixels);
}

// =============================================================================
// Sky and atmosphere
// =============================================================================

static uint16_t skyTopColour(float altitude) {
  float blueTransition = smoothStep(0.0f, 125.0f, altitude);

  float spaceTransition = smoothStep(105.0f, 520.0f, altitude);

  uint16_t dawn = C565(177, 91, 84);

  uint16_t blue = C565(37, 85, 158);

  uint16_t upper = C565(5, 8, 23);

  uint16_t firstBlend =
      blend565(dawn, blue, (uint8_t)(blueTransition * 255.0f));

  return blend565(firstBlend, upper, (uint8_t)(spaceTransition * 255.0f));
}

static uint16_t skyBottomColour(float altitude) {
  float blueTransition = smoothStep(0.0f, 150.0f, altitude);

  float spaceTransition = smoothStep(145.0f, 600.0f, altitude);

  uint16_t horizon = C565(218, 126, 88);

  uint16_t blue = C565(64, 125, 189);

  uint16_t upper = C565(8, 12, 30);

  uint16_t firstBlend =
      blend565(horizon, blue, (uint8_t)(blueTransition * 255.0f));

  return blend565(firstBlend, upper, (uint8_t)(spaceTransition * 255.0f));
}

static uint16_t skyColourAtY(float altitude, int16_t y) {
  float verticalAmount =
      clampFloat((float)y / (float)(SCREEN_H - 1), 0.0f, 1.0f);

  return blend565(skyTopColour(altitude), skyBottomColour(altitude),
                  (uint8_t)(verticalAmount * 255.0f));
}

static void drawSkyGradient(float altitude) {
  static constexpr int16_t STRIP_HEIGHT = 5;

  for (int16_t y = 0; y < SCREEN_H; y += STRIP_HEIGHT) {
    frame.fillRect(0, y, SCREEN_W, STRIP_HEIGHT,
                   skyColourAtY(altitude, y + STRIP_HEIGHT / 2));
  }
}

static void drawStars(float altitude) {
  for (uint8_t index = 0; index < 62; index++) {
    const Star &star = stars[index];

    float revealStart =
        58.0f + (1.0f - star.parallax) * 110.0f + (index % 9) * 7.0f;

    float visibility = smoothStep(revealStart, revealStart + 190.0f, altitude);

    if (visibility <= 0.01f) {
      continue;
    }

    float movingY = star.y + altitude * star.parallax * 0.55f;

    movingY = fmodf(movingY, (float)SCREEN_H);

    if (movingY < 0.0f) {
      movingY += SCREEN_H;
    }

    int16_t screenY = (int16_t)movingY;

    uint16_t backgroundColour = skyColourAtY(altitude, screenY);

    uint16_t targetColour = star.parallax > 0.68f ? C_WHITE : C_MUTED;

    uint16_t starColour = blend565(backgroundColour, targetColour,
                                   (uint8_t)(visibility * 255.0f));

    frame.fillRect(star.x, screenY, star.size, star.size, starColour);
  }
}

static void drawSun(float altitude) {
  float visibility = 1.0f - smoothStep(100.0f, 320.0f, altitude);

  if (visibility <= 0.01f) {
    return;
  }

  int16_t y = 70 + (int16_t)(altitude * 0.48f);

  if (y > SCREEN_H + 20) {
    return;
  }

  int16_t sampledY = y;

  if (sampledY < 0) {
    sampledY = 0;
  }

  if (sampledY >= SCREEN_H) {
    sampledY = SCREEN_H - 1;
  }

  uint16_t background = skyColourAtY(altitude, sampledY);

  uint16_t sunColour =
      blend565(background, C_YELLOW, (uint8_t)(visibility * 255.0f));

  frame.fillCircle(137, y, 12, sunColour);

  frame.drawFastHLine(
      125, y, 24,
      blend565(background, C_WHITE, (uint8_t)(visibility * 145.0f)));
}

static void drawCloudShape(int16_t centreX, int16_t centreY, int16_t size,
                           uint16_t colour) {
  frame.fillCircle(centreX - size / 2, centreY, size / 2, colour);

  frame.fillCircle(centreX, centreY - size / 4, size / 2, colour);

  frame.fillCircle(centreX + size / 2, centreY, size / 2, colour);

  frame.fillRect(centreX - size, centreY, size * 2,
                 maxInt16((int16_t)2, size / 2), colour);
}

static void drawClouds(float cameraAltitude, uint32_t now) {
  float atmosphereVisibility =
      1.0f - smoothStep(150.0f, 470.0f, cameraAltitude);

  for (uint8_t index = 0; index < 18; index++) {
    const Cloud &cloud = clouds[index];

    float layerScale =
        WORLD_PIXELS_PER_ALTITUDE * (0.52f + cloud.layer * 0.075f);

    float screenY =
        286.0f - (cloud.worldAltitude - cameraAltitude) * layerScale;

    if (screenY < -42.0f || screenY > SCREEN_H + 44.0f) {
      continue;
    }

    float topVisibility = smoothStep(-40.0f, 12.0f, screenY);

    float bottomVisibility =
        1.0f - smoothStep(SCREEN_H - 34.0f, SCREEN_H + 40.0f, screenY);

    float visibility = atmosphereVisibility * topVisibility * bottomVisibility;

    if (visibility <= 0.01f) {
      continue;
    }

    int16_t drift = (int16_t)(sinf(now * 0.00018f + index * 1.7f) * 4.0f);

    int16_t sampledY = (int16_t)screenY;

    if (sampledY < 0) {
      sampledY = 0;
    }

    if (sampledY >= SCREEN_H) {
      sampledY = SCREEN_H - 1;
    }

    uint16_t originalColour = cloud.layer == 0 ? C565(186, 198, 205) : C_CLOUD;

    uint16_t cloudColour =
        blend565(skyColourAtY(cameraAltitude, sampledY), originalColour,
                 (uint8_t)(visibility * 255.0f));

    drawCloudShape(cloud.x + drift, (int16_t)screenY, cloud.size, cloudColour);
  }
}

static void drawMountains(float cameraAltitude) {
  float farBase = 247.0f + cameraAltitude * 2.75f;

  float nearBase = 253.0f + cameraAltitude * 3.20f;

  if (farBase <= SCREEN_H + 90.0f) {
    uint16_t farColour = C565(47, 41, 64);

    frame.fillTriangle(0, (int16_t)farBase, 26, (int16_t)farBase - 38, 55,
                       (int16_t)farBase, farColour);

    frame.fillTriangle(34, (int16_t)farBase, 78, (int16_t)farBase - 56, 117,
                       (int16_t)farBase, farColour);

    frame.fillTriangle(92, (int16_t)farBase, 142, (int16_t)farBase - 46,
                       SCREEN_W, (int16_t)farBase, farColour);
  }

  if (nearBase <= SCREEN_H + 90.0f) {
    uint16_t nearColour = C565(28, 31, 46);

    frame.fillTriangle(0, (int16_t)nearBase + 20, 42, (int16_t)nearBase - 22,
                       84, (int16_t)nearBase + 20, nearColour);

    frame.fillTriangle(55, (int16_t)nearBase + 20, 107, (int16_t)nearBase - 31,
                       157, (int16_t)nearBase + 20, nearColour);

    frame.fillTriangle(120, (int16_t)nearBase + 20, 160, (int16_t)nearBase - 14,
                       SCREEN_W, (int16_t)nearBase + 20, nearColour);

    int16_t fillTop = (int16_t)nearBase;

    if (fillTop < 0) {
      fillTop = 0;
    }

    if (fillTop < SCREEN_H) {
      frame.fillRect(0, fillTop, SCREEN_W, SCREEN_H - fillTop, nearColour);
    }
  }
}

// =============================================================================
// Enhanced Earth rendering
// =============================================================================

static void drawEarthOceanSphere(int16_t centreX, int16_t centreY,
                                 int16_t radius, float surfaceVisibility) {
  int16_t startY = centreY - radius;

  int16_t endY = centreY + radius;

  if (startY < 0) {
    startY = 0;
  }

  if (endY >= SCREEN_H) {
    endY = SCREEN_H - 1;
  }

  for (int16_t y = startY; y <= endY; y++) {
    float deltaY = (float)(y - centreY);

    float inside = radius * radius - deltaY * deltaY;

    if (inside <= 0.0f) {
      continue;
    }

    int16_t halfWidth = (int16_t)sqrtf(inside);

    int16_t left = centreX - halfWidth;

    int16_t right = centreX + halfWidth;

    if (left < 0) {
      left = 0;
    }

    if (right >= SCREEN_W) {
      right = SCREEN_W - 1;
    }

    int16_t totalWidth = right - left + 1;

    if (totalWidth <= 0) {
      continue;
    }

    float verticalNormal =
        clampFloat((deltaY + radius) / (radius * 2.0f), 0.0f, 1.0f);

    uint16_t centralOcean =
        blend565(C_OCEAN_LIGHT, C_OCEAN, (uint8_t)(verticalNormal * 170.0f));

    centralOcean = blend565(C_OCEAN_DEEP, centralOcean,
                            (uint8_t)(surfaceVisibility * 255.0f));

    int16_t edgeWidth = maxInt16((int16_t)2, totalWidth / 6);

    int16_t middleWidth = totalWidth - edgeWidth * 2;

    if (middleWidth < 1) {
      middleWidth = 1;
    }

    uint16_t darkEdge = blend565(C_VOID, centralOcean, 135);

    uint16_t lightEdge = blend565(centralOcean, C_OCEAN_LIGHT, 80);

    frame.drawFastHLine(left, y, edgeWidth, darkEdge);

    frame.drawFastHLine(left + edgeWidth, y, middleWidth, centralOcean);

    frame.drawFastHLine(right - edgeWidth + 1, y, edgeWidth, lightEdge);
  }
}

static void drawEarthContinents(int16_t centreX, int16_t centreY,
                                int16_t radius, float visibility) {
  if (visibility <= 0.01f) {
    return;
  }

  uint16_t landDark =
      blend565(C_OCEAN, C_LAND_DARK, (uint8_t)(visibility * 255.0f));

  uint16_t land = blend565(C_OCEAN, C_LAND, (uint8_t)(visibility * 255.0f));

  uint16_t landLight =
      blend565(C_OCEAN, C_LAND_LIGHT, (uint8_t)(visibility * 220.0f));

  int16_t scale = radius / 10;

  // Left continent.
  frame.fillCircle(centreX - scale * 4, centreY - scale * 3, scale * 2, land);

  frame.fillCircle(centreX - scale * 3, centreY - scale, scale * 2, land);

  frame.fillTriangle(centreX - scale * 5, centreY - scale * 3, centreX - scale,
                     centreY - scale * 2, centreX - scale * 3,
                     centreY + scale * 2, land);

  frame.fillCircle(centreX - scale * 2, centreY + scale * 2, scale, landDark);

  // Right continent.
  frame.fillCircle(centreX + scale * 3, centreY - scale * 2, scale * 2,
                   landLight);

  frame.fillTriangle(centreX + scale, centreY - scale * 3, centreX + scale * 5,
                     centreY - scale, centreX + scale * 3, centreY + scale * 2,
                     landLight);

  frame.fillCircle(centreX + scale * 4, centreY + scale, scale, land);

  // Small islands.
  frame.fillCircle(centreX, centreY - scale * 4,
                   maxInt16((int16_t)1, scale / 2), landLight);

  frame.fillCircle(centreX + scale, centreY + scale * 3,
                   maxInt16((int16_t)1, scale / 2), land);
}

static void drawEarthCloudBands(int16_t centreX, int16_t centreY,
                                int16_t radius, float visibility) {
  if (visibility <= 0.05f) {
    return;
  }

  uint16_t cloudColour =
      blend565(C_OCEAN, C_WHITE, (uint8_t)(visibility * 145.0f));

  for (int8_t band = -3; band <= 3; band++) {
    int16_t y = centreY + band * radius / 7;

    float deltaY = (float)(y - centreY);

    float inside = radius * radius - deltaY * deltaY;

    if (inside <= 0.0f) {
      continue;
    }

    int16_t halfWidth = (int16_t)sqrtf(inside);

    int16_t left = centreX - halfWidth + 13 + (band & 1 ? 5 : 0);

    int16_t width = halfWidth * 2 - 27;

    if (width <= 8) {
      continue;
    }

    for (int16_t segment = 0; segment < width; segment += 24) {
      int16_t segmentWidth = minInt16((int16_t)14, (int16_t)(width - segment));

      if (segmentWidth > 2 && ((segment / 24 + band + 9) % 3) != 1) {
        frame.drawFastHLine(left + segment, y, segmentWidth, cloudColour);

        if ((segment / 24) & 1) {
          frame.drawFastHLine(left + segment + 3, y + 1,
                              maxInt16((int16_t)1, segmentWidth - 5),
                              cloudColour);
        }
      }
    }
  }
}

static void drawEarth(float altitude) {
  // Earth starts as a faint atmospheric glow and slowly rises.
  float reveal = smoothStep(105.0f, 720.0f, altitude);

  if (reveal <= 0.001f) {
    return;
  }

  static constexpr int16_t EARTH_RADIUS = 188;

  int16_t centreY = (int16_t)roundf(536.0f - reveal * 270.0f);

  int16_t earthTop = centreY - EARTH_RADIUS;

  if (earthTop > SCREEN_H + 10) {
    return;
  }

  int16_t sampledY = earthTop;

  if (sampledY < 0) {
    sampledY = 0;
  }

  if (sampledY >= SCREEN_H) {
    sampledY = SCREEN_H - 1;
  }

  uint16_t background = skyColourAtY(altitude, sampledY);

  float glowVisibility = smoothStep(0.0f, 0.34f, reveal);

  uint16_t outerGlow =
      blend565(background, C_ATMOSPHERE, (uint8_t)(glowVisibility * 165.0f));

  frame.drawCircle(SCREEN_W / 2, centreY, EARTH_RADIUS + 7, outerGlow);

  frame.drawCircle(SCREEN_W / 2, centreY, EARTH_RADIUS + 6, outerGlow);

  frame.drawCircle(SCREEN_W / 2, centreY, EARTH_RADIUS + 5, outerGlow);

  frame.drawCircle(SCREEN_W / 2, centreY, EARTH_RADIUS + 4, outerGlow);

  float surfaceVisibility = smoothStep(0.07f, 0.48f, reveal);

  drawEarthOceanSphere(SCREEN_W / 2, centreY, EARTH_RADIUS, surfaceVisibility);

  float continentVisibility = smoothStep(0.25f, 0.80f, reveal);

  drawEarthContinents(SCREEN_W / 2, centreY, EARTH_RADIUS, continentVisibility);

  float cloudVisibility = smoothStep(0.32f, 0.90f, reveal);

  drawEarthCloudBands(SCREEN_W / 2, centreY, EARTH_RADIUS, cloudVisibility);

  // Bright limb.
  uint16_t limbColour =
      blend565(C_OCEAN_LIGHT, C_WHITE, (uint8_t)(glowVisibility * 150.0f));

  frame.drawCircle(SCREEN_W / 2, centreY, EARTH_RADIUS, limbColour);
}

static void drawBackground(float visualAltitude, uint32_t now) {
  drawSkyGradient(visualAltitude);
  drawStars(visualAltitude);
  drawSun(visualAltitude);
  drawMountains(visualAltitude);
  drawClouds(visualAltitude, now);
  drawEarth(visualAltitude);
}

// =============================================================================
// Launchpad
// =============================================================================

static void drawLaunchLight(int16_t x, int16_t y, bool lit,
                            uint16_t activeColour) {
  frame.fillCircle(x, y, 2, lit ? activeColour : C_DARK_RED);

  if (lit) {
    frame.drawPixel(x, y - 2, C_WHITE);
  }
}

static void drawLaunchPad(float cameraAltitude, uint32_t now, bool armed) {
  float padYFloat = 274.0f + cameraAltitude * WORLD_PIXELS_PER_ALTITUDE;

  if (padYFloat > SCREEN_H + 110.0f) {
    return;
  }

  int16_t padY = (int16_t)padYFloat;

  bool slowBlink = ((now / 480) & 1U) == 0;

  bool fastBlink = ((now / 210) & 1U) == 0;

  uint8_t chase = (uint8_t)((now / 135) % 6);

  frame.fillRect(0, padY + 24, SCREEN_W, 90, C_GROUND);

  // Flame trench.
  frame.fillRect(53, padY + 11, 66, 20, C_BLACK);

  frame.fillTriangle(56, padY + 12, 72, padY + 31, 72, padY + 12, C_METAL_DARK);

  frame.fillTriangle(116, padY + 12, 100, padY + 31, 100, padY + 12,
                     C_METAL_DARK);

  // Main platform.
  frame.fillRect(45, padY + 3, 82, 10, C_CONCRETE);

  frame.fillRect(55, padY - 4, 62, 8, C565(124, 101, 83));

  frame.fillRect(65, padY - 10, 42, 7, C_METAL_DARK);

  for (int16_t support = 0; support < 5; support++) {
    frame.fillRect(50 + support * 17, padY + 13, 5, 17, C565(42, 35, 34));
  }

  static constexpr int16_t TOWER_LEFT = 24;
  static constexpr int16_t TOWER_RIGHT = 48;

  frame.fillRect(TOWER_LEFT, padY - 157, 5, 157, C_TOWER);

  frame.fillRect(TOWER_RIGHT, padY - 140, 4, 140, C_TOWER);

  for (int16_t y = padY - 150; y < padY - 6; y += 13) {
    frame.drawFastHLine(TOWER_LEFT - 2, y, 31, C_TOWER_LIGHT);

    frame.drawLine(TOWER_LEFT, y, TOWER_RIGHT + 3, y + 11, C_TOWER_LIGHT);

    frame.drawLine(TOWER_RIGHT + 3, y, TOWER_LEFT, y + 11, C_TOWER_LIGHT);
  }

  // Service arms.
  frame.fillRect(TOWER_RIGHT, padY - 126, 34, 4, C_TOWER_LIGHT);

  frame.fillRect(78, padY - 130, 4, 15, C_TOWER_LIGHT);

  frame.fillRect(78, padY - 119, 10, 4, C_METAL_DARK);

  frame.fillRect(TOWER_RIGHT, padY - 77, 31, 4, C_TOWER_LIGHT);

  frame.fillRect(75, padY - 81, 4, 15, C_TOWER_LIGHT);

  frame.fillRect(75, padY - 70, 12, 4, C_METAL_DARK);

  // Tower lights.
  frame.fillRect(TOWER_LEFT - 1, padY - 166, 8, 8, C_METAL_DARK);

  drawLaunchLight(TOWER_LEFT + 3, padY - 168, slowBlink, C_RED);

  drawLaunchLight(TOWER_LEFT + 3, padY - 116, fastBlink, C_RED);

  drawLaunchLight(TOWER_LEFT + 3, padY - 64, !fastBlink, C_ORANGE);

  // Pad clamps.
  frame.fillRect(68, padY - 15, 8, 8, C_METAL_DARK);

  frame.fillRect(96, padY - 15, 8, 8, C_METAL_DARK);

  frame.drawLine(72, padY - 15, 81, padY - 24, C_METAL);

  frame.drawLine(100, padY - 15, 91, padY - 24, C_METAL);

  // Sequencing lights.
  for (uint8_t light = 0; light < 6; light++) {
    bool lit;

    if (armed) {
      lit = light == chase;
    } else {
      lit = ((light + (now / 380)) & 1U) != 0;
    }

    drawLaunchLight(59 + light * 11, padY + 1, lit, armed ? C_YELLOW : C_CYAN);
  }

  // Floodlights.
  frame.drawLine(10, padY + 22, 10, padY - 26, C_METAL);

  frame.drawLine(162, padY + 22, 162, padY - 24, C_METAL);

  frame.fillRect(6, padY - 31, 9, 6, C_METAL_DARK);

  frame.fillRect(157, padY - 29, 9, 6, C_METAL_DARK);

  if (fastBlink) {
    frame.fillTriangle(14, padY - 28, 36, padY - 18, 14, padY - 22,
                       C565(255, 226, 144));
  } else {
    frame.fillTriangle(157, padY - 26, 136, padY - 17, 157, padY - 20,
                       C565(255, 226, 144));
  }
}

// =============================================================================
// Rocket drawing
// =============================================================================

static void drawPayload(const RocketGeometry &geometry, bool recoveryCapsule) {
  int16_t x = (int16_t)roundf(geometry.payloadX);

  int16_t y = (int16_t)roundf(geometry.payloadY);

  int16_t width = (int16_t)roundf(geometry.payloadWidth);

  int16_t height = (int16_t)roundf(geometry.payloadHeight);

  int16_t centreX = x + width / 2;

  frame.fillCircle(centreX, y + 8, width / 2 - 2, C_PAPER);

  frame.fillRect(x + 2, y + 8, width - 4, height - 8, C_PAPER);

  frame.fillRect(x + 3, y + 7, 3, height - 10, C_HIGHLIGHT);

  frame.fillRect(x + width - 5, y + 8, 2, height - 10, C_METAL);

  frame.fillRoundRect(centreX - 4, y + 8, 8, 6, 2, C_GLASS);

  frame.drawPixel(centreX - 2, y + 9, C_CYAN);

  frame.drawPixel(centreX - 1, y + 9, C_WHITE);

  frame.fillRect(x + 3, y + height - 5, width - 6, 4,
                 recoveryCapsule ? C_CYAN : C_PURPLE);

  frame.drawFastHLine(x + 2, y + height - 1, width - 4, C_METAL_DARK);
}

static void drawFuelWindow(int16_t stageX, int16_t stageY, int16_t stageWidth,
                           int16_t stageHeight, float fuelFraction, bool active,
                           uint32_t now) {
  int16_t windowX = stageX + stageWidth / 2 - 3;

  int16_t windowY = stageY + 8;

  int16_t windowHeight = stageHeight - 17;

  windowHeight = maxInt16((int16_t)8, windowHeight);

  frame.fillRoundRect(windowX, windowY, 6, windowHeight, 2, C_GLASS_EDGE);

  frame.fillRect(windowX + 1, windowY + 1, 4, windowHeight - 2, C_GLASS);

  int16_t insideHeight = windowHeight - 2;

  int16_t fuelHeight = (int16_t)roundf(insideHeight * clamp01(fuelFraction));

  if (fuelHeight > 0) {
    int16_t fuelY = windowY + 1 + insideHeight - fuelHeight;

    frame.fillRect(windowX + 1, fuelY, 4, fuelHeight, C_FUEL);

    frame.drawFastHLine(windowX + 1, fuelY, 4, C_WHITE);
  }

  frame.drawFastVLine(windowX + 1, windowY + 2,
                      maxInt16((int16_t)1, windowHeight / 3),
                      C565(112, 168, 179));

  if (active && fuelFraction < 0.16f && ((now / 110) & 1U) == 0) {
    frame.drawRoundRect(windowX - 1, windowY - 1, 8, windowHeight + 2, 2,
                        C_RED);
  }
}

static void drawEngineAssembly(int16_t centreX, int16_t bottomY,
                               int16_t stageWidth) {
  int16_t bellWidth = stageWidth / 3;

  bellWidth = maxInt16((int16_t)6, bellWidth);

  frame.fillTriangle(centreX - bellWidth / 2, bottomY - 2,
                     centreX + bellWidth / 2, bottomY - 2, centreX, bottomY + 5,
                     C_METAL_DARK);

  frame.drawFastHLine(centreX - bellWidth / 2, bottomY - 2, bellWidth, C_METAL);

  frame.drawLine(centreX - bellWidth / 2, bottomY - 3, centreX - bellWidth,
                 bottomY - 7, C_METAL);

  frame.drawLine(centreX + bellWidth / 2, bottomY - 3, centreX + bellWidth,
                 bottomY - 7, C_METAL);
}

static void drawStageBody(const StageDefinition &definition,
                          const StageGeometry &geometry, float fuelFraction,
                          bool active, uint32_t now) {
  int16_t x = (int16_t)roundf(geometry.x);

  int16_t y = (int16_t)roundf(geometry.y);

  int16_t width = (int16_t)roundf(geometry.width);

  int16_t height = (int16_t)roundf(geometry.height);

  frame.fillRoundRect(x + 2, y + 2, width, height, 2, C_BLACK);

  frame.fillRoundRect(x, y, width, height, 2, C_PAPER);

  frame.fillRect(x + 2, y + 2, maxInt16((int16_t)2, width / 6), height - 4,
                 C_HIGHLIGHT);

  frame.fillRect(x + width - 4, y + 2, 2, height - 4, C_METAL);

  int16_t bandY = y + maxInt16((int16_t)5, height / 5);

  frame.fillRect(x, bandY, width, 4, definition.bandColour);

  frame.drawFastHLine(x, bandY, width,
                      blend565(definition.bandColour, C_WHITE, 70));

  uint16_t collarColour = C_BORDER;

  if (active) {
    if (fuelFraction < 0.05f) {
      collarColour = ((now / 85) & 1U) ? C_RED : C_YELLOW;
    } else if (fuelFraction < 0.18f) {
      collarColour = ((now / 150) & 1U) ? C_YELLOW : C_GREEN;
    } else {
      collarColour = C_GREEN;
    }
  }

  frame.fillRect(x - 2, y - 2, width + 4, 3, collarColour);

  frame.drawFastHLine(x - 1, y + 1, width + 2, C_METAL_DARK);

  frame.fillRect(x, y + height - 5, width, 5, C_METAL_DARK);

  frame.drawFastHLine(x + 2, y + height - 5, width - 4, C_METAL);

  for (int16_t rivetY = y + 12; rivetY < y + height - 8; rivetY += 13) {
    frame.drawPixel(x + 2, rivetY, C_METAL_DARK);

    frame.drawPixel(x + width - 3, rivetY, C_METAL_DARK);
  }

  drawFuelWindow(x, y, width, height, fuelFraction, active, now);

  if (active) {
    drawEngineAssembly(x + width / 2, y + height, width);
  }
}

static void drawEngineFlame(const StageGeometry &activeGeometry, uint32_t now) {
  int16_t centreX =
      (int16_t)roundf(activeGeometry.x + activeGeometry.width * 0.5f);

  int16_t nozzleY =
      (int16_t)roundf(activeGeometry.y + activeGeometry.height + 4.0f);

  float flicker = sinf(now * 0.045f) * 0.13f + sinf(now * 0.083f) * 0.08f;

  float power = clampFloat(1.0f + flicker, 0.75f, 1.24f);

  int16_t outerLength =
      (int16_t)roundf((24.0f + activeGeometry.width * 0.31f) * power);

  int16_t outerWidth = (int16_t)(activeGeometry.width * 0.52f);

  outerWidth = maxInt16((int16_t)8, outerWidth);

  frame.fillTriangle(centreX - outerWidth / 2, nozzleY,
                     centreX + outerWidth / 2, nozzleY, centreX,
                     nozzleY + outerLength + 5, C_RED);

  frame.fillTriangle(centreX - outerWidth / 2 + 2, nozzleY,
                     centreX + outerWidth / 2 - 2, nozzleY, centreX,
                     nozzleY + outerLength, C_ORANGE);

  frame.fillTriangle(centreX - outerWidth / 4, nozzleY + 1,
                     centreX + outerWidth / 4, nozzleY + 1, centreX,
                     nozzleY + outerLength * 2 / 3, C_YELLOW);

  frame.fillTriangle(centreX - 2, nozzleY + 1, centreX + 2, nozzleY + 1,
                     centreX, nozzleY + outerLength / 3, C_WHITE);
}

static void drawRocket(const MissionDefinition &mission,
                       const RocketGeometry &geometry, uint8_t activeStage,
                       float activeFuel, bool engineOn, uint32_t now) {
  drawPayload(geometry, mission.recoverCapsule);

  for (uint8_t index = 0; index < geometry.stageCount; index++) {
    const StageGeometry &stageGeometry = geometry.stages[index];

    bool active = stageGeometry.sourceIndex == activeStage;

    float fuel = active ? activeFuel : 1.0f;

    drawStageBody(mission.stages[stageGeometry.sourceIndex], stageGeometry,
                  fuel, active, now);
  }

  if (engineOn && geometry.stageCount > 0) {
    drawEngineFlame(geometry.stages[geometry.stageCount - 1], now);
  }
}

// =============================================================================
// Feedback, separation and explosion
// =============================================================================

static void setFeedback(const char *message, uint16_t colour, float duration) {
  strncpy(game.feedback, message, sizeof(game.feedback) - 1);

  game.feedback[sizeof(game.feedback) - 1] = '\0';

  game.feedbackColour = colour;
  game.feedbackRemaining = duration;
}

static void createWholeDetachedStage(const StageDefinition &definition,
                                     const StageGeometry &geometry,
                                     float fuelFraction) {
  int16_t bandPosition =
      maxInt16((int16_t)5, (int16_t)(geometry.height / 5.0f));

  createFragment(FragmentType::WHOLE_STAGE, geometry.x + geometry.width * 0.5f,
                 geometry.y + geometry.height * 0.5f, geometry.width,
                 geometry.height, randomGenerator.range(-10.0f, 10.0f),
                 randomGenerator.range(18.0f, 29.0f), 0.0f,
                 randomGenerator.range(-1.65f, 1.65f), 4.8f,
                 definition.bandColour, fuelFraction, (int8_t)bandPosition);

  createFragment(FragmentType::ENGINE, geometry.x + geometry.width * 0.5f,
                 geometry.y + geometry.height + 2.0f,
                 maxFloat(7.0f, geometry.width * 0.34f), 7.0f,
                 randomGenerator.range(-13.0f, 13.0f),
                 randomGenerator.range(22.0f, 34.0f), 0.0f,
                 randomGenerator.range(-2.1f, 2.1f), 3.2f, C_METAL, 0.0f, -1);
}

static void beginExplosion(const char *reason) {
  if (game.phase == FlightPhase::EXPLOSION) {
    return;
  }

  RocketGeometry geometry;
  buildCurrentRocketGeometry(geometry);

  game.explosionX = SCREEN_W * 0.5f;

  game.explosionY =
      geometry.noseY + (geometry.bottomY - geometry.noseY) * 0.62f;

  strncpy(game.failureReason, reason, sizeof(game.failureReason) - 1);

  game.failureReason[sizeof(game.failureReason) - 1] = '\0';

  game.phase = FlightPhase::EXPLOSION;

  game.phaseStartedAt = millis();

  game.success = false;
  game.flash = 1.0f;
  game.shake = 10.0f;

  createFragment(
      FragmentType::CAPSULE, geometry.payloadX + geometry.payloadWidth * 0.5f,
      geometry.payloadY + geometry.payloadHeight * 0.5f, geometry.payloadWidth,
      geometry.payloadHeight, randomGenerator.range(-48.0f, 48.0f),
      randomGenerator.range(-92.0f, -34.0f),
      randomGenerator.range(-0.25f, 0.25f), randomGenerator.range(-4.4f, 4.4f),
      3.7f, currentMission().recoverCapsule ? C_CYAN : C_PURPLE, 0.0f, -1);

  for (uint8_t geometryIndex = 0; geometryIndex < geometry.stageCount;
       geometryIndex++) {
    const StageGeometry &stageGeometry = geometry.stages[geometryIndex];

    const StageDefinition &definition =
        currentMission().stages[stageGeometry.sourceIndex];

    float stageFuel = stageGeometry.sourceIndex == game.activeStage
                          ? currentFuelFraction()
                          : 1.0f;

    static constexpr uint8_t SLICE_COUNT = 3;

    float sliceHeight = stageGeometry.height / SLICE_COUNT;

    float originalBandY = maxFloat(5.0f, stageGeometry.height / 5.0f);

    for (uint8_t slice = 0; slice < SLICE_COUNT; slice++) {
      float sliceTop = slice * sliceHeight;

      float sliceCentreY = stageGeometry.y + sliceTop + sliceHeight * 0.5f;

      int8_t localBand = -1;

      if (originalBandY >= sliceTop && originalBandY < sliceTop + sliceHeight) {
        localBand = (int8_t)(originalBandY - sliceTop);
      }

      float deltaX =
          stageGeometry.x + stageGeometry.width * 0.5f - game.explosionX;

      float deltaY = sliceCentreY - game.explosionY;

      float distance = sqrtf(deltaX * deltaX + deltaY * deltaY);

      if (distance < 1.0f) {
        distance = 1.0f;
      }

      float radialX = deltaX / distance;

      float radialY = deltaY / distance;

      float force = randomGenerator.range(38.0f, 86.0f);

      createFragment(FragmentType::STAGE_SLICE,
                     stageGeometry.x + stageGeometry.width * 0.5f, sliceCentreY,
                     stageGeometry.width, sliceHeight - 1.0f,
                     radialX * force + randomGenerator.range(-32.0f, 32.0f),
                     radialY * force + randomGenerator.range(-42.0f, 22.0f),
                     randomGenerator.range(-0.18f, 0.18f),
                     randomGenerator.range(-5.2f, 5.2f),
                     randomGenerator.range(3.0f, 4.2f), definition.bandColour,
                     stageFuel, localBand);
    }

    for (uint8_t side = 0; side < 2; side++) {
      float sideDirection = side == 0 ? -1.0f : 1.0f;

      createFragment(
          FragmentType::SIDE_PANEL,
          stageGeometry.x + stageGeometry.width * (side == 0 ? 0.12f : 0.88f),
          stageGeometry.y +
              stageGeometry.height * randomGenerator.range(0.25f, 0.75f),
          4.0f, stageGeometry.height * randomGenerator.range(0.28f, 0.52f),
          sideDirection * randomGenerator.range(55.0f, 105.0f),
          randomGenerator.range(-72.0f, 35.0f), 0.0f,
          randomGenerator.range(-7.0f, 7.0f), 2.8f, definition.bandColour, 0.0f,
          -1);
    }

    createFragment(FragmentType::ENGINE,
                   stageGeometry.x + stageGeometry.width * 0.5f,
                   stageGeometry.y + stageGeometry.height,
                   maxFloat(7.0f, stageGeometry.width * 0.35f), 7.0f,
                   randomGenerator.range(-70.0f, 70.0f),
                   randomGenerator.range(-40.0f, 70.0f), 0.0f,
                   randomGenerator.range(-6.0f, 6.0f), 3.0f, C_METAL, 0.0f, -1);
  }

  spawnFireBurst(game.explosionX, game.explosionY, 52, 94.0f);

  spawnFireBurst(game.explosionX, game.explosionY + 15.0f, 24, 74.0f);

  spawnSmokeBurst(game.explosionX, game.explosionY, 43, 49.0f);

  spawnSparkBurst(game.explosionX, game.explosionY, 36, C_YELLOW);
}

static void separateCurrentStage() {
  const StageDefinition *stage = currentStage();

  if (stage == nullptr || game.phase != FlightPhase::BURNING) {
    return;
  }

  float progress = game.stageElapsed / stage->burnSeconds;

  if (progress >= 0.985f) {
    beginExplosion("STAGE RUPTURE");

    return;
  }

  RocketGeometry geometry;
  buildCurrentRocketGeometry(geometry);

  if (geometry.stageCount == 0) {
    return;
  }

  const StageGeometry &activeGeometry =
      geometry.stages[geometry.stageCount - 1];

  float remainingFuel = clamp01(1.0f - progress);

  createWholeDetachedStage(*stage, activeGeometry, remainingFuel);

  float seamX = activeGeometry.x + activeGeometry.width * 0.5f;

  float seamY = activeGeometry.y;

  spawnSparkBurst(seamX, seamY, 15, C_GREEN);

  spawnSmokeBurst(seamX, seamY + 2.0f, 7, 17.0f);

  if (progress < 0.62f) {
    setFeedback("EARLY", C_ORANGE, 1.15f);

    game.velocity *= 0.72f;
    game.score += 75;
    game.shake = 1.0f;
  } else if (progress < 0.84f) {
    setFeedback("GOOD", C_CYAN, 1.15f);

    game.velocity *= 0.90f;
    game.score += 280;
    game.shake = 1.4f;
  } else {
    setFeedback("AWESOME", C_GREEN, 1.25f);

    game.velocity *= 0.985f;
    game.score += 590;
    game.shake = 2.2f;
  }

  game.activeStage++;
  game.stageElapsed = 0.0f;

  if (game.activeStage >= currentMission().stageCount) {
    game.phase = FlightPhase::COASTING;

    game.phaseStartedAt = millis();
  } else {
    game.phase = FlightPhase::BURNING;

    game.phaseStartedAt = millis();

    RocketGeometry nextGeometry;
    buildCurrentRocketGeometry(nextGeometry);

    if (nextGeometry.stageCount > 0) {
      const StageGeometry &newActive =
          nextGeometry.stages[nextGeometry.stageCount - 1];

      float nozzleX = newActive.x + newActive.width * 0.5f;

      float nozzleY = newActive.y + newActive.height + 4.0f;

      spawnFireBurst(nozzleX, nozzleY, 11, 30.0f);

      spawnSmokeBurst(nozzleX, nozzleY + 4.0f, 5, 17.0f);
    }
  }
}

// =============================================================================
// State management
// =============================================================================

static void setScreen(ScreenMode screen) {
  game.screen = screen;
  game.screenStartedAt = millis();

  button.clearEvents();
}

static void setPhase(FlightPhase phase) {
  game.phase = phase;
  game.phaseStartedAt = millis();
}

static void resetFlightObjects() {
  clearParticles();
  clearFragments();

  game.stageElapsed = 0.0f;

  game.altitude = 0.0f;
  game.maximumAltitude = 0.0f;
  game.velocity = 0.0f;

  game.cameraAltitude = 0.0f;
  game.previousCameraAltitude = 0.0f;

  game.activeStage = 0;

  game.score = 0;
  game.success = false;

  game.shake = 0.0f;
  game.flash = 0.0f;

  game.feedbackRemaining = 0.0f;
  game.feedback[0] = '\0';
  game.failureReason[0] = '\0';

  game.recoveryY = -36.0f;
  game.parachuteOpen = 0.0f;
  game.deploymentY = 170.0f;

  game.explosionX = 86.0f;
  game.explosionY = 150.0f;
}

static void startMission() {
  resetFlightObjects();

  const MissionDefinition &mission = currentMission();

  float completeHeight = remainingRocketHeight(mission, 0);

  game.launchNoseY = 270.0f - completeHeight;

  // The camera begins tracking after a short, fast pad-clearance movement.
  game.trackingNoseY = game.launchNoseY - 48.0f;

  if (game.trackingNoseY < 24.0f) {
    game.trackingNoseY = 24.0f;
  }

  game.cameraAltitude = 0.0f;
  game.previousCameraAltitude = 0.0f;

  setScreen(ScreenMode::FLIGHT);

  setPhase(FlightPhase::COUNTDOWN);
}

static void continueCampaignFromResult() {
  if (!game.success) {
    startMission();
    return;
  }

  if (game.selectedMission + 1 < MISSION_COUNT) {
    game.selectedMission++;

    if (game.selectedMission + 1 > game.unlockedMissions) {
      game.unlockedMissions = game.selectedMission + 1;
    }

    preferences.putUChar("current", game.selectedMission);

    preferences.putUChar("unlocked", game.unlockedMissions);

    setScreen(ScreenMode::BRIEFING);

    return;
  }

  // Final mission stays active after the campaign is completed.
  setScreen(ScreenMode::BRIEFING);
}

// =============================================================================
// Physics
// =============================================================================

static float gravityAtAltitude(float altitude) {
  return lerpFloat(1.90f, 1.15f, smoothStep(0.0f, 1500.0f, altitude));
}

static void spawnContinuousEngineEffects(const RocketGeometry &geometry) {
  if (geometry.stageCount == 0) {
    return;
  }

  const StageGeometry &activeGeometry =
      geometry.stages[geometry.stageCount - 1];

  float nozzleX = activeGeometry.x + activeGeometry.width * 0.5f;

  float nozzleY = activeGeometry.y + activeGeometry.height + 7.0f;

  spawnParticle(
      ParticleType::FIRE, nozzleX + randomGenerator.range(-2.5f, 2.5f),
      nozzleY + randomGenerator.range(3.0f, 10.0f),
      randomGenerator.range(-6.0f, 6.0f), randomGenerator.range(31.0f, 61.0f),
      randomGenerator.range(0.18f, 0.42f), randomGenerator.range(1.2f, 2.5f),
      randomGenerator.chance(0.5f) ? C_YELLOW : C_ORANGE);

  if (randomGenerator.chance(0.65f)) {
    spawnParticle(ParticleType::SMOKE,
                  nozzleX + randomGenerator.range(-5.0f, 5.0f), nozzleY + 20.0f,
                  randomGenerator.range(-15.0f, 15.0f),
                  randomGenerator.range(21.0f, 39.0f),
                  randomGenerator.range(0.55f, 1.15f),
                  randomGenerator.range(2.0f, 4.0f), C_SMOKE);
  }

  if (game.altitude < 12.0f) {
    float padY = 274.0f + game.cameraAltitude * WORLD_PIXELS_PER_ALTITUDE;

    spawnParticle(
        ParticleType::SMOKE, 86.0f + randomGenerator.range(-28.0f, 28.0f),
        padY + randomGenerator.range(-2.0f, 10.0f),
        randomGenerator.range(-40.0f, 40.0f),
        randomGenerator.range(-8.0f, 14.0f), randomGenerator.range(0.8f, 1.8f),
        randomGenerator.range(3.0f, 6.0f),
        randomGenerator.chance(0.5f) ? C_SMOKE : C_SMOKE_DARK);
  }
}

static void completeMission() {
  game.success = true;

  game.score += 1500 + (int32_t)(maxFloat(0.0f, game.velocity) * 90.0f);

  saveMissionProgress();

  setFeedback("SPACE REACHED", C_GREEN, 1.7f);

  if (currentMission().recoverCapsule) {
    game.recoveryY = -38.0f;
    game.parachuteOpen = 0.0f;

    setPhase(FlightPhase::RECOVERY);
  } else {
    game.deploymentY = 170.0f;

    setPhase(FlightPhase::DEPLOYMENT);
  }
}

static void failByTrajectory() {
  game.success = false;

  strncpy(game.failureReason, "INSUFFICIENT DELTA-V",
          sizeof(game.failureReason) - 1);

  game.failureReason[sizeof(game.failureReason) - 1] = '\0';

  setFeedback("APOGEE", C_ORANGE, 1.1f);

  setPhase(FlightPhase::DESCENDING);
}

static void updateFlight(float deltaSeconds, uint32_t now) {
  float phaseSeconds = (now - game.phaseStartedAt) / 1000.0f;

  bool emitEngineEffects = false;

  if (game.phase == FlightPhase::COUNTDOWN) {
    if (phaseSeconds >= 3.0f) {
      setPhase(FlightPhase::BURNING);

      // Stronger initial kick.
      game.velocity = 1.35f;

      setFeedback("LIFTOFF", C_YELLOW, 1.0f);

      spawnSmokeBurst(86.0f, 267.0f, 34, 43.0f);

      spawnFireBurst(86.0f, 258.0f, 18, 34.0f);
    }
  } else if (game.phase == FlightPhase::BURNING) {
    const StageDefinition *stage = currentStage();

    if (stage == nullptr) {
      setPhase(FlightPhase::COASTING);
    } else {
      game.stageElapsed += deltaSeconds;

      float gravity = gravityAtAltitude(game.altitude);

      game.velocity += (stage->thrust - gravity) * deltaSeconds;

      game.altitude += maxFloat(0.0f, game.velocity) * deltaSeconds;

      game.score += (int32_t)(maxFloat(0.0f, game.velocity) * 1.9f);

      emitEngineEffects = true;

      if (game.stageElapsed > stage->burnSeconds * 1.045f) {
        beginExplosion("ENGINE RUPTURE");

        emitEngineEffects = false;
      }
    }
  } else if (game.phase == FlightPhase::COASTING) {
    float gravity = gravityAtAltitude(game.altitude);

    game.velocity -= gravity * deltaSeconds;

    game.altitude += game.velocity * deltaSeconds;

    if (game.altitude >= currentMission().targetAltitude) {
      completeMission();
    } else if (game.velocity <= 0.0f) {
      failByTrajectory();
    }
  } else if (game.phase == FlightPhase::DESCENDING) {
    game.velocity -= gravityAtAltitude(game.altitude) * deltaSeconds;

    game.altitude =
        maxFloat(0.0f, game.altitude + game.velocity * deltaSeconds);

    if (phaseSeconds >= 2.8f) {
      saveMissionProgress();

      setScreen(ScreenMode::RESULT);
    }
  } else if (game.phase == FlightPhase::EXPLOSION) {
    if (phaseSeconds < 0.95f && randomGenerator.chance(0.72f)) {
      spawnFireBurst(game.explosionX + randomGenerator.range(-8.0f, 8.0f),
                     game.explosionY + randomGenerator.range(-10.0f, 12.0f), 2,
                     28.0f);
    }

    if (phaseSeconds < 1.45f && randomGenerator.chance(0.55f)) {
      spawnParticle(ParticleType::SMOKE,
                    game.explosionX + randomGenerator.range(-13.0f, 13.0f),
                    game.explosionY + randomGenerator.range(-9.0f, 15.0f),
                    randomGenerator.range(-14.0f, 14.0f),
                    randomGenerator.range(-18.0f, 12.0f),
                    randomGenerator.range(0.8f, 1.55f),
                    randomGenerator.range(3.0f, 6.0f), C_SMOKE);
    }

    if (phaseSeconds >= 2.85f) {
      saveMissionProgress();

      setScreen(ScreenMode::RESULT);
    }
  } else if (game.phase == FlightPhase::DEPLOYMENT) {
    game.deploymentY -= 19.0f * deltaSeconds;

    game.altitude += maxFloat(0.35f, game.velocity * 0.18f) * deltaSeconds;

    if (phaseSeconds >= 3.0f) {
      setScreen(ScreenMode::RESULT);
    }
  } else if (game.phase == FlightPhase::RECOVERY) {
    game.parachuteOpen = clamp01(game.parachuteOpen + deltaSeconds * 0.75f);

    game.recoveryY +=
        lerpFloat(23.0f, 38.0f, game.parachuteOpen) * deltaSeconds;

    if (phaseSeconds >= 4.3f) {
      setScreen(ScreenMode::RESULT);
    }
  }

  updateWorldCamera(deltaSeconds);

  if (emitEngineEffects && game.phase == FlightPhase::BURNING) {
    RocketGeometry geometry;

    buildCurrentRocketGeometry(geometry);

    spawnContinuousEngineEffects(geometry);
  }

  game.maximumAltitude = maxFloat(game.maximumAltitude, game.altitude);

  game.feedbackRemaining =
      maxFloat(0.0f, game.feedbackRemaining - deltaSeconds);

  game.shake = maxFloat(0.0f, game.shake - deltaSeconds * 12.0f);

  game.flash = maxFloat(0.0f, game.flash - deltaSeconds * 1.8f);

  updateParticles(deltaSeconds);
  updateFragments(deltaSeconds);
}

// =============================================================================
// Input
// =============================================================================

static void updateScreenInput() {
  if (game.screen == ScreenMode::SPLASH) {
    if (button.events.tapped) {
      setScreen(ScreenMode::BRIEFING);
    }

    return;
  }

  if (game.screen == ScreenMode::BRIEFING) {
    if (button.events.tapped) {
      startMission();
    }

    return;
  }

  if (game.screen == ScreenMode::FLIGHT) {
    if (button.events.pressed && game.phase == FlightPhase::BURNING) {
      separateCurrentStage();
    }

    return;
  }

  if (game.screen == ScreenMode::RESULT) {
    if (button.events.tapped) {
      continueCampaignFromResult();
    }
  }
}

// =============================================================================
// HUD
// =============================================================================

static void drawHud() {
  char buffer[32];

  frame.fillRoundRect(5, 5, 68, 25, 5, C_PANEL);

  frame.drawRoundRect(5, 5, 68, 25, 5, C_BORDER);

  drawTextLeft(currentMission().code, 10, 9, 1, C_MUTED);

  snprintf(buffer, sizeof(buffer), "%d KM", (int)game.altitude);

  drawTextRight(buffer, 68, 19, 1, C_TEXT);

  frame.fillRoundRect(99, 5, 68, 25, 5, C_PANEL);

  frame.drawRoundRect(99, 5, 68, 25, 5, C_BORDER);

  drawTextLeft("SCORE", 104, 9, 1, C_MUTED);

  snprintf(buffer, sizeof(buffer), "%ld", (long)game.score);

  drawTextRight(buffer, 162, 19, 1, C_YELLOW);

  if (game.phase == FlightPhase::BURNING) {
    snprintf(buffer, sizeof(buffer), "STAGE %u/%u", game.activeStage + 1,
             currentMission().stageCount);

    drawTextCentered(buffer, 35, 1, C_MUTED);
  } else if (game.phase == FlightPhase::COASTING) {
    drawTextCentered("COAST", 35, 1, C_CYAN);
  }

  if (game.feedbackRemaining > 0.0f) {
    uint8_t size = strcmp(game.feedback, "AWESOME") == 0 ? 2 : 1;

    drawTextCentered(game.feedback, 48, size, game.feedbackColour);
  }
}

static void drawCountdown() {
  float seconds = (millis() - game.phaseStartedAt) / 1000.0f;

  int countdown = 3 - (int)seconds;

  char buffer[8];

  if (countdown > 0) {
    snprintf(buffer, sizeof(buffer), "%d", countdown);
  } else {
    strcpy(buffer, "GO");
  }

  drawTextCentered(buffer, 48, 4, C_WHITE);

  drawTextCentered("LAUNCH SEQUENCE", 82, 1, C_YELLOW);
}

static void drawShockwave() {
  if (game.phase != FlightPhase::EXPLOSION) {
    return;
  }

  float elapsed = (millis() - game.phaseStartedAt) / 1000.0f;

  if (elapsed > 0.75f) {
    return;
  }

  int16_t radius = 4 + (int16_t)(elapsed * 120.0f);

  frame.drawCircle((int16_t)game.explosionX, (int16_t)game.explosionY, radius,
                   C_YELLOW);

  if (radius > 5) {
    frame.drawCircle((int16_t)game.explosionX, (int16_t)game.explosionY,
                     radius - 4, C_ORANGE);
  }
}

// =============================================================================
// Flight, recovery and deployment rendering
// =============================================================================

static void drawRecoveryScreen(uint32_t now) {
  drawBackground(85.0f, now);

  float y = game.recoveryY;

  float open = game.parachuteOpen;

  int16_t canopyWidth = (int16_t)(40.0f * open);

  if (canopyWidth > 5) {
    int16_t centreX = SCREEN_W / 2;

    int16_t canopyY = (int16_t)y - 12;

    frame.fillCircle(centreX, canopyY, canopyWidth / 2, C_RED);

    frame.fillRect(centreX - canopyWidth / 2, canopyY, canopyWidth,
                   canopyWidth / 2 + 2, skyTopColour(85.0f));

    frame.drawLine(centreX - canopyWidth / 2, canopyY, centreX - 7,
                   (int16_t)y + 8, C_WHITE);

    frame.drawLine(centreX + canopyWidth / 2, canopyY, centreX + 7,
                   (int16_t)y + 8, C_WHITE);

    frame.drawLine(centreX, canopyY, centreX, (int16_t)y + 8, C_WHITE);
  }

  RocketGeometry capsuleGeometry = {};

  capsuleGeometry.payloadX = SCREEN_W / 2 - 10;

  capsuleGeometry.payloadY = y + 7;

  capsuleGeometry.payloadWidth = 20;
  capsuleGeometry.payloadHeight = 22;

  drawPayload(capsuleGeometry, true);

  drawTextCentered("CAPSULE RECOVERY", 32, 1, C_GREEN);
}

static void drawDeploymentScreen(uint32_t now) {
  drawBackground(maxFloat(game.cameraAltitude, 680.0f), now);

  int16_t payloadY = (int16_t)game.deploymentY;

  frame.fillRoundRect(78, payloadY, 16, 14, 3, C_PANEL_2);

  frame.drawRoundRect(78, payloadY, 16, 14, 3, C_METAL);

  float elapsed = (now - game.phaseStartedAt) / 1000.0f;

  float deployment = smoothStep(0.4f, 1.35f, elapsed);

  int16_t panelWidth = (int16_t)(22.0f * deployment);

  frame.fillRect(78 - panelWidth, payloadY + 3, panelWidth, 8, C_BLUE);

  frame.fillRect(94, payloadY + 3, panelWidth, 8, C_BLUE);

  for (int16_t panelLine = 4; panelLine < panelWidth; panelLine += 5) {
    frame.drawFastVLine(78 - panelLine, payloadY + 3, 8, C_CYAN);

    frame.drawFastVLine(93 + panelLine, payloadY + 3, 8, C_CYAN);
  }

  frame.drawFastVLine(85, payloadY - 8, 28, C_WHITE);

  drawTextCentered("PAYLOAD CONTINUES", 38, 1, C_GREEN);

  drawTextCentered("OUTER SPACE", 53, 1, C_CYAN);
}

static void drawFlightScreen(uint32_t now) {
  if (game.phase == FlightPhase::RECOVERY) {
    drawRecoveryScreen(now);
    return;
  }

  if (game.phase == FlightPhase::DEPLOYMENT) {
    drawDeploymentScreen(now);
    return;
  }

  drawBackground(game.cameraAltitude, now);

  drawLaunchPad(game.cameraAltitude, now, game.phase == FlightPhase::COUNTDOWN);

  drawParticles();
  drawFragments();

  if (game.phase != FlightPhase::EXPLOSION) {
    RocketGeometry geometry;

    buildCurrentRocketGeometry(geometry);

    drawRocket(currentMission(), geometry, game.activeStage,
               currentFuelFraction(), game.phase == FlightPhase::BURNING, now);
  } else {
    drawShockwave();
    drawParticles();
  }

  drawHud();

  if (game.phase == FlightPhase::COUNTDOWN) {
    drawCountdown();
  }

  if (game.phase == FlightPhase::DESCENDING) {
    drawTextCentered("TRAJECTORY LOST", 66, 1, C_ORANGE);

    drawTextCentered("ROCKET FALLING", 81, 1, C_RED);
  }

  if (game.phase == FlightPhase::EXPLOSION) {
    drawTextCentered("MISSION FAILURE", 52, 1, C_RED);

    drawTextCentered(game.failureReason, 67, 1, C_TEXT);
  }

  if (game.flash > 0.0f) {
    uint16_t flashColour =
        blend565(C_BLACK, C_WHITE, (uint8_t)(game.flash * 155.0f));

    frame.drawRect(0, 0, SCREEN_W, SCREEN_H, flashColour);

    frame.drawRect(1, 1, SCREEN_W - 2, SCREEN_H - 2, flashColour);
  }
}

// =============================================================================
// Splash
// =============================================================================

static void drawSplashSmoke(uint32_t elapsed) {
  if (elapsed < 950) {
    return;
  }

  for (uint8_t index = 0; index < 15; index++) {
    float age = (elapsed - 950 + index * 83) % 1250;

    float amount = age / 1250.0f;

    int16_t x = 86 + (int16_t)(sinf(index * 2.7f + elapsed * 0.004f) *
                               (4.0f + amount * 25.0f));

    int16_t y = 272 + (int16_t)(amount * 34.0f);

    int16_t radius = 2 + (int16_t)(amount * 6.0f);

    frame.fillCircle(x, y, radius, index & 1 ? C_SMOKE : C_SMOKE_DARK);
  }
}

static void drawSplashScreen(uint32_t now) {
  uint32_t elapsed = now - game.screenStartedAt;

  drawBackground(0.0f, now);

  drawLaunchPad(0.0f, now, elapsed > 650);

  const MissionDefinition &mission = MISSIONS[game.selectedMission];

  float completeHeight = remainingRocketHeight(mission, 0);

  float baseNoseY = 270.0f - completeHeight;

  float lift = 0.0f;

  if (elapsed > 1800) {
    lift = minFloat(38.0f, (elapsed - 1800) * 0.040f);
  }

  RocketGeometry geometry;

  buildRocketGeometryFor(mission, 0, baseNoseY - lift, geometry);

  drawSplashSmoke(elapsed);

  drawRocket(mission, geometry, 0, 0.92f, elapsed > 920, now);

  frame.fillRoundRect(11, 28, 150, 85, 10, C_PANEL);

  frame.drawRoundRect(11, 28, 150, 85, 10, C_BORDER);

  frame.drawFastVLine(20, 38, 64, C_GREEN);

  drawTextLeft("FLIGHT SYSTEM", 28, 39, 1, C_MUTED);

  drawTextLeft("VOID", 28, 55, 3, C_WHITE);

  drawTextLeft("ASCENT", 28, 83, 2, C_GREEN);

  if (((elapsed / 400) & 1U) == 0) {
    drawTextCentered("TAP TO CONTINUE", 292, 1, C_WHITE);
  }
}

// =============================================================================
// Briefing
// =============================================================================

static void drawBriefingScreen(uint32_t now) {
  drawBackground(0.0f, now);

  drawLaunchPad(0.0f, now, false);

  const MissionDefinition &mission = currentMission();

  float height = remainingRocketHeight(mission, 0);

  RocketGeometry previewGeometry;

  buildRocketGeometryFor(mission, 0, 267.0f - height, previewGeometry);

  drawRocket(mission, previewGeometry, 0, 1.0f, false, now);

  frame.fillRoundRect(8, 8, 156, 170, 9, C_PANEL);

  frame.drawRoundRect(8, 8, 156, 170, 9, C_BORDER);

  char levelText[24];

  snprintf(levelText, sizeof(levelText), "LEVEL %u / %u",
           game.selectedMission + 1, MISSION_COUNT);

  drawTextLeft(levelText, 17, 18, 1, C_GREEN);

  drawTextRight(mission.code, 154, 18, 1, C_CYAN);

  drawTextCentered(mission.name, 37, 1, C_TEXT);

  char targetText[28];

  snprintf(targetText, sizeof(targetText), "TARGET %u KM",
           mission.targetAltitude);

  drawTextCentered(targetText, 55, 1, C_YELLOW);

  char stagesText[24];

  snprintf(stagesText, sizeof(stagesText), "%u STAGE STACK",
           mission.stageCount);

  drawTextCentered(stagesText, 70, 1, C_CYAN);

  drawTextLeft("OBJECTIVE", 18, 92, 1, C_MUTED);

  drawTextLeft(mission.objectiveLine1, 18, 109, 1, C_TEXT);

  drawTextLeft(mission.objectiveLine2, 18, 124, 1, C_TEXT);

  drawTextLeft("WATCH THE FUEL GLASS", 18, 146, 1, C_GREEN);

  drawTextLeft("TAP NEAR EMPTY", 18, 160, 1, C_YELLOW);

  frame.fillRoundRect(18, 278, 136, 34, 8, C_PANEL);

  frame.drawRoundRect(18, 278, 136, 34, 8, C_BORDER);

  drawTextCentered("TAP TO LAUNCH", 287, 1,
                   ((now / 330) & 1U) ? C_WHITE : C_GREEN);

  drawTextCentered("NEXT LEVEL LOCKED", 301, 1,
                   game.selectedMission + 1 < game.unlockedMissions ? C_GREEN
                                                                    : C_MUTED);
}

// =============================================================================
// Result
// =============================================================================

static void drawResultScreen(uint32_t now) {
  drawBackground(game.maximumAltitude, now);

  frame.fillRoundRect(10, 28, 152, 225, 10, C_PANEL);

  frame.drawRoundRect(10, 28, 152, 225, 10, C_BORDER);

  bool finalMission = game.selectedMission + 1 >= MISSION_COUNT;

  drawTextCentered(game.success
                       ? (finalMission ? "CAMPAIGN COMPLETE" : "LEVEL CLEARED")
                       : "MISSION FAILED",
                   43, 1, game.success ? C_GREEN : C_RED);

  drawTextCentered(currentMission().name, 60, 1, C_TEXT);

  char buffer[32];

  drawTextLeft("SCORE", 23, 91, 1, C_MUTED);

  snprintf(buffer, sizeof(buffer), "%ld", (long)game.score);

  drawTextRight(buffer, 148, 91, 1, C_YELLOW);

  drawTextLeft("APOGEE", 23, 115, 1, C_MUTED);

  snprintf(buffer, sizeof(buffer), "%d KM", (int)game.maximumAltitude);

  drawTextRight(buffer, 148, 115, 1, C_TEXT);

  drawTextLeft("BEST", 23, 139, 1, C_MUTED);

  snprintf(buffer, sizeof(buffer), "%ld",
           (long)game.bestScores[game.selectedMission]);

  drawTextRight(buffer, 148, 139, 1, C_TEXT);

  if (game.success) {
    if (!finalMission) {
      char unlockedText[28];

      snprintf(unlockedText, sizeof(unlockedText), "LEVEL %u UNLOCKED",
               game.selectedMission + 2);

      drawTextCentered(unlockedText, 177, 1, C_GREEN);

      drawTextCentered("TAP FOR NEXT LEVEL", 198, 1, C_CYAN);
    } else {
      drawTextCentered("ALL LEVELS CLEARED", 177, 1, C_GREEN);

      drawTextCentered("TAP TO REPLAY", 198, 1, C_CYAN);
    }
  } else {
    drawTextCentered("FAILURE CAUSE", 170, 1, C_MUTED);

    drawTextCentered(game.failureReason, 190, 1, C_ORANGE);

    drawTextCentered("TAP TO RETRY", 218, 1, C_WHITE);
  }
}

// =============================================================================
// Rendering
// =============================================================================

static void renderFrame(uint32_t now) {
  frame.fillScreen(C_BLACK);

  switch (game.screen) {
  case ScreenMode::SPLASH:
    drawSplashScreen(now);
    break;

  case ScreenMode::BRIEFING:
    drawBriefingScreen(now);
    break;

  case ScreenMode::FLIGHT:
    drawFlightScreen(now);
    break;

  case ScreenMode::RESULT:
    drawResultScreen(now);
    break;
  }

  if (game.shake > 0.0f) {
    frame.drawRect(0, 0, SCREEN_W, SCREEN_H, blend565(C_BORDER, C_RED, 80));
  }

  gfx->draw16bitRGBBitmap(0, 0, frame.getBuffer(), SCREEN_W, SCREEN_H);
}

// =============================================================================
// RGB LED
// =============================================================================

static void setLedColour(uint8_t red, uint8_t green, uint8_t blue) {
  rgbLed.setPixelColor(0, rgbLed.Color(red, green, blue));

  rgbLed.show();
}

static void updateLed(uint32_t now) {
  if (game.screen == ScreenMode::SPLASH) {
    uint8_t pulse = 8 + (uint8_t)((sinf(now * 0.004f) * 0.5f + 0.5f) * 28.0f);

    setLedColour(0, pulse, pulse);

    return;
  }

  if (game.screen != ScreenMode::FLIGHT) {
    setLedColour(0, 18, 26);

    return;
  }

  if (game.phase == FlightPhase::COUNTDOWN) {
    bool lit = ((now / 220) & 1U) == 0;

    setLedColour(lit ? 45 : 5, lit ? 20 : 2, 0);

    return;
  }

  if (game.phase == FlightPhase::BURNING) {
    float fuel = currentFuelFraction();

    if (fuel < 0.08f) {
      setLedColour(55, 0, 0);
    } else if (fuel < 0.20f) {
      setLedColour(42, 23, 0);
    } else {
      setLedColour(0, 24, 32);
    }

    return;
  }

  if (game.phase == FlightPhase::EXPLOSION) {
    bool lit = ((now / 85) & 1U) == 0;

    setLedColour(lit ? 62 : 6, 0, 0);

    return;
  }

  if (game.success) {
    setLedColour(0, 45, 14);
  } else {
    setLedColour(30, 8, 0);
  }
}

// =============================================================================
// Backlight
// =============================================================================

static void initializeBacklight() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3

  ledcAttach(PIN_LCD_BL, 5000, 8);

  ledcWrite(PIN_LCD_BL, 175);

#else

  static constexpr uint8_t BACKLIGHT_CHANNEL = 0;

  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);

  ledcAttachPin(PIN_LCD_BL, BACKLIGHT_CHANNEL);

  ledcWrite(BACKLIGHT_CHANNEL, 175);

#endif
}

// =============================================================================
// Public entry points
// =============================================================================

void voidAscentSetup() {
  Serial.begin(115200);

  randomGenerator.state = esp_random();

  button.begin();

  rgbLed.begin();
  rgbLed.setBrightness(45);
  rgbLed.clear();
  rgbLed.show();

  initializeBacklight();

  if (!gfx->begin(LCD_SPI_HZ)) {
    Serial.println("LCD initialization failed.");

    while (true) {
      setLedColour(55, 0, 0);

      delay(160);

      setLedColour(0, 0, 0);

      delay(160);
    }
  }

  gfx->fillScreen(C_BLACK);

  frame.setTextWrap(false);

  initializeSceneSeeds();
  loadProgress();

  setScreen(ScreenMode::SPLASH);

  lastFrameAt = millis();

  Serial.println("VOID ASCENT campaign build started.");
}

void voidAscentLoop() {
  uint32_t now = millis();

  button.update(now);
  updateScreenInput();

  if (game.screen == ScreenMode::SPLASH && now - game.screenStartedAt > 3900) {
    setScreen(ScreenMode::BRIEFING);
  }

  if (now - lastFrameAt < FRAME_INTERVAL_MS) {
    updateLed(now);
    delay(1);
    return;
  }

  float deltaSeconds = (now - lastFrameAt) / 1000.0f;

  if (deltaSeconds > 0.08f) {
    deltaSeconds = 0.08f;
  }

  lastFrameAt = now;

  if (game.screen == ScreenMode::FLIGHT) {
    updateFlight(deltaSeconds, now);
  }

  renderFrame(now);
  updateLed(now);

  button.clearEvents();
}
