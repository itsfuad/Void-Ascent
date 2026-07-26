// =============================================================================
// System imports
// =============================================================================

#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <cstdint>
#include <esp_system.h>
#include <math.h>
#include <string.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// =============================================================================
// Self files
// =============================================================================

#include "VoidAscentGame.h"

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

#define DISPLAY_BRIGHTNESS 50 // 0-100 percent
#define ROCKET_POSITION_ADJUST_Y 25.0f
#define ROCKET_POSITION_ADJUST_START_ALTITUDE 12.0f
#define ROCKET_POSITION_ADJUST_TRANSITION_ALTITUDE 24.0f
#define ROCKET_STAGE_ZOOM_PER_SEPARATION 0.26f
#define ROCKET_MAX_SCALE 2.45f

static_assert(DISPLAY_BRIGHTNESS >= 0 && DISPLAY_BRIGHTNESS <= 100,
              "DISPLAY_BRIGHTNESS must be in the 0-100 range");
static_assert(ROCKET_POSITION_ADJUST_Y >= -SCREEN_H &&
                  ROCKET_POSITION_ADJUST_Y <= SCREEN_H,
              "ROCKET_POSITION_ADJUST_Y must stay within one screen height");
static_assert(ROCKET_POSITION_ADJUST_TRANSITION_ALTITUDE > 0.0f,
              "ROCKET_POSITION_ADJUST_TRANSITION_ALTITUDE must be positive");

// Runtime switch for all onboard RGB status indication.
static bool onboardLedEnabled = true;
static constexpr uint8_t BACKLIGHT_CHANNEL = 0;

#define LCD_ROTATION 2
#define LCD_SPI_HZ 40000000UL

Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(
    PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, GFX_NOT_DEFINED);

Arduino_GFX *gfx = new Arduino_ST7789(lcdBus, PIN_LCD_RST, LCD_ROTATION, true,
                                      LCD_NATIVE_W, LCD_NATIVE_H, 34, 0, 34, 0);

static GFXcanvas16 frame(SCREEN_W, SCREEN_H);

// The assembled rocket is drawn upright into one local layer, then that
// completed image is rotated once. This prevents independently rasterised
// stage details from opening seams at shallow pitch angles.
static constexpr int16_t ROCKET_LAYER_W = 80;
static constexpr int16_t ROCKET_LAYER_H = 288;
static constexpr int16_t ROCKET_LAYER_PIVOT_X = ROCKET_LAYER_W / 2;
static constexpr int16_t ROCKET_LAYER_PIVOT_Y = 128;
static GFXcanvas16 rocketLayer(ROCKET_LAYER_W, ROCKET_LAYER_H);
static Adafruit_GFX *rocketRenderTarget = &frame;

static Adafruit_NeoPixel rgbLed(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);

static Preferences preferences;

// =============================================================================
// Timing and camera
// =============================================================================

static constexpr uint32_t FRAME_INTERVAL_MS = 33;
static constexpr float MAX_FRAME_DELTA_SECONDS = 0.08f;

// Faster visual world movement.
static constexpr float WORLD_PIXELS_PER_ALTITUDE = 5.80f;

// Faster camera response makes acceleration clearly visible.
static constexpr float CAMERA_FOLLOW_SPEED = 9.0f;

// Prevents the camera from lagging too far behind.
static constexpr float CAMERA_MAX_LAG_ALTITUDE = 1.15f;

// Body-only screen-space centre used after the initial free rise. Exhaust is
// intentionally excluded so plume length cannot pull the vehicle upward.
static constexpr float ROCKET_BODY_TRACKING_CENTRE_Y = 140.0f;

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
static constexpr uint16_t C_WHITE = C565(242, 245, 239);
static constexpr uint16_t C_MUTED = C565(136, 149, 164);

static constexpr uint16_t C_GREEN = C565(83, 235, 166);
static constexpr uint16_t C_FUEL = C565(89, 246, 178);
static constexpr uint16_t C_CYAN = C565(54, 185, 216);
static constexpr uint16_t C_BLUE = C565(64, 123, 238);
static constexpr uint16_t C_PURPLE = C565(153, 91, 238);

static constexpr uint16_t C_YELLOW = C565(255, 213, 75);
static constexpr uint16_t C_ORANGE = C565(250, 125, 43);
static constexpr uint16_t C_RED = C565(242, 49, 61);
static constexpr uint16_t C_DARK_RED = C565(126, 34, 38);

static constexpr uint16_t C_PAPER = C565(218, 226, 226);
static constexpr uint16_t C_HIGHLIGHT = C565(232, 236, 230);
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

enum class FlightType : uint8_t { TEST_FLIGHT, OUTER_SPACE };

static constexpr uint8_t MAX_STAGE_COUNT = 4;
static constexpr float BASE_POWERED_SECONDS = 12.40f;
static constexpr float EXTRA_POWERED_SECONDS_PER_STAGE = 0.75f;
static constexpr float SURFACE_GRAVITY = 2.35f;
static constexpr float LIFTOFF_VELOCITY = 1.35f;
static constexpr float PERFECT_STAGE_VELOCITY_RETAINED = 0.995f;

struct StageDefinition {
  const char *name;

  uint8_t width;
  uint8_t height;

  float thrust;

  uint16_t bandColour;
};

struct MissionDefinition {
  const char *code;
  const char *name;

  const char *objectiveLine1;
  const char *objectiveLine2;

  uint16_t targetAltitude;
  uint16_t displayTargetAltitude;
  bool recoverCapsule;

  FlightType flightType;

  StageDefinition stages[MAX_STAGE_COUNT];

  uint8_t stageCount() const {
    uint8_t count = 0;
    while (count < MAX_STAGE_COUNT && stages[count].name != nullptr) {
      count++;
    }
    return count;
  }
};

#include "MissionValidation.h"

// CREATE_MISSION keeps the table readable while giving each entry a
// code-specific compile-time reachability check. STAGE keeps each stage's
// commas inside one macro argument.
#define STAGE(name, width, height, thrust, bandColour) \
  StageDefinition{name, width, height, thrust, bandColour}
#define CREATE_MISSION(code, name, objective1, objective2, target, display, \
                       recover, flightType, ...)                             \
  []() constexpr {                                                           \
    constexpr MissionDefinition mission = {                                  \
        code, name, objective1, objective2, target, display, recover,       \
        flightType,                                                           \
        {__VA_ARGS__}};                                                       \
    static_assert(missionValidationCanReachTarget(mission),                  \
                  code " target altitude exceeds its ideal-flight apogee"); \
    return mission;                                                           \
  }()

static constexpr MissionDefinition MISSIONS[] = {
    CREATE_MISSION(
        "T-01", "TEST FLIGHT", "VALIDATE THE STACK", "RECOVER THE CAPSULE",
        320, 45, true, FlightType::TEST_FLIGHT,
        STAGE("BOOSTER", 31, 68, 7.00f, C_ORANGE),
        STAGE("UPPER", 22, 50, 5.70f, C_CYAN)),

    CREATE_MISSION(
        "K-100", "KARMAN RUN", "CROSS OUTER SPACE", "PAYLOAD CONTINUES",
        520, 85, false, FlightType::OUTER_SPACE,
        STAGE("BOOSTER", 32, 70, 7.40f, C_ORANGE),
        STAGE("UPPER", 23, 53, 6.10f, C_BLUE)),

    CREATE_MISSION(
        "P-180", "ORBITAL PROBE", "REACH HIGH SPACE", "DEPLOY THE PROBE",
        760, 135, false, FlightType::OUTER_SPACE,
        STAGE("BOOSTER", 33, 66, 8.20f, C_ORANGE),
        STAGE("CORE", 25, 50, 7.10f, C_BLUE),
        STAGE("UPPER", 18, 38, 6.10f, C_PURPLE)),

    CREATE_MISSION(
        "C-220", "CREW QUAL", "COMPLETE CREW ASCENT", "RECOVER THE CAPSULE",
        1000, 190, true, FlightType::OUTER_SPACE,
        STAGE("BOOSTER", 34, 68, 8.50f, C_ORANGE),
        STAGE("CORE", 26, 52, 7.30f, C_BLUE),
        STAGE("UPPER", 19, 39, 6.20f, C_GREEN)),

    CREATE_MISSION(
        "H-340", "HEAVY ASCENT", "FOUR-STAGE FLIGHT", "REACH DEEP SPACE",
        1350, 260, false, FlightType::OUTER_SPACE,
        STAGE("BOOSTER", 35, 58, 9.40f, C_ORANGE),
        STAGE("CORE", 29, 46, 8.10f, C_BLUE),
        STAGE("UPPER", 23, 36, 7.00f, C_GREEN),
        STAGE("KICK", 17, 28, 6.10f, C_PURPLE)),

    CREATE_MISSION(
        "V-520", "VOID RELAY", "CROSS THE VOID", "SEND THE SIGNAL",
        1750, 340, false, FlightType::OUTER_SPACE,
        STAGE("BOOSTER", 36, 60, 10.20f, C_ORANGE),
        STAGE("CORE", 30, 48, 9.00f, C_BLUE),
        STAGE("UPPER", 24, 37, 7.80f, C_GREEN),
        STAGE("KICK", 18, 27, 6.80f, C_PURPLE)),

    CREATE_MISSION(
        "D-760", "DEEP RANGE", "REACH THE DARK SIDE", "NO RECOVERY WINDOW",
        2250, 440, false, FlightType::OUTER_SPACE,
        STAGE("BOOSTER", 37, 58, 11.20f, C_ORANGE),
        STAGE("CORE", 31, 46, 9.90f, C_BLUE),
        STAGE("UPPER", 25, 35, 8.70f, C_GREEN),
        STAGE("KICK", 18, 25, 7.60f, C_PURPLE)),

    CREATE_MISSION(
        "X-1000", "FINAL ASCENT", "MAXIMUM ALTITUDE", "TOUCH THE VOID",
        2800, 560, false, FlightType::OUTER_SPACE,
        STAGE("BOOSTER", 38, 56, 12.40f, C_ORANGE),
        STAGE("CORE", 32, 44, 11.00f, C_BLUE),
        STAGE("UPPER", 26, 33, 9.80f, C_GREEN),
        STAGE("KICK", 19, 24, 8.70f, C_PURPLE)),
};

#undef CREATE_MISSION
#undef STAGE

static constexpr uint8_t MISSION_COUNT = sizeof(MISSIONS) / sizeof(MISSIONS[0]);

class FlightTimingModel {
public:
  float totalPoweredSeconds(const MissionDefinition &mission) const {
    // Two-, three- and four-stage stacks stay within a 12 percent total burn
    // range while giving higher-stage missions shorter individual windows.
    return BASE_POWERED_SECONDS +
           maxFloat(0.0f, mission.stageCount() - 2.0f) *
               EXTRA_POWERED_SECONDS_PER_STAGE;
  }

  float stageBurnSeconds(const MissionDefinition &mission,
                         uint8_t stageIndex) const {
    uint8_t stageCount = mission.stageCount();
    if (stageIndex >= stageCount) {
      return 0.0f;
    }

    float totalHeight = 0.0f;
    for (uint8_t index = 0; index < stageCount; index++) {
      totalHeight += mission.stages[index].height;
    }

    return totalPoweredSeconds(mission) * mission.stages[stageIndex].height /
           maxFloat(1.0f, totalHeight);
  }

  float lateGraceSeconds(const MissionDefinition &mission,
                         uint8_t stageIndex) const {
    return clampFloat(stageBurnSeconds(mission, stageIndex) * 0.16f, 0.45f,
                      0.85f);
  }
};

static const FlightTimingModel flightTiming;

// =============================================================================
// Runtime types
// =============================================================================

enum class ScreenMode : uint8_t { SPLASH, BRIEFING, FLIGHT, RESULT };

enum class FlightPhase : uint8_t {
  COUNTDOWN,
  BURNING,
  COASTING,
  APOGEE,
  REENTRY,
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

struct BoundingBox {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;

  bool intersectsScreen() const {
    return right >= 0.0f && left < SCREEN_W && bottom >= 0.0f && top < SCREEN_H;
  }
};

// Independent drawable entities share one screen-space visibility contract.
// No virtual methods are needed because these fixed-size pools are never
// accessed polymorphically.
class SceneObject {
public:
  BoundingBox bounds;

  bool isVisibleOnScreen() const { return bounds.intersectsScreen(); }

  void setBoundingBox(float left, float top, float right, float bottom) {
    bounds.left = left;
    bounds.top = top;
    bounds.right = right;
    bounds.bottom = bottom;
  }
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

struct Particle : public SceneObject {
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

struct Fragment : public SceneObject {
  bool active = false;
  bool cameraTracked = false;

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
  float gravity = 35.0f;
  float cameraScale = 1.0f;
  float worldVelocity = 0.0f;
  float worldAltitude = 0.0f;
  float mass = 1.0f;
  float minimumDragArea = 1.0f;
  float maximumDragArea = 1.0f;

  uint16_t bodyColour = C_PAPER;
  uint16_t bandColour = C_ORANGE;

  float fuelFraction = 0.0f;
  int8_t bandY = -1;
};

struct Star : public SceneObject {
  int16_t x = 0;
  int16_t y = 0;

  uint8_t size = 1;
  float parallax = 1.0f;
};

struct Cloud : public SceneObject {
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

struct RocketGeometry : public SceneObject {
  float noseY = 0.0f;
  float bottomY = 0.0f;
  float visualScale = 1.0f;
  float pitchRadians = 0.0f;

  float payloadX = 0.0f;
  float payloadY = 0.0f;
  float payloadWidth = 0.0f;
  float payloadHeight = 0.0f;

  uint8_t stageCount = 0;
  StageGeometry stages[MAX_STAGE_COUNT];
};

class LaunchPadObject : public SceneObject {
public:
  void place(float cameraAltitude) {
    baseY = 274.0f + cameraAltitude * WORLD_PIXELS_PER_ALTITUDE;

    // Includes the upper warning light and the complete ground fill.
    setBoundingBox(0.0f, baseY - 170.0f, SCREEN_W - 1.0f, baseY + 113.0f);
  }

  float y() const { return baseY; }

private:
  float baseY = 274.0f;
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
  bool stageBurnoutCueShown = false;

  float altitude = 0.0f;
  float maximumAltitude = 0.0f;
  float velocity = 0.0f;

  float launchNoseY = 0.0f;
  float trackingNoseY = 0.0f;

  float centeringOffset = 0.0f;
  float centeringTarget = 0.0f;

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

  float transitionStartAltitude = 0.0f;
  float reentryHeat = 0.0f;

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
static LaunchPadObject launchPad;

static uint32_t lastFrameAt = 0;

struct SceneProjection {
  float earthRadius = 6000.0f;
  float earthCentreY = 6284.0f;
  float rocketCentreX = SCREEN_W * 0.5f;
  float rocketScale = 1.0f;
  float rocketPitchRadians = 0.0f;
};

class SceneProjectionModel {
public:
  SceneProjection atAltitude(float displayedAltitude) const {
    float altitude = maxFloat(0.0f, displayedAltitude);
    float distance = 1.0f + altitude / 18.0f;

    SceneProjection projection;
    projection.earthRadius = 42.0f + (6000.0f - 42.0f) / powf(distance, 1.55f);

    float surfaceY =
        lerpFloat(284.0f, 292.0f, smoothStep(0.0f, 115.0f, altitude));
    float curvatureLift = 32.0f * smoothStep(150.0f, 340.0f, altitude);
    projection.earthCentreY = surfaceY + projection.earthRadius - curvatureLift;

    projection.rocketScale =
        lerpFloat(1.0f, 0.75f, smoothStep(145.0f, 340.0f, altitude));

    // A quadratic downrange path starts nearly vertical, then becomes visibly
    // lateral as the gravity turn develops. It is altitude-driven, so coasting
    // or waiting on an empty stage cannot advance it independently.
    float pathAmount = clamp01((altitude - 15.0f) / 325.0f);
    projection.rocketCentreX =
        SCREEN_W * 0.5f + 42.0f * pathAmount * pathAmount;
    projection.rocketPitchRadians =
        0.175f * smoothStep(32.0f, 205.0f, altitude);
    return projection;
  }
};

static const SceneProjectionModel sceneProjectionModel;

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

static void fillTranslucentRoundRect(int16_t x, int16_t y, int16_t width,
                                     int16_t height, int16_t radius,
                                     uint16_t colour, uint8_t opacity) {
  for (int16_t row = 0; row < height; row++) {
    int16_t edgeDistance = minInt16(row, height - 1 - row);
    int16_t inset = 0;
    if (edgeDistance < radius) {
      float circleY = radius - edgeDistance;
      inset = radius - (int16_t)floorf(sqrtf(maxFloat(
                           0.0f, radius * radius - circleY * circleY)));
    }

    for (int16_t column = inset; column < width - inset; column++) {
      int16_t pixelX = x + column;
      int16_t pixelY = y + row;
      frame.drawPixel(
          pixelX, pixelY,
          blend565(frame.getPixel(pixelX, pixelY), colour, opacity));
    }
  }
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

static float currentRocketVisualScale();
static float gravityAtAltitude(float altitude);
static float atmosphereDensityAtAltitude(float altitude);

// =============================================================================
// Particle system
// =============================================================================

// Particles and fragments store screen-space positions. World-camera movement
// shifts them once per frame; camera-tracked stages additionally retain a
// world-space vertical velocity so separation motion remains physical.

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
      particle.velocityX *= 0.970f;
      particle.velocityY *= 0.968f;
      particle.velocityY -= 1.5f * deltaSeconds;
    } else if (particle.type == ParticleType::FIRE) {
      particle.velocityX *= 0.965f;
      particle.velocityY += 7.0f * deltaSeconds;
    } else {
      particle.velocityY += 20.0f * deltaSeconds;
    }
  }
}

static void drawParticles() {
  for (uint8_t index = 0; index < MAX_PARTICLES; index++) {
    Particle &particle = particles[index];

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

      particle.setBoundingBox(x - radius, y - radius, x + radius, y + radius);
      if (!particle.isVisibleOnScreen()) {
        continue;
      }

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

      particle.setBoundingBox(x - radius - 1, y - radius - 1, x + radius + 1,
                              y + radius + 1);
      if (!particle.isVisibleOnScreen()) {
        continue;
      }

      frame.fillCircle(x, y, radius + 1, C_RED);

      frame.fillCircle(x, y, radius, particle.colour);

      if (remaining > 0.55f) {
        frame.drawPixel(x, y, C_WHITE);
      }
    } else {
      int16_t tailX = x - (int16_t)(particle.velocityX * 0.035f);

      int16_t tailY = y - (int16_t)(particle.velocityY * 0.035f);

      particle.setBoundingBox(minInt16(x, tailX), minInt16(y, tailY),
                              maxInt16(x, tailX), maxInt16(y, tailY));
      if (!particle.isVisibleOnScreen()) {
        continue;
      }

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

static Fragment *createFragment(FragmentType type, float x, float y,
                                float width, float height, float velocityX,
                                float velocityY, float rotation, float spin,
                                float life, uint16_t bandColour,
                                float fuelFraction, int8_t bandY) {
  Fragment *fragment = allocateFragment();

  fragment->type = type;
  fragment->cameraTracked = false;
  fragment->x = x;
  fragment->y = y;
  fragment->width = width;
  fragment->height = height;
  fragment->velocityX = velocityX;
  fragment->velocityY = velocityY;
  fragment->rotation = rotation;
  fragment->spin = spin;
  fragment->life = life;
  fragment->gravity = 35.0f;
  fragment->cameraScale = 1.0f;
  fragment->worldVelocity = 0.0f;
  fragment->worldAltitude = 0.0f;
  fragment->mass = 1.0f;
  fragment->minimumDragArea = 1.0f;
  fragment->maximumDragArea = 1.0f;
  fragment->bodyColour = C_PAPER;
  fragment->bandColour = bandColour;
  fragment->fuelFraction = clamp01(fuelFraction);
  fragment->bandY = bandY;

  return fragment;
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

    if (fragment.life <= 0.0f) {
      fragment.active = false;
      continue;
    }

    if (fragment.type == FragmentType::WHOLE_STAGE) {
      float newCameraScale = currentRocketVisualScale();
      if (newCameraScale < fragment.cameraScale - 0.001f) {
        float ratio = newCameraScale / maxFloat(0.01f, fragment.cameraScale);
        fragment.width *= ratio;
        fragment.height *= ratio;
        if (fragment.bandY >= 0) {
          fragment.bandY =
              (int8_t)maxInt16(1, (int16_t)roundf(fragment.bandY * ratio));
        }
        fragment.cameraScale = newCameraScale;
      }
    }

    fragment.x += fragment.velocityX * deltaSeconds;

    if (fragment.cameraTracked) {
      float gravity = gravityAtAltitude(fragment.worldAltitude);
      float density = atmosphereDensityAtAltitude(fragment.worldAltitude);
      float tumble = fabsf(sinf(fragment.rotation));
      float dragArea =
          lerpFloat(fragment.minimumDragArea, fragment.maximumDragArea, tumble);
      float dragCoefficient = lerpFloat(0.72f, 1.35f, tumble);
      float speedSquared = fragment.worldVelocity * fragment.worldVelocity;

      // Fd = 1/2 rho Cd A v^2 and a = F/m. The scale maps the arcade world
      // units to a readable ballistic fall without changing the relationship.
      float dragAcceleration = 0.5f * density * dragCoefficient * dragArea *
                               speedSquared / maxFloat(1.0f, fragment.mass) *
                               0.0014f;
      dragAcceleration = minFloat(5.0f, dragAcceleration);

      float acceleration = -gravity;
      if (fragment.worldVelocity > 0.0f) {
        acceleration -= dragAcceleration;
      } else if (fragment.worldVelocity < 0.0f) {
        acceleration += dragAcceleration;
      }

      fragment.worldVelocity += acceleration * deltaSeconds;
      fragment.worldAltitude = maxFloat(
          0.0f, fragment.worldAltitude + fragment.worldVelocity * deltaSeconds);
      fragment.y -=
          fragment.worldVelocity * WORLD_PIXELS_PER_ALTITUDE * deltaSeconds;
    } else {
      fragment.velocityY += fragment.gravity * deltaSeconds;
      fragment.y += fragment.velocityY * deltaSeconds;
    }

    fragment.rotation += fragment.spin * deltaSeconds;
  }
}

static void drawFragmentFuelWindow(const Fragment &fragment) {
  if (fragment.type == FragmentType::SIDE_PANEL ||
      fragment.type == FragmentType::ENGINE ||
      fragment.type == FragmentType::CAPSULE) {
    return;
  }

  float scale = fragment.cameraScale;
  float windowWidth = maxFloat(2.0f, 5.0f * scale);
  float windowHeight = maxFloat(3.0f * scale, fragment.height - 6.0f * scale);

  fillRotatedRectangle(fragment.x, fragment.y, windowWidth, windowHeight,
                       fragment.rotation, C_GLASS_EDGE);

  fillRotatedRectangle(fragment.x, fragment.y, maxFloat(1.0f, 3.0f * scale),
                       maxFloat(1.0f, windowHeight - 2.0f * scale),
                       fragment.rotation, C_GLASS);

  float fuelHeight =
      maxFloat(0.0f, windowHeight - 2.0f * scale) * fragment.fuelFraction;

  if (fuelHeight > 0.5f) {
    float localCentreY = windowHeight * 0.5f - fuelHeight * 0.5f - scale;

    PointF fuelCentre = rotatePoint(0.0f, localCentreY, fragment.x, fragment.y,
                                    fragment.rotation);

    fillRotatedRectangle(fuelCentre.x, fuelCentre.y,
                         maxFloat(1.0f, 3.0f * scale), fuelHeight,
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

    fillRotatedRectangle(bandCentre.x, bandCentre.y, fragment.width,
                         maxFloat(1.0f, 3.0f * fragment.cameraScale),
                         fragment.rotation, fragment.bandColour);
  }

  drawFragmentFuelWindow(fragment);

  if (fragment.type == FragmentType::WHOLE_STAGE) {
    PointF engineCentre =
        rotatePoint(0.0f, fragment.height * 0.5f + fragment.width * 0.13f,
                    fragment.x, fragment.y, fragment.rotation);
    fillRotatedRectangle(engineCentre.x, engineCentre.y,
                         maxFloat(3.0f, fragment.width * 0.46f),
                         maxFloat(2.0f, fragment.width * 0.26f),
                         fragment.rotation, C_METAL_DARK);
  }
}

static void drawFragments() {
  for (uint8_t index = 0; index < MAX_FRAGMENTS; index++) {
    Fragment &fragment = fragments[index];
    if (!fragment.active) {
      continue;
    }

    float sine = fabsf(sinf(fragment.rotation));
    float cosine = fabsf(cosf(fragment.rotation));
    float extentX =
        fragment.width * 0.5f * cosine + fragment.height * 0.5f * sine;
    float extentY =
        fragment.width * 0.5f * sine + fragment.height * 0.5f * cosine;
    float detailMargin =
        maxFloat(4.0f * fragment.cameraScale, fragment.width * 0.20f);
    fragment.setBoundingBox(fragment.x - extentX - detailMargin,
                            fragment.y - extentY - detailMargin,
                            fragment.x + extentX + detailMargin,
                            fragment.y + extentY + detailMargin);

    if (fragment.isVisibleOnScreen()) {
      drawFragment(fragment);
    }
  }
}

// =============================================================================
// Rocket geometry
// =============================================================================

static const MissionDefinition &currentMission() {
  return MISSIONS[game.selectedMission];
}

static float missionDisplayTargetAltitude() {
  return currentMission().displayTargetAltitude;
}

static float missionProgressAt(float internalAltitude) {
  return clampFloat(internalAltitude /
                        maxFloat(1.0f, (float)currentMission().targetAltitude),
                    0.0f, 1.20f);
}

static float displayedAltitudeAt(float internalAltitude) {
  return missionDisplayTargetAltitude() * missionProgressAt(internalAltitude);
}

static float displayedVelocityAt(float internalVelocity) {
  return internalVelocity * missionDisplayTargetAltitude() /
         maxFloat(1.0f, (float)currentMission().targetAltitude);
}

static float currentRocketVisualScale() {
  return sceneProjectionModel.atAltitude(displayedAltitudeAt(game.altitude))
      .rocketScale;
}

static const StageDefinition *currentStage() {
  const MissionDefinition &mission = currentMission();

  if (game.activeStage >= mission.stageCount()) {
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

  for (int8_t index = (int8_t)mission.stageCount() - 1;
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
  // Build unscaled screen geometry from the rocket-local stack first.
  geometry.noseY = noseY;
  geometry.visualScale = 1.0f;
  geometry.pitchRadians = 0.0f;

  geometry.payloadWidth = 20.0f;
  geometry.payloadHeight = payloadHeight(mission);

  geometry.payloadX = SCREEN_W * 0.5f - geometry.payloadWidth * 0.5f;

  geometry.payloadY = noseY;

  float y = noseY + geometry.payloadHeight;

  geometry.stageCount = 0;

  uint8_t stageCount = mission.stageCount();
  for (int8_t index = (int8_t)stageCount - 1;
       index >= (int8_t)activeStage; index--) {
    if (geometry.stageCount >= MAX_STAGE_COUNT) {
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

static void scaleRocketGeometry(RocketGeometry &geometry, float scale) {
  // Apply the sole rocket cinematic transform in screen space, around the
  // complete remaining vehicle's visual centre.
  scale = clampFloat(scale, 0.65f, ROCKET_MAX_SCALE);

  float anchorX = SCREEN_W * 0.5f;
  float anchorY = (geometry.noseY + geometry.bottomY) * 0.5f;

  geometry.payloadX = anchorX + (geometry.payloadX - anchorX) * scale;
  geometry.payloadY = anchorY + (geometry.payloadY - anchorY) * scale;
  geometry.payloadWidth *= scale;
  geometry.payloadHeight *= scale;

  for (uint8_t index = 0; index < geometry.stageCount; index++) {
    StageGeometry &stage = geometry.stages[index];
    stage.x = anchorX + (stage.x - anchorX) * scale;
    stage.y = anchorY + (stage.y - anchorY) * scale;
    stage.width *= scale;
    stage.height *= scale;
  }

  geometry.noseY = anchorY + (geometry.noseY - anchorY) * scale;
  geometry.bottomY = anchorY + (geometry.bottomY - anchorY) * scale;
  geometry.visualScale = scale;
}

static void buildCurrentRocketGeometry(RocketGeometry &geometry) {
  const MissionDefinition &mission = currentMission();
  buildRocketGeometryFor(mission, game.activeStage, currentRocketNoseY(),
                         geometry);
  SceneProjection projection =
      sceneProjectionModel.atAltitude(displayedAltitudeAt(game.altitude));
  uint8_t separatedStages = game.activeStage;
  uint8_t finalStage = mission.stageCount() - 1;
  if (separatedStages > finalStage) {
    separatedStages = finalStage;
  }
  float stageZoom = 1.0f + separatedStages * ROCKET_STAGE_ZOOM_PER_SEPARATION;
  scaleRocketGeometry(geometry, projection.rocketScale * stageZoom);

  float offsetX = projection.rocketCentreX - SCREEN_W * 0.5f;
  float positionAdjustAmount = smoothStep(
      ROCKET_POSITION_ADJUST_START_ALTITUDE,
      ROCKET_POSITION_ADJUST_START_ALTITUDE +
          ROCKET_POSITION_ADJUST_TRANSITION_ALTITUDE,
      game.altitude);
  float offsetY = ROCKET_POSITION_ADJUST_Y * positionAdjustAmount;
  geometry.payloadX += offsetX;
  geometry.payloadY += offsetY;
  geometry.noseY += offsetY;
  geometry.bottomY += offsetY;
  for (uint8_t index = 0; index < geometry.stageCount; index++) {
    geometry.stages[index].x += offsetX;
    geometry.stages[index].y += offsetY;
  }
  geometry.pitchRadians = projection.rocketPitchRadians;
}

static float currentFuelFraction() {
  const StageDefinition *stage = currentStage();

  if (stage == nullptr) {
    return 0.0f;
  }

  float burnSeconds =
      flightTiming.stageBurnSeconds(currentMission(), game.activeStage);
  return clamp01(1.0f - game.stageElapsed / maxFloat(0.01f, burnSeconds));
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
    Star &star = stars[index];

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

    star.setBoundingBox(star.x, screenY, star.x + star.size - 1,
                        screenY + star.size - 1);
    if (!star.isVisibleOnScreen()) {
      continue;
    }

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
    Cloud &cloud = clouds[index];

    float layerScale =
        WORLD_PIXELS_PER_ALTITUDE * (0.52f + cloud.layer * 0.075f);

    float screenY =
        286.0f - (cloud.worldAltitude - cameraAltitude) * layerScale;

    int16_t drift = (int16_t)(sinf(now * 0.00018f + index * 1.7f) * 4.0f);
    float centreX = cloud.x + drift;
    cloud.setBoundingBox(centreX - cloud.size, screenY - cloud.size,
                         centreX + cloud.size, screenY + cloud.size);
    if (!cloud.isVisibleOnScreen()) {
      continue;
    }

    float topVisibility = smoothStep(-40.0f, 12.0f, screenY);

    float bottomVisibility =
        1.0f - smoothStep(SCREEN_H - 34.0f, SCREEN_H + 40.0f, screenY);

    float visibility = atmosphereVisibility * topVisibility * bottomVisibility;

    if (visibility <= 0.01f) {
      continue;
    }

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
// Projected Earth rendering
// =============================================================================

struct EarthLandPoint {
  float x;
  float y;
};

static bool projectedEarthBoundsAtY(float centreX, float centreY, float radius,
                                    int16_t y, int16_t &left, int16_t &right) {
  float deltaY = (float)y - centreY;
  float inside = radius * radius - deltaY * deltaY;

  if (inside <= 0.0f) {
    return false;
  }

  float halfWidth = sqrtf(inside);
  int32_t rawLeft = (int32_t)floorf(centreX - halfWidth);
  int32_t rawRight = (int32_t)ceilf(centreX + halfWidth);

  if (rawRight < 0 || rawLeft >= SCREEN_W) {
    return false;
  }

  left = (int16_t)maxFloat(0.0f, (float)rawLeft);
  right = (int16_t)minFloat((float)(SCREEN_W - 1), (float)rawRight);

  return right >= left;
}

static void drawProjectedEarthOcean(float centreX, float centreY, float radius,
                                    float earthVisibility, float skyAltitude) {
  int16_t startY = maxInt16(0, (int16_t)floorf(centreY - radius));
  int16_t endY = minInt16(SCREEN_H - 1, (int16_t)ceilf(centreY + radius));

  for (int16_t y = startY; y <= endY; y++) {
    float deltaY = (float)y - centreY;
    float inside = radius * radius - deltaY * deltaY;

    if (inside <= 0.0f) {
      continue;
    }

    float halfWidth = sqrtf(inside);
    int32_t rawLeft = (int32_t)floorf(centreX - halfWidth);
    int32_t rawRight = (int32_t)ceilf(centreX + halfWidth);

    if (rawRight < 0 || rawLeft >= SCREEN_W) {
      continue;
    }

    int16_t left = maxInt16(0, (int16_t)rawLeft);
    int16_t right = minInt16(SCREEN_W - 1, (int16_t)rawRight);
    float normalY = clampFloat(deltaY / radius, -1.0f, 1.0f);
    float depth = (normalY + 1.0f) * 0.5f;

    uint16_t ocean = blend565(C_OCEAN_LIGHT, C_OCEAN_DEEP,
                              (uint8_t)(58.0f + depth * 132.0f));
    uint16_t sky = skyColourAtY(skyAltitude, y);
    ocean = blend565(sky, ocean, (uint8_t)(clamp01(earthVisibility) * 255.0f));
    frame.drawFastHLine(left, y, right - left + 1, ocean);

    // A subtle limb only when the actual sides of the globe are visible.
    if (radius < 220.0f) {
      int16_t limbWidth = maxInt16(1, (int16_t)((right - left + 1) / 20));
      if (rawLeft >= 0) {
        frame.drawFastHLine(left, y, limbWidth, blend565(ocean, C_VOID, 48));
      }
      if (rawRight < SCREEN_W) {
        frame.drawFastHLine(right - limbWidth + 1, y, limbWidth,
                            blend565(ocean, C_OCEAN_LIGHT, 46));
      }
    }
  }
}

static PointF projectEarthLandPoint(float sphereX, float sphereY,
                                    float sphereRadius,
                                    const EarthLandPoint &point,
                                    float longitudeShift) {
  float latitude = clampFloat(point.y, -0.92f, 0.92f);
  float longitude = clampFloat(point.x + longitudeShift, -0.94f, 0.94f);
  float rowProjection = sqrtf(maxFloat(0.0f, 1.0f - latitude * latitude));

  PointF result;
  result.x = sphereX + longitude * sphereRadius * rowProjection;
  result.y = sphereY + latitude * sphereRadius;
  return result;
}

static void drawProjectedEarthLandMass(float sphereX, float sphereY,
                                       float sphereRadius,
                                       const EarthLandPoint *points,
                                       uint8_t pointCount,
                                       uint16_t targetColour,
                                       float longitudeShift, float visibility) {
  if (pointCount < 3) {
    return;
  }

  EarthLandPoint centre = {0.0f, 0.0f};
  for (uint8_t index = 0; index < pointCount; index++) {
    centre.x += points[index].x;
    centre.y += points[index].y;
  }
  centre.x /= pointCount;
  centre.y /= pointCount;

  PointF projectedCentre = projectEarthLandPoint(sphereX, sphereY, sphereRadius,
                                                 centre, longitudeShift);
  uint16_t colour =
      blend565(C_OCEAN, targetColour, (uint8_t)(visibility * 225.0f));

  for (uint8_t index = 0; index < pointCount; index++) {
    uint8_t next = (uint8_t)((index + 1) % pointCount);
    PointF first = projectEarthLandPoint(sphereX, sphereY, sphereRadius,
                                         points[index], longitudeShift);
    PointF second = projectEarthLandPoint(sphereX, sphereY, sphereRadius,
                                          points[next], longitudeShift);

    frame.fillTriangle(
        (int16_t)roundf(projectedCentre.x), (int16_t)roundf(projectedCentre.y),
        (int16_t)roundf(first.x), (int16_t)roundf(first.y),
        (int16_t)roundf(second.x), (int16_t)roundf(second.y), colour);
  }
}

static void drawProjectedEarthDetails(float centreX, float centreY,
                                      float radius, float earthVisibility,
                                      uint32_t now) {
  float globeVisibility = 1.0f - smoothStep(88.0f, 215.0f, radius);
  float detailVisibility = globeVisibility * earthVisibility;
  if (detailVisibility <= 0.01f) {
    return;
  }

  static const EarthLandPoint NORTH_AMERICA[] = {
      {-0.72f, -0.43f}, {-0.52f, -0.52f}, {-0.31f, -0.42f}, {-0.20f, -0.25f},
      {-0.31f, -0.08f}, {-0.47f, -0.04f}, {-0.62f, -0.18f}, {-0.76f, -0.24f}};
  static const EarthLandPoint CENTRAL_AMERICA[] = {{-0.45f, -0.08f},
                                                   {-0.28f, -0.02f},
                                                   {-0.20f, 0.08f},
                                                   {-0.30f, 0.14f},
                                                   {-0.47f, 0.05f}};
  static const EarthLandPoint SOUTH_AMERICA[] = {
      {-0.31f, 0.10f}, {-0.17f, 0.16f}, {-0.12f, 0.32f},
      {-0.23f, 0.58f}, {-0.36f, 0.45f}, {-0.42f, 0.22f}};
  static const EarthLandPoint EURASIA[] = {
      {-0.10f, -0.46f}, {0.12f, -0.55f}, {0.38f, -0.49f}, {0.66f, -0.34f},
      {0.72f, -0.16f},  {0.49f, -0.08f}, {0.34f, 0.02f},  {0.12f, -0.04f},
      {-0.03f, -0.17f}, {-0.18f, -0.28f}};
  static const EarthLandPoint AFRICA[] = {
      {0.02f, -0.12f}, {0.22f, -0.10f}, {0.34f, 0.06f}, {0.25f, 0.35f},
      {0.09f, 0.52f},  {-0.04f, 0.27f}, {-0.12f, 0.02f}};
  static const EarthLandPoint AUSTRALIA[] = {{0.47f, 0.31f},
                                             {0.68f, 0.28f},
                                             {0.76f, 0.43f},
                                             {0.58f, 0.54f},
                                             {0.43f, 0.44f}};
  static const EarthLandPoint GREENLAND[] = {
      {-0.31f, -0.67f}, {-0.18f, -0.72f}, {-0.12f, -0.58f}, {-0.25f, -0.51f}};

  float longitudeShift = sinf(now * 0.000055f) * 0.055f;
  drawProjectedEarthLandMass(centreX, centreY, radius, NORTH_AMERICA,
                             sizeof(NORTH_AMERICA) / sizeof(NORTH_AMERICA[0]),
                             C_LAND, longitudeShift, detailVisibility);
  drawProjectedEarthLandMass(centreX, centreY, radius, CENTRAL_AMERICA,
                             sizeof(CENTRAL_AMERICA) /
                                 sizeof(CENTRAL_AMERICA[0]),
                             C_LAND_LIGHT, longitudeShift, detailVisibility);
  drawProjectedEarthLandMass(centreX, centreY, radius, SOUTH_AMERICA,
                             sizeof(SOUTH_AMERICA) / sizeof(SOUTH_AMERICA[0]),
                             C_LAND_DARK, longitudeShift, detailVisibility);
  drawProjectedEarthLandMass(centreX, centreY, radius, EURASIA,
                             sizeof(EURASIA) / sizeof(EURASIA[0]), C_LAND_LIGHT,
                             longitudeShift, detailVisibility);
  drawProjectedEarthLandMass(centreX, centreY, radius, AFRICA,
                             sizeof(AFRICA) / sizeof(AFRICA[0]), C_LAND,
                             longitudeShift, detailVisibility);
  drawProjectedEarthLandMass(centreX, centreY, radius, AUSTRALIA,
                             sizeof(AUSTRALIA) / sizeof(AUSTRALIA[0]),
                             C_LAND_DARK, longitudeShift, detailVisibility);
  drawProjectedEarthLandMass(centreX, centreY, radius, GREENLAND,
                             sizeof(GREENLAND) / sizeof(GREENLAND[0]),
                             C_LAND_LIGHT, longitudeShift, detailVisibility);

  float cloudVisibility = globeVisibility * globeVisibility * earthVisibility;
  if (cloudVisibility <= 0.03f) {
    return;
  }

  uint16_t cloudColour =
      blend565(C_OCEAN_LIGHT, C_WHITE, (uint8_t)(cloudVisibility * 145.0f));
  static const float CLOUD_LATITUDES[] = {-0.45f, -0.16f, 0.13f, 0.39f};

  for (uint8_t band = 0; band < 4; band++) {
    float phase = now * 0.00022f + band * 1.73f;
    bool previousValid = false;
    int16_t previousX = 0;
    int16_t previousY = 0;

    int16_t xStart = maxInt16(0, (int16_t)(centreX - radius * 0.88f));
    int16_t xEnd = minInt16(SCREEN_W - 1, (int16_t)(centreX + radius * 0.88f));

    for (int16_t x = xStart; x <= xEnd; x += 2) {
      float nx = ((float)x - centreX) / radius;
      float inside = 1.0f - nx * nx;
      if (inside <= 0.0f) {
        previousValid = false;
        continue;
      }

      float wave = sinf(nx * 7.0f + phase) * radius * 0.025f +
                   sinf(nx * 15.0f - phase * 0.7f) * radius * 0.012f;
      int16_t y = (int16_t)roundf(centreY + CLOUD_LATITUDES[band] * radius +
                                  nx * radius * 0.055f + wave);
      float dy = (float)y - centreY;
      bool insideSphere = nx * nx + (dy * dy) / (radius * radius) < 0.94f;
      bool gap = ((x + band * 19 + (int16_t)(phase * 7.0f)) % 23) < 6;

      if (!insideSphere || gap) {
        previousValid = false;
        continue;
      }

      if (previousValid) {
        frame.drawLine(previousX, previousY, x, y, cloudColour);
      } else {
        frame.drawPixel(x, y, cloudColour);
      }

      if ((x + band) % 6 == 0) {
        frame.drawPixel(x, y + 1, cloudColour);
      }

      previousValid = true;
      previousX = x;
      previousY = y;
    }
  }
}

static void drawProjectedEarthAtmosphere(float centreX, float centreY,
                                         float radius, float earthVisibility,
                                         float skyAltitude) {
  float thickness = radius > 240.0f ? 1.0f : 1.35f;
  float outerRadius = radius + thickness;
  float innerRadius = maxFloat(1.0f, radius - 0.5f);
  int16_t startY = maxInt16(0, (int16_t)floorf(centreY - outerRadius));
  int16_t endY = minInt16(SCREEN_H - 1, (int16_t)ceilf(centreY + outerRadius));
  uint8_t amount = (uint8_t)(clamp01(earthVisibility) * 125.0f);

  for (int16_t y = startY; y <= endY; y++) {
    int16_t outerLeft;
    int16_t outerRight;
    if (!projectedEarthBoundsAtY(centreX, centreY, outerRadius, y, outerLeft,
                                 outerRight)) {
      continue;
    }

    uint16_t colour =
        blend565(skyColourAtY(skyAltitude, y), C_ATMOSPHERE, amount);
    int16_t innerLeft;
    int16_t innerRight;
    if (!projectedEarthBoundsAtY(centreX, centreY, innerRadius, y, innerLeft,
                                 innerRight)) {
      frame.drawFastHLine(outerLeft, y, outerRight - outerLeft + 1, colour);
      continue;
    }

    if (innerLeft > outerLeft) {
      frame.drawFastHLine(outerLeft, y, innerLeft - outerLeft, colour);
    }
    if (outerRight > innerRight) {
      frame.drawFastHLine(innerRight + 1, y, outerRight - innerRight, colour);
    }
  }
}

static void drawEarth(float earthAltitude, float skyAltitude,
                      FlightType flightType, uint32_t now) {
  // Earth is projected directly into screen space from world/display altitude;
  // it is independent of the rocket-local cinematic scale.
  (void)flightType;

  SceneProjection projection = sceneProjectionModel.atAltitude(earthAltitude);
  float radius = projection.earthRadius;
  float centreY = projection.earthCentreY;
  float centreX = SCREEN_W * 0.5f;

  drawProjectedEarthOcean(centreX, centreY, radius, 1.0f, skyAltitude);
  drawProjectedEarthDetails(centreX, centreY, radius, 1.0f, now);
  drawProjectedEarthAtmosphere(centreX, centreY, radius, 1.0f, skyAltitude);
}

static void drawBackground(float visualAltitude, float earthAltitude,
                           uint32_t now, FlightType flightType,
                           bool showEarth) {
  drawSkyGradient(visualAltitude);
  drawStars(visualAltitude);
  drawSun(visualAltitude);
  if (showEarth) {
    drawEarth(earthAltitude, visualAltitude, flightType, now);
  }
  drawMountains(visualAltitude);
  drawClouds(visualAltitude, now);
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
  launchPad.place(cameraAltitude);
  if (!launchPad.isVisibleOnScreen()) {
    return;
  }

  int16_t padY = (int16_t)launchPad.y();

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

static int16_t scaledSize(float size, float scale, int16_t minimum = 1) {
  return maxInt16(minimum, (int16_t)roundf(size * scale));
}

struct RocketTransform {
  float pivotX;
  float pivotY;
  float angle;

  PointF apply(float x, float y) const {
    return rotatePoint(x - pivotX, y - pivotY, pivotX, pivotY, angle);
  }
};

static void drawRocketLine(float x1, float y1, float x2, float y2,
                           const RocketTransform &transform, uint16_t colour) {
  PointF first = transform.apply(x1, y1);
  PointF second = transform.apply(x2, y2);
  rocketRenderTarget->drawLine(
      (int16_t)roundf(first.x), (int16_t)roundf(first.y),
      (int16_t)roundf(second.x), (int16_t)roundf(second.y), colour);
}

static void drawRocketPixel(float x, float y, const RocketTransform &transform,
                            uint16_t colour) {
  PointF point = transform.apply(x, y);
  rocketRenderTarget->drawPixel((int16_t)roundf(point.x),
                                (int16_t)roundf(point.y), colour);
}

static void drawPitchedHLine(int16_t x, int16_t y, int16_t width,
                             const RocketTransform &transform,
                             uint16_t colour) {
  drawRocketLine(x, y, x + maxInt16(1, width) - 1, y, transform, colour);
}

static void fillPitchedQuad(float x1, float y1, float x2, float y2, float x3,
                            float y3, float x4, float y4,
                            const RocketTransform &transform, uint16_t colour) {
  PointF first = transform.apply(x1, y1);
  PointF second = transform.apply(x2, y2);
  PointF third = transform.apply(x3, y3);
  PointF fourth = transform.apply(x4, y4);

  rocketRenderTarget->fillTriangle(
      (int16_t)roundf(first.x), (int16_t)roundf(first.y),
      (int16_t)roundf(second.x), (int16_t)roundf(second.y),
      (int16_t)roundf(third.x), (int16_t)roundf(third.y), colour);
  rocketRenderTarget->fillTriangle(
      (int16_t)roundf(first.x), (int16_t)roundf(first.y),
      (int16_t)roundf(third.x), (int16_t)roundf(third.y),
      (int16_t)roundf(fourth.x), (int16_t)roundf(fourth.y), colour);
}

static void fillPitchedRect(int16_t x, int16_t y, int16_t width, int16_t height,
                            const RocketTransform &transform, uint16_t colour) {
  int16_t right = x + maxInt16(1, width) - 1;
  int16_t bottom = y + maxInt16(1, height) - 1;

  if (width <= 1 || height <= 1) {
    drawRocketLine(x, y, right, bottom, transform, colour);
    return;
  }

  fillPitchedQuad(x, y, right, y, right, bottom, x, bottom, transform, colour);
}

static void fillPitchedBody(int16_t x, int16_t y, int16_t width, int16_t height,
                            const RocketTransform &transform, uint16_t colour) {
  fillPitchedRect(x, y, width, height, transform, colour);
}

static void drawPitchedRectOutline(int16_t x, int16_t y, int16_t width,
                                   int16_t height,
                                   const RocketTransform &transform,
                                   uint16_t colour) {
  int16_t right = x + maxInt16(1, width) - 1;
  int16_t bottom = y + maxInt16(1, height) - 1;
  drawRocketLine(x, y, right, y, transform, colour);
  drawRocketLine(right, y, right, bottom, transform, colour);
  drawRocketLine(right, bottom, x, bottom, transform, colour);
  drawRocketLine(x, bottom, x, y, transform, colour);
}

static void drawPayload(const RocketGeometry &geometry, bool recoveryCapsule,
                        const RocketTransform &transform) {
  int16_t x = (int16_t)roundf(geometry.payloadX);

  int16_t y = (int16_t)roundf(geometry.payloadY);

  int16_t width = (int16_t)roundf(geometry.payloadWidth);

  int16_t height = (int16_t)roundf(geometry.payloadHeight);

  int16_t centreX = x + width / 2;
  float scale = geometry.visualScale;
  int16_t one = scaledSize(1.0f, scale);
  int16_t two = scaledSize(2.0f, scale);
  int16_t three = scaledSize(3.0f, scale);
  int16_t four = scaledSize(4.0f, scale);
  int16_t five = scaledSize(5.0f, scale);
  int16_t domeY = y + scaledSize(8.0f, scale);

  PointF domeCentre = transform.apply(centreX, domeY);
  rocketRenderTarget->fillCircle((int16_t)roundf(domeCentre.x),
                                 (int16_t)roundf(domeCentre.y),
                                 maxInt16(2, width / 2 - two), C_PAPER);

  fillPitchedRect(x + two, domeY, maxInt16(2, width - four),
                  maxInt16(2, y + height - domeY), transform, C_PAPER);

  fillPitchedRect(x + three, y + scaledSize(7.0f, scale), three,
                  maxInt16(2, height - scaledSize(10.0f, scale)), transform,
                  C_HIGHLIGHT);

  fillPitchedRect(x + width - five, domeY, two,
                  maxInt16(2, height - scaledSize(10.0f, scale)), transform,
                  C_METAL);

  int16_t windowWidth = scaledSize(8.0f, scale, 4);
  int16_t windowHeight = scaledSize(6.0f, scale, 3);
  fillPitchedRect(centreX - windowWidth / 2, domeY, windowWidth, windowHeight,
                  transform, C_GLASS);

  drawRocketPixel(centreX - two, domeY + one, transform, C_CYAN);
  drawRocketPixel(centreX - one, domeY + one, transform, C_WHITE);

  fillPitchedRect(x + three, y + height - five,
                  maxInt16(2, width - scaledSize(6.0f, scale)), four, transform,
                  recoveryCapsule ? C_CYAN : C_PURPLE);

  drawPitchedHLine(x + two, y + height - one, maxInt16(2, width - four),
                   transform, C_METAL_DARK);
}

static void drawFuelWindow(int16_t stageX, int16_t stageY, int16_t stageWidth,
                           int16_t stageHeight, float fuelFraction, bool active,
                           float scale, const RocketTransform &transform,
                           uint32_t now) {
  int16_t one = scaledSize(1.0f, scale);
  int16_t two = scaledSize(2.0f, scale);
  int16_t windowWidth = scaledSize(6.0f, scale, 3);
  int16_t windowX = stageX + stageWidth / 2 - windowWidth / 2;

  int16_t windowY = stageY + scaledSize(8.0f, scale);

  int16_t windowHeight = stageHeight - scaledSize(17.0f, scale);

  windowHeight = maxInt16(scaledSize(8.0f, scale, 4), windowHeight);

  fillPitchedRect(windowX, windowY, windowWidth, windowHeight, transform,
                  C_GLASS_EDGE);

  fillPitchedRect(windowX + one, windowY + one, maxInt16(1, windowWidth - two),
                  maxInt16(1, windowHeight - two), transform, C_GLASS);

  int16_t insideHeight = maxInt16(1, windowHeight - two);

  int16_t fuelHeight = (int16_t)roundf(insideHeight * clamp01(fuelFraction));

  if (fuelHeight > 0) {
    int16_t fuelY = windowY + one + insideHeight - fuelHeight;

    fillPitchedRect(windowX + one, fuelY, maxInt16(1, windowWidth - two),
                    fuelHeight, transform, C_FUEL);

    drawPitchedHLine(windowX + one, fuelY, maxInt16(1, windowWidth - two),
                     transform, C_WHITE);
  }

  fillPitchedRect(windowX + one, windowY + two, one,
                  maxInt16((int16_t)1, windowHeight / 3), transform,
                  C565(112, 168, 179));

  if (active && fuelFraction < 0.16f && ((now / 110) & 1U) == 0) {
    drawPitchedRectOutline(windowX - one, windowY - one, windowWidth + two,
                           windowHeight + two, transform, C_RED);
  }
}

struct EngineGeometry {
  int16_t centreX;
  int16_t topY;
  int16_t nozzleY;
  int16_t throatWidth;
  int16_t exitWidth;
};

static EngineGeometry engineGeometryFor(const StageGeometry &stage,
                                        float scale) {
  int16_t stageWidth = (int16_t)roundf(stage.width);
  int16_t stageBottomY = (int16_t)roundf(stage.y + stage.height);
  int16_t bellHeight = maxInt16(scaledSize(7.0f, scale, 4),
                                (int16_t)roundf(stage.width * 0.26f));

  return {
      (int16_t)roundf(stage.x + stage.width * 0.5f),
      stageBottomY - scaledSize(1.0f, scale),
      (int16_t)(stageBottomY - scaledSize(1.0f, scale) + bellHeight),
      maxInt16(scaledSize(5.0f, scale, 3), (int16_t)roundf(stageWidth * 0.22f)),
      maxInt16(scaledSize(9.0f, scale, 5),
               (int16_t)roundf(stageWidth * 0.46f))};
}

static void drawEngineAssembly(const StageGeometry &stage, float scale,
                               const RocketTransform &transform) {
  EngineGeometry engine = engineGeometryFor(stage, scale);
  int16_t throatHalfWidth = maxInt16(1, engine.throatWidth / 2);
  int16_t exitHalfWidth = maxInt16(2, engine.exitWidth / 2);

  fillPitchedQuad(engine.centreX - throatHalfWidth, engine.topY,
                  engine.centreX + throatHalfWidth, engine.topY,
                  engine.centreX + exitHalfWidth, engine.nozzleY,
                  engine.centreX - exitHalfWidth, engine.nozzleY, transform,
                  C_METAL_DARK);

  drawRocketLine(engine.centreX - throatHalfWidth, engine.topY,
                 engine.centreX - exitHalfWidth, engine.nozzleY, transform,
                 C_METAL);
  drawRocketLine(engine.centreX - exitHalfWidth, engine.nozzleY,
                 engine.centreX + exitHalfWidth, engine.nozzleY, transform,
                 C_METAL);
  drawRocketLine(engine.centreX + exitHalfWidth, engine.nozzleY,
                 engine.centreX + throatHalfWidth, engine.topY, transform,
                 C_METAL);
}

static void drawStageBody(const StageDefinition &definition,
                          const StageGeometry &geometry, float fuelFraction,
                          bool active, float scale,
                          const RocketTransform &transform, uint32_t now) {
  int16_t x = (int16_t)roundf(geometry.x);

  int16_t y = (int16_t)roundf(geometry.y);

  int16_t width = (int16_t)roundf(geometry.width);

  int16_t height = (int16_t)roundf(geometry.height);
  int16_t one = scaledSize(1.0f, scale);
  int16_t two = scaledSize(2.0f, scale);
  int16_t three = scaledSize(3.0f, scale);
  int16_t four = scaledSize(4.0f, scale);
  int16_t five = scaledSize(5.0f, scale);

  fillPitchedBody(x + two, y + two, width, height, transform, C_BLACK);

  fillPitchedBody(x, y, width, height, transform, C_PAPER);

  fillPitchedRect(x + two, y + two, maxInt16(two, width / 6),
                  maxInt16(2, height - four), transform, C_HIGHLIGHT);

  fillPitchedRect(x + width - four, y + two, two, maxInt16(2, height - four),
                  transform, C_METAL);

  int16_t bandY = y + maxInt16(five, height / 5);

  fillPitchedRect(x, bandY, width, four, transform, definition.bandColour);

  drawPitchedHLine(x, bandY, width, transform,
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

  fillPitchedRect(x - two, y - two, width + four, three, transform,
                  collarColour);

  drawPitchedHLine(x - one, y + one, width + two, transform, C_METAL_DARK);

  fillPitchedRect(x, y + height - five, width, five, transform, C_METAL_DARK);

  drawPitchedHLine(x + two, y + height - five, maxInt16(2, width - four),
                   transform, C_METAL);

  for (int16_t rivetY = y + scaledSize(12.0f, scale);
       rivetY < y + height - scaledSize(8.0f, scale);
       rivetY += scaledSize(13.0f, scale)) {
    drawRocketPixel(x + two, rivetY, transform, C_METAL_DARK);
    drawRocketPixel(x + width - three, rivetY, transform, C_METAL_DARK);
  }

  drawFuelWindow(x, y, width, height, fuelFraction, active, scale, transform,
                 now);

  if (active) {
    drawEngineAssembly(geometry, scale, transform);
  }
}

static void drawEngineFlame(const StageGeometry &activeGeometry,
                            float fuelFraction, float scale,
                            const RocketTransform &transform, uint32_t now) {
  EngineGeometry engine = engineGeometryFor(activeGeometry, scale);
  int16_t centreX = engine.centreX;
  int16_t nozzleY = engine.nozzleY;

  float flicker = sinf(now * 0.045f) * 0.045f + sinf(now * 0.081f) * 0.025f;
  float burnoutTaper = smoothStep(0.0f, 0.13f, fuelFraction);
  float power = clampFloat((1.0f + flicker) * burnoutTaper, 0.0f, 1.08f);

  if (power <= 0.04f) {
    return;
  }

  float spaceAmount =
      smoothStep(45.0f, 165.0f, displayedAltitudeAt(game.altitude));
  int16_t outerLength = maxInt16(
      scaledSize(11.0f, scale, 7),
      (int16_t)roundf((11.0f * scale + activeGeometry.width * 0.82f) * power));
  int16_t baseHalfWidth =
      maxInt16(1, (int16_t)roundf(engine.exitWidth * 0.22f));
  int16_t shoulderHalfWidth =
      maxInt16(2, (int16_t)roundf(activeGeometry.width *
                                  lerpFloat(0.22f, 0.30f, spaceAmount)));
  float tipDrift = (sinf(now * 0.031f) + sinf(now * 0.017f)) * 0.55f * scale;

  // A pressure plume starts narrow at the bell, expands smoothly, then breaks
  // into a soft turbulent tail. Vacuum expansion widens it without changing
  // the attachment point or making the whole flame pulse.
  for (int16_t row = 0; row <= outerLength; row++) {
    float amount = (float)row / maxFloat(1.0f, (float)outerLength);
    float expansion = smoothStep(0.0f, 0.58f, amount);
    float tail = 1.0f - 0.88f * smoothStep(0.72f, 1.0f, amount);
    int16_t halfWidth = maxInt16(
        1, (int16_t)roundf((baseHalfWidth +
                            (shoulderHalfWidth - baseHalfWidth) * expansion) *
                           tail));
    int16_t centreDrift = (int16_t)roundf(
        tipDrift * amount + sinf(now * 0.023f + row * 0.61f) * 0.35f * amount);
    int16_t edgeVariation =
        amount > 0.55f && ((row + (int16_t)(now / 73U)) % 7 == 0) ? 1 : 0;
    drawPitchedHLine(centreX + centreDrift - halfWidth, nozzleY + row,
                     halfWidth * 2 + 1 + edgeVariation, transform, C_RED);
  }

  int16_t middleLength = maxInt16(4, (int16_t)roundf(outerLength * 0.80f));
  int16_t middleShoulder =
      maxInt16(1, (int16_t)roundf(shoulderHalfWidth * 0.62f));
  for (int16_t row = 0; row <= middleLength; row++) {
    float amount = (float)row / maxFloat(1.0f, (float)middleLength);
    float expansion = smoothStep(0.0f, 0.52f, amount);
    float tail = 1.0f - 0.92f * smoothStep(0.68f, 1.0f, amount);
    int16_t halfWidth = maxInt16(
        1, (int16_t)roundf(
               (baseHalfWidth + (middleShoulder - baseHalfWidth) * expansion) *
               tail));
    int16_t centreDrift = (int16_t)roundf(tipDrift * amount * 0.45f);
    drawPitchedHLine(centreX + centreDrift - halfWidth, nozzleY + row,
                     halfWidth * 2 + 1, transform, C_ORANGE);
  }

  int16_t coreLength = maxInt16(4, (int16_t)roundf(outerLength * 0.42f));
  int16_t coreShoulder = maxInt16(1, scaledSize(3.0f, scale) / 2);
  for (int16_t row = 0; row < coreLength; row++) {
    float amount = (float)row / maxFloat(1.0f, (float)(coreLength - 1));
    float diamond = fabsf(sinf(amount * 2.0f * 3.1415926f));
    int16_t halfWidth =
        maxInt16(0, (int16_t)roundf((1.0f - amount) * coreShoulder *
                                    (0.55f + diamond * 0.45f)));
    drawPitchedHLine(centreX - halfWidth, nozzleY + row, halfWidth * 2 + 1,
                     transform, amount < 0.62f ? C_WHITE : C_YELLOW);
  }
}

static void updateRocketBounds(RocketGeometry &geometry, bool engineOn) {
  float left = geometry.payloadX;
  float right = geometry.payloadX + geometry.payloadWidth;
  float maximumStageWidth = 0.0f;

  for (uint8_t index = 0; index < geometry.stageCount; index++) {
    const StageGeometry &stage = geometry.stages[index];
    left = minFloat(left, stage.x);
    right = maxFloat(right, stage.x + stage.width);
    maximumStageWidth = maxFloat(maximumStageWidth, stage.width);
  }

  float bottomMargin = 7.0f * geometry.visualScale;
  if (engineOn && geometry.stageCount > 0) {
    // Covers the longest permitted flame envelope, including pitch offset.
    bottomMargin += maximumStageWidth * 0.70f + 22.0f * geometry.visualScale;
  }

  float totalHeight = geometry.bottomY - geometry.noseY + bottomMargin;
  float pitchReach = fabsf(tanf(geometry.pitchRadians)) * totalHeight;
  float sideMargin = 3.0f * geometry.visualScale + pitchReach;

  geometry.setBoundingBox(left - sideMargin,
                          geometry.noseY - 3.0f * geometry.visualScale,
                          right + sideMargin, geometry.bottomY + bottomMargin);
}

static void drawRocketComponents(const MissionDefinition &mission,
                                 const RocketGeometry &geometry,
                                 uint8_t activeStage, float activeFuel,
                                 bool engineOn, uint32_t now,
                                 const RocketTransform &transform) {
  // Exhaust is behind the bell. Its first row still shares the exact nozzle
  // coordinate, while the nozzle lip remains visible over it.
  if (engineOn && geometry.stageCount > 0) {
    drawEngineFlame(geometry.stages[geometry.stageCount - 1], activeFuel,
                    geometry.visualScale, transform, now);
  }

  drawPayload(geometry, mission.recoverCapsule, transform);

  for (uint8_t index = 0; index < geometry.stageCount; index++) {
    const StageGeometry &stageGeometry = geometry.stages[index];
    bool active = stageGeometry.sourceIndex == activeStage;
    float fuel = active ? activeFuel : 1.0f;

    drawStageBody(mission.stages[stageGeometry.sourceIndex], stageGeometry,
                  fuel, active, geometry.visualScale, transform, now);
  }
}

static void drawRotatedRocketLayer(float pivotX, float pivotY, float angle) {
  uint16_t *pixels = rocketLayer.getBuffer();
  if (pixels == nullptr) {
    return;
  }

  int16_t sourceLeft = ROCKET_LAYER_W;
  int16_t sourceTop = ROCKET_LAYER_H;
  int16_t sourceRight = -1;
  int16_t sourceBottom = -1;

  for (int16_t y = 0; y < ROCKET_LAYER_H; y++) {
    for (int16_t x = 0; x < ROCKET_LAYER_W; x++) {
      if (pixels[y * ROCKET_LAYER_W + x] == 0) {
        continue;
      }

      sourceLeft = minInt16(sourceLeft, x);
      sourceTop = minInt16(sourceTop, y);
      sourceRight = maxInt16(sourceRight, x);
      sourceBottom = maxInt16(sourceBottom, y);
    }
  }

  if (sourceRight < sourceLeft || sourceBottom < sourceTop) {
    return;
  }

  PointF corners[4] = {
      rotatePoint(sourceLeft - ROCKET_LAYER_PIVOT_X,
                  sourceTop - ROCKET_LAYER_PIVOT_Y, pivotX, pivotY, angle),
      rotatePoint(sourceRight - ROCKET_LAYER_PIVOT_X,
                  sourceTop - ROCKET_LAYER_PIVOT_Y, pivotX, pivotY, angle),
      rotatePoint(sourceRight - ROCKET_LAYER_PIVOT_X,
                  sourceBottom - ROCKET_LAYER_PIVOT_Y, pivotX, pivotY, angle),
      rotatePoint(sourceLeft - ROCKET_LAYER_PIVOT_X,
                  sourceBottom - ROCKET_LAYER_PIVOT_Y, pivotX, pivotY, angle)};

  float minimumX = corners[0].x;
  float maximumX = corners[0].x;
  float minimumY = corners[0].y;
  float maximumY = corners[0].y;
  for (uint8_t index = 1; index < 4; index++) {
    minimumX = minFloat(minimumX, corners[index].x);
    maximumX = maxFloat(maximumX, corners[index].x);
    minimumY = minFloat(minimumY, corners[index].y);
    maximumY = maxFloat(maximumY, corners[index].y);
  }

  int16_t screenLeft = maxInt16(0, (int16_t)floorf(minimumX) - 1);
  int16_t screenRight = minInt16(SCREEN_W - 1, (int16_t)ceilf(maximumX) + 1);
  int16_t screenTop = maxInt16(0, (int16_t)floorf(minimumY) - 1);
  int16_t screenBottom = minInt16(SCREEN_H - 1, (int16_t)ceilf(maximumY) + 1);
  float cosine = cosf(angle);
  float sine = sinf(angle);
  uint16_t *framePixels = frame.getBuffer();

  // Inverse mapping visits every destination pixel, avoiding the pinholes
  // produced when source pixels are individually pushed through a rotation.
  for (int16_t screenY = screenTop; screenY <= screenBottom; screenY++) {
    float deltaX = screenLeft - pivotX;
    float deltaY = screenY - pivotY;
    float sourceX = ROCKET_LAYER_PIVOT_X + deltaX * cosine + deltaY * sine;
    float sourceY = ROCKET_LAYER_PIVOT_Y - deltaX * sine + deltaY * cosine;

    for (int16_t screenX = screenLeft; screenX <= screenRight; screenX++) {
      int16_t sampledX = (int16_t)roundf(sourceX);
      int16_t sampledY = (int16_t)roundf(sourceY);

      if (sampledX >= sourceLeft && sampledX <= sourceRight &&
          sampledY >= sourceTop && sampledY <= sourceBottom) {
        uint16_t colour = pixels[sampledY * ROCKET_LAYER_W + sampledX];
        if (colour != 0) {
          framePixels[screenY * SCREEN_W + screenX] = colour;
        }
      }

      sourceX += cosine;
      sourceY -= sine;
    }
  }
}

static void drawRocket(const MissionDefinition &mission,
                       RocketGeometry &geometry, uint8_t activeStage,
                       float activeFuel, bool engineOn, uint32_t now) {
  updateRocketBounds(geometry, engineOn);
  if (!geometry.isVisibleOnScreen()) {
    return;
  }

  float pivotX = geometry.payloadX + geometry.payloadWidth * 0.5f;
  float pivotY = (geometry.noseY + geometry.bottomY) * 0.5f;
  RocketTransform transform = {pivotX, pivotY, geometry.pitchRadians};

  if (fabsf(geometry.pitchRadians) < 0.001f ||
      rocketLayer.getBuffer() == nullptr) {
    rocketRenderTarget = &frame;
    drawRocketComponents(mission, geometry, activeStage, activeFuel, engineOn,
                         now, transform);
    return;
  }

  RocketGeometry localGeometry = geometry;
  float shiftX = ROCKET_LAYER_PIVOT_X - pivotX;
  float shiftY = ROCKET_LAYER_PIVOT_Y - pivotY;
  localGeometry.noseY += shiftY;
  localGeometry.bottomY += shiftY;
  localGeometry.payloadX += shiftX;
  localGeometry.payloadY += shiftY;
  for (uint8_t index = 0; index < localGeometry.stageCount; index++) {
    localGeometry.stages[index].x += shiftX;
    localGeometry.stages[index].y += shiftY;
  }

  rocketLayer.fillScreen(0);
  rocketRenderTarget = &rocketLayer;
  RocketTransform localTransform = {(float)ROCKET_LAYER_PIVOT_X,
                                    (float)ROCKET_LAYER_PIVOT_Y, 0.0f};
  drawRocketComponents(mission, localGeometry, activeStage, activeFuel,
                       engineOn, now, localTransform);
  rocketRenderTarget = &frame;
  drawRotatedRocketLayer(pivotX, pivotY, geometry.pitchRadians);
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
                                     float fuelFraction, float visualScale,
                                     const RocketTransform &transform) {
  int16_t bandPosition =
      maxInt16((int16_t)5, (int16_t)(geometry.height / 5.0f));
  float side = randomGenerator.chance(0.5f) ? -1.0f : 1.0f;
  PointF centre = transform.apply(geometry.x + geometry.width * 0.5f,
                                  geometry.y + geometry.height * 0.5f);

  Fragment *stage = createFragment(
      FragmentType::WHOLE_STAGE, centre.x, centre.y, geometry.width,
      geometry.height, side * randomGenerator.range(6.0f, 11.0f), 0.0f,
      transform.angle, side * randomGenerator.range(0.28f, 0.52f), 5.2f,
      definition.bandColour, fuelFraction, (int8_t)bandPosition);

  float physicalScale = maxFloat(0.65f, visualScale);
  float physicalWidth = geometry.width / physicalScale;
  float physicalHeight = geometry.height / physicalScale;
  stage->mass =
      maxFloat(1.0f, physicalWidth * physicalWidth * physicalHeight * 0.0024f);
  stage->minimumDragArea = physicalWidth * physicalWidth;
  stage->maximumDragArea = physicalWidth * physicalHeight;

  // A fixed separator impulse produces delta-v = impulse / mass, so lighter
  // upper stages move away faster while every stage retains the vehicle's
  // post-result upward velocity.
  float separationDeltaV = clampFloat(450.0f / stage->mass, 1.8f, 4.8f);
  stage->cameraTracked = true;
  stage->worldVelocity = game.velocity - separationDeltaV;
  stage->worldAltitude = game.altitude;
  stage->cameraScale = physicalScale;
}

static void beginExplosion(const char *reason) {
  if (game.phase == FlightPhase::EXPLOSION) {
    return;
  }

  RocketGeometry geometry;
  buildCurrentRocketGeometry(geometry);

  float pivotX = geometry.payloadX + geometry.payloadWidth * 0.5f;
  float pivotY = (geometry.noseY + geometry.bottomY) * 0.5f;
  RocketTransform transform = {pivotX, pivotY, geometry.pitchRadians};
  float localExplosionY =
      geometry.noseY + (geometry.bottomY - geometry.noseY) * 0.62f;
  PointF explosion = transform.apply(pivotX, localExplosionY);
  game.explosionX = explosion.x;
  game.explosionY = explosion.y;

  strncpy(game.failureReason, reason, sizeof(game.failureReason) - 1);

  game.failureReason[sizeof(game.failureReason) - 1] = '\0';

  game.phase = FlightPhase::EXPLOSION;

  game.phaseStartedAt = millis();

  game.success = false;
  game.flash = 1.0f;
  game.shake = 10.0f;

  PointF capsuleCentre =
      transform.apply(geometry.payloadX + geometry.payloadWidth * 0.5f,
                      geometry.payloadY + geometry.payloadHeight * 0.5f);
  createFragment(FragmentType::CAPSULE, capsuleCentre.x, capsuleCentre.y,
                 geometry.payloadWidth, geometry.payloadHeight,
                 randomGenerator.range(-48.0f, 48.0f),
                 randomGenerator.range(-92.0f, -34.0f),
                 geometry.pitchRadians + randomGenerator.range(-0.25f, 0.25f),
                 randomGenerator.range(-4.4f, 4.4f), 3.7f,
                 currentMission().recoverCapsule ? C_CYAN : C_PURPLE, 0.0f, -1);

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
      PointF sliceCentre = transform.apply(
          stageGeometry.x + stageGeometry.width * 0.5f, sliceCentreY);

      int8_t localBand = -1;

      if (originalBandY >= sliceTop && originalBandY < sliceTop + sliceHeight) {
        localBand = (int8_t)(originalBandY - sliceTop);
      }

      float deltaX = sliceCentre.x - game.explosionX;

      float deltaY = sliceCentre.y - game.explosionY;

      float distance = sqrtf(deltaX * deltaX + deltaY * deltaY);

      if (distance < 1.0f) {
        distance = 1.0f;
      }

      float radialX = deltaX / distance;

      float radialY = deltaY / distance;

      float force = randomGenerator.range(38.0f, 86.0f);

      createFragment(
          FragmentType::STAGE_SLICE, sliceCentre.x, sliceCentre.y,
          stageGeometry.width, sliceHeight - 1.0f,
          radialX * force + randomGenerator.range(-32.0f, 32.0f),
          radialY * force + randomGenerator.range(-42.0f, 22.0f),
          geometry.pitchRadians + randomGenerator.range(-0.18f, 0.18f),
          randomGenerator.range(-5.2f, 5.2f), randomGenerator.range(3.0f, 4.2f),
          definition.bandColour, stageFuel, localBand);
    }

    for (uint8_t side = 0; side < 2; side++) {
      float sideDirection = side == 0 ? -1.0f : 1.0f;
      float panelY = stageGeometry.y +
                     stageGeometry.height * randomGenerator.range(0.25f, 0.75f);
      PointF panelCentre = transform.apply(
          stageGeometry.x + stageGeometry.width * (side == 0 ? 0.12f : 0.88f),
          panelY);

      createFragment(FragmentType::SIDE_PANEL, panelCentre.x, panelCentre.y,
                     4.0f,
                     stageGeometry.height * randomGenerator.range(0.28f, 0.52f),
                     sideDirection * randomGenerator.range(55.0f, 105.0f),
                     randomGenerator.range(-72.0f, 35.0f),
                     geometry.pitchRadians, randomGenerator.range(-7.0f, 7.0f),
                     2.8f, definition.bandColour, 0.0f, -1);
    }

    EngineGeometry engine =
        engineGeometryFor(stageGeometry, geometry.visualScale);
    PointF engineCentre =
        transform.apply(engine.centreX, (engine.topY + engine.nozzleY) * 0.5f);
    createFragment(FragmentType::ENGINE, engineCentre.x, engineCentre.y,
                   engine.exitWidth, engine.nozzleY - engine.topY,
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

  float burnSeconds =
      flightTiming.stageBurnSeconds(currentMission(), game.activeStage);
  float progress = game.stageElapsed / maxFloat(0.01f, burnSeconds);
  float lateSeconds = maxFloat(0.0f, game.stageElapsed - burnSeconds);
  float lateGrace =
      flightTiming.lateGraceSeconds(currentMission(), game.activeStage);

  if (lateSeconds > lateGrace) {
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

  float pivotX = geometry.payloadX + geometry.payloadWidth * 0.5f;
  float pivotY = (geometry.noseY + geometry.bottomY) * 0.5f;
  RocketTransform transform = {pivotX, pivotY, geometry.pitchRadians};
  PointF seam = transform.apply(activeGeometry.x + activeGeometry.width * 0.5f,
                                activeGeometry.y);

  spawnSparkBurst(seam.x, seam.y, 15, C_GREEN);
  spawnSmokeBurst(seam.x, seam.y + 2.0f, 7, 17.0f);

  char stageFeedback[20];
  uint8_t separatedStage = game.activeStage + 1;

  if (progress < 0.65f) {
    snprintf(stageFeedback, sizeof(stageFeedback), "S%u TOO EARLY",
             separatedStage);
    setFeedback(stageFeedback, C_RED, 1.05f);
    game.velocity *= 0.52f;
    game.score = game.score > 120 ? game.score - 120 : 0;
    game.shake = 4.5f;
  } else if (progress < 0.88f) {
    snprintf(stageFeedback, sizeof(stageFeedback), "S%u EARLY", separatedStage);
    setFeedback(stageFeedback, C_ORANGE, 1.0f);
    game.velocity *= 0.72f;
    game.score += 90;
    game.shake = 4.5f;
  } else if (progress < 0.95f) {
    snprintf(stageFeedback, sizeof(stageFeedback), "S%u GOOD", separatedStage);
    setFeedback(stageFeedback, C_CYAN, 1.0f);
    game.velocity *= 0.90f;
    game.score += 300;
    game.shake = 5.0f;
  } else if (lateSeconds <= 0.18f) {
    snprintf(stageFeedback, sizeof(stageFeedback), "S%u PERFECT",
             separatedStage);
    setFeedback(stageFeedback, C_GREEN, 1.08f);
    game.velocity *= PERFECT_STAGE_VELOCITY_RETAINED;
    game.score += 600;
    game.shake = 5.5f;
  } else {
    snprintf(stageFeedback, sizeof(stageFeedback), "S%u LATE", separatedStage);
    setFeedback(stageFeedback, C_YELLOW, 1.0f);
    game.velocity *= 0.88f;
    game.score += 170;
    game.shake = 5.0f;
  }

  // Create the detached body after applying the staging result so both bodies
  // inherit the same post-separation trajectory before the mass-based impulse.
  createWholeDetachedStage(*stage, activeGeometry, remainingFuel,
                           geometry.visualScale, transform);

  game.activeStage++;
  game.stageElapsed = 0.0f;
  game.stageBurnoutCueShown = false;

  // Keep the surviving hardware continuous, then smoothly move its new visual
  // centre back to the pre-separation tracking position.
  float remainingHeight =
      remainingRocketHeight(currentMission(), game.activeStage);
  float previousHeight =
      remainingRocketHeight(currentMission(), game.activeStage - 1);
  game.centeringTarget += (previousHeight - remainingHeight) * 0.5f;

  if (game.activeStage >= currentMission().stageCount()) {
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

      EngineGeometry engine =
          engineGeometryFor(newActive, nextGeometry.visualScale);
      float nextPivotX =
          nextGeometry.payloadX + nextGeometry.payloadWidth * 0.5f;
      float nextPivotY = (nextGeometry.noseY + nextGeometry.bottomY) * 0.5f;
      RocketTransform nextTransform = {nextPivotX, nextPivotY,
                                       nextGeometry.pitchRadians};
      PointF nozzle = nextTransform.apply(engine.centreX, engine.nozzleY);

      spawnFireBurst(nozzle.x, nozzle.y, 11, 30.0f);
      spawnSmokeBurst(nozzle.x, nozzle.y + 4.0f, 5, 17.0f);
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
  game.stageBurnoutCueShown = false;

  game.altitude = 0.0f;
  game.maximumAltitude = 0.0f;
  game.velocity = 0.0f;

  game.cameraAltitude = 0.0f;
  game.previousCameraAltitude = 0.0f;

  game.centeringOffset = 0.0f;
  game.centeringTarget = 0.0f;

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
  game.transitionStartAltitude = 0.0f;
  game.reentryHeat = 0.0f;

  game.explosionX = 86.0f;
  game.explosionY = 150.0f;
}

static void startMission() {
  resetFlightObjects();

  const MissionDefinition &mission = currentMission();

  float completeHeight = remainingRocketHeight(mission, 0);

  game.launchNoseY = 270.0f - completeHeight;

  // The camera begins tracking after a short, fast pad-clearance movement.
  // Track the complete stack by its visual centre, not by a fixed nose
  // displacement. This gives two-, three- and four-stage rockets the same
  // upper-middle composition while preserving their launch-pad position.
  game.trackingNoseY = ROCKET_BODY_TRACKING_CENTRE_Y - completeHeight * 0.5f;

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
  return lerpFloat(SURFACE_GRAVITY, 1.45f,
                   smoothStep(0.0f, 1500.0f, altitude));
}

static float atmosphereDensityAtAltitude(float altitude) {
  // Displayed kilometres are the mission-independent atmospheric scale.
  return expf(-displayedAltitudeAt(maxFloat(0.0f, altitude)) / 18.0f);
}

static void spawnContinuousEngineEffects(const RocketGeometry &geometry) {
  if (geometry.stageCount == 0) {
    return;
  }

  const StageGeometry &activeGeometry =
      geometry.stages[geometry.stageCount - 1];
  float scale = geometry.visualScale;
  EngineGeometry engine = engineGeometryFor(activeGeometry, scale);
  float nozzleX = engine.centreX;
  float nozzleY = engine.nozzleY;
  float pivotX = geometry.payloadX + geometry.payloadWidth * 0.5f;
  float pivotY = (geometry.noseY + geometry.bottomY) * 0.5f;
  RocketTransform transform = {pivotX, pivotY, geometry.pitchRadians};
  PointF nozzle = transform.apply(nozzleX, nozzleY);
  nozzleX = nozzle.x;
  nozzleY = nozzle.y;
  float spaceAmount =
      smoothStep(35.0f, 150.0f, displayedAltitudeAt(game.altitude));
  float inheritedScreenVelocity =
      -maxFloat(0.0f, game.velocity) * WORLD_PIXELS_PER_ALTITUDE;
  float exhaustAxisX = -tanf(geometry.pitchRadians);

  if (spaceAmount < 0.72f &&
      randomGenerator.chance(0.72f - spaceAmount * 0.75f)) {
    float exhaustSpeed = randomGenerator.range(19.0f, 30.0f);
    spawnParticle(
        ParticleType::FIRE,
        nozzleX + randomGenerator.range(-0.16f, 0.16f) * engine.exitWidth,
        nozzleY + randomGenerator.range(1.0f, 3.0f) * scale,
        exhaustAxisX * exhaustSpeed +
            randomGenerator.range(-5.0f, 5.0f) * (1.0f - spaceAmount * 0.35f),
        inheritedScreenVelocity + exhaustSpeed,
        randomGenerator.range(0.09f, 0.17f),
        randomGenerator.range(0.8f, 1.4f) * scale,
        randomGenerator.chance(0.55f) ? C_YELLOW : C_ORANGE);
  }

  if (spaceAmount < 0.70f &&
      randomGenerator.chance((1.0f - spaceAmount) * 0.58f)) {
    float exhaustSpeed = randomGenerator.range(13.0f, 23.0f);
    spawnParticle(
        ParticleType::SMOKE,
        nozzleX + randomGenerator.range(-0.28f, 0.28f) * engine.exitWidth,
        nozzleY + scaledSize(4.0f, scale),
        exhaustAxisX * exhaustSpeed + randomGenerator.range(-7.0f, 7.0f),
        inheritedScreenVelocity + exhaustSpeed,
        randomGenerator.range(0.28f, 0.58f),
        randomGenerator.range(1.4f, 2.6f) * scale, C_SMOKE);
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
  setFeedback("TARGET REACHED", C_GREEN, 1.25f);

  if (currentMission().recoverCapsule) {
    game.transitionStartAltitude = game.altitude;
    setPhase(FlightPhase::APOGEE);
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

static void updateCentering(float deltaSeconds) {
  if (game.centeringOffset >= game.centeringTarget) {
    return;
  }

  float centeringSpeed = 12.0f;
  float followAmount = 1.0f - expf(-centeringSpeed * deltaSeconds);
  float newOffset =
      lerpFloat(game.centeringOffset, game.centeringTarget, followAmount);

  float delta = newOffset - game.centeringOffset;
  game.centeringOffset = newOffset;

  game.launchNoseY += delta;
  game.trackingNoseY += delta;
  if (game.trackingNoseY > 170.0f) {
    game.trackingNoseY = 170.0f;
  }

  // Recentring is a camera/composition move. World effects must receive the
  // same shift or the survivor moving down makes the old stage appear to jump
  // upward at the instant of separation.
  for (uint8_t index = 0; index < MAX_PARTICLES; index++) {
    if (particles[index].active) {
      particles[index].y += delta;
    }
  }

  for (uint8_t index = 0; index < MAX_FRAGMENTS; index++) {
    if (fragments[index].active && fragments[index].cameraTracked) {
      fragments[index].y += delta;
    }
  }
}

static void updateFlight(float deltaSeconds, uint32_t now) {
  float phaseSeconds = (now - game.phaseStartedAt) / 1000.0f;
  bool emitEngineEffects = false;

  if (game.phase == FlightPhase::COUNTDOWN) {
    if (phaseSeconds >= 3.0f) {
      setPhase(FlightPhase::BURNING);
      game.velocity = LIFTOFF_VELOCITY;
      setFeedback("LIFTOFF", C_YELLOW, 1.0f);
      game.shake = 7.0f;
      spawnSmokeBurst(86.0f, 267.0f, 34, 43.0f);
      spawnFireBurst(86.0f, 258.0f, 18, 34.0f);
    }
  } else if (game.phase == FlightPhase::BURNING) {
    const StageDefinition *stage = currentStage();

    if (stage == nullptr) {
      setPhase(FlightPhase::COASTING);
    } else {
      float previousElapsed = game.stageElapsed;
      float nextElapsed = previousElapsed + deltaSeconds;
      float burnSeconds =
          flightTiming.stageBurnSeconds(currentMission(), game.activeStage);
      float poweredSeconds =
          clampFloat(burnSeconds - previousElapsed, 0.0f, deltaSeconds);
      float unpoweredSeconds = deltaSeconds - poweredSeconds;

      if (poweredSeconds > 0.0f) {
        float gravity = gravityAtAltitude(game.altitude);
        game.velocity += (stage->thrust - gravity) * poweredSeconds;
        game.altitude += maxFloat(0.0f, game.velocity) * poweredSeconds;
        emitEngineEffects = true;
      }

      if (unpoweredSeconds > 0.0f) {
        float gravity = gravityAtAltitude(game.altitude);
        game.velocity -= gravity * unpoweredSeconds;
        game.altitude =
            maxFloat(0.0f, game.altitude + game.velocity * unpoweredSeconds);
      }

      game.stageElapsed = nextElapsed;
      game.score +=
          (int32_t)(maxFloat(0.0f, game.velocity) * deltaSeconds * 57.0f);

      if (!game.stageBurnoutCueShown && previousElapsed < burnSeconds &&
          nextElapsed >= burnSeconds) {
        game.stageBurnoutCueShown = true;
        setFeedback("SEPARATE NOW", C_YELLOW, 0.85f);
        game.shake = maxFloat(game.shake, 1.2f);
      }

      if (game.stageElapsed >
          burnSeconds + flightTiming.lateGraceSeconds(currentMission(),
                                                      game.activeStage)) {
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
  } else if (game.phase == FlightPhase::APOGEE) {
    // Compress the apogee without freezing the scene. The remaining upward
    // speed eases to zero while stars and the flight path continue moving.
    float settleAmount = 1.0f - expf(-3.8f * deltaSeconds);
    game.velocity = lerpFloat(game.velocity, 0.0f, settleAmount);
    game.altitude += maxFloat(0.0f, game.velocity) * deltaSeconds * 0.16f;

    if (phaseSeconds >= 1.15f) {
      game.transitionStartAltitude = game.altitude;
      game.velocity = 0.0f;
      game.reentryHeat = 0.0f;
      setFeedback("REENTRY", C_ORANGE, 1.1f);
      setPhase(FlightPhase::REENTRY);
    }
  } else if (game.phase == FlightPhase::REENTRY) {
    float recoveryDisplayAltitude =
        minFloat(28.0f, missionDisplayTargetAltitude() * 0.36f);
    float recoveryInternalAltitude = currentMission().targetAltitude *
                                     recoveryDisplayAltitude /
                                     missionDisplayTargetAltitude();
    float descentDistance =
        maxFloat(1.0f, game.transitionStartAltitude - recoveryInternalAltitude);
    float progress = clamp01((game.transitionStartAltitude - game.altitude) /
                             descentDistance);
    float descentRate =
        lerpFloat(0.18f, 0.32f, smoothStep(0.08f, 0.88f, progress));
    float desiredVelocity = -currentMission().targetAltitude * descentRate;
    float velocityFollow = 1.0f - expf(-3.2f * deltaSeconds);

    game.velocity = lerpFloat(game.velocity, desiredVelocity, velocityFollow);
    game.altitude = maxFloat(recoveryInternalAltitude,
                             game.altitude + game.velocity * deltaSeconds);
    game.cameraAltitude = game.altitude;
    game.previousCameraAltitude = game.cameraAltitude;
    game.reentryHeat = sinf(smoothStep(0.04f, 0.96f, progress) * 3.1415926f);

    if (game.altitude <= recoveryInternalAltitude + 0.01f) {
      RocketGeometry capsuleGeometry;
      buildCurrentRocketGeometry(capsuleGeometry);
      game.recoveryY = capsuleGeometry.payloadY - 7.0f;
      game.transitionStartAltitude = game.altitude;
      // This deceleration begins with the now-immediate pilot-chute visual;
      // reentry itself no longer eases down before deployment is visible.
      game.velocity =
          maxFloat(game.velocity, -game.transitionStartAltitude * 0.24f);
      game.parachuteOpen = 0.0f;
      game.reentryHeat = 0.0f;
      setPhase(FlightPhase::RECOVERY);
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
    game.parachuteOpen = smoothStep(0.25f, 1.95f, phaseSeconds);
    float descentSpeed = lerpFloat(34.0f, 15.0f, game.parachuteOpen);
    game.recoveryY += descentSpeed * deltaSeconds;

    // The parachute arrests the world-space descent as the ground and daytime
    // sky continue to approach. Screen-space capsule motion remains gentle.
    float desiredVelocity = -game.transitionStartAltitude *
                            lerpFloat(0.24f, 0.09f, game.parachuteOpen);
    float velocityFollow =
        1.0f - expf(-(2.0f + game.parachuteOpen * 3.0f) * deltaSeconds);
    game.velocity = lerpFloat(game.velocity, desiredVelocity, velocityFollow);
    game.altitude =
        maxFloat(0.0f, game.altitude + game.velocity * deltaSeconds);
    game.cameraAltitude = game.altitude;
    game.previousCameraAltitude = game.cameraAltitude;

    if (phaseSeconds >= 5.4f) {
      setScreen(ScreenMode::RESULT);
    }
  }

  updateCentering(deltaSeconds);

  if (game.phase != FlightPhase::REENTRY &&
      game.phase != FlightPhase::RECOVERY) {
    updateWorldCamera(deltaSeconds);
  }

  if (emitEngineEffects && game.phase == FlightPhase::BURNING) {
    RocketGeometry geometry;
    buildCurrentRocketGeometry(geometry);
    spawnContinuousEngineEffects(geometry);
  }

  game.maximumAltitude = maxFloat(game.maximumAltitude, game.altitude);
  game.feedbackRemaining =
      maxFloat(0.0f, game.feedbackRemaining - deltaSeconds);
  game.shake = maxFloat(0.0f, game.shake - deltaSeconds * 10.0f);
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
    if (button.events.held) {
      game.selectedMission = (game.selectedMission + 1) % game.unlockedMissions;

      game.screenStartedAt = millis();
    }

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
  // HUD coordinates are fixed screen space and never receive camera or rocket
  // transforms.
  char buffer[32];

  frame.fillRoundRect(5, 5, 68, 25, 5, C_PANEL);

  frame.drawRoundRect(5, 5, 68, 25, 5, C_BORDER);

  drawTextLeft(currentMission().code, 10, 9, 1, C_MUTED);

  snprintf(buffer, sizeof(buffer), "%d KM",
           (int)roundf(displayedAltitudeAt(game.altitude)));

  drawTextRight(buffer, 68, 19, 1, C_TEXT);

  frame.fillRoundRect(99, 5, 68, 25, 5, C_PANEL);

  frame.drawRoundRect(99, 5, 68, 25, 5, C_BORDER);

  drawTextLeft("SCORE", 104, 9, 1, C_MUTED);

  snprintf(buffer, sizeof(buffer), "%ld", (long)game.score);

  drawTextRight(buffer, 162, 19, 1, C_YELLOW);

  if (game.phase == FlightPhase::BURNING) {
    snprintf(buffer, sizeof(buffer), "STAGE %u/%u", game.activeStage + 1,
             currentMission().stageCount());

    drawTextCentered(buffer, 35, 1, C_MUTED);
  } else if (game.phase == FlightPhase::COASTING) {
    snprintf(buffer, sizeof(buffer), "COAST %d/%d",
             (int)roundf(displayedAltitudeAt(game.altitude)),
             (int)roundf(missionDisplayTargetAltitude()));
    drawTextCentered(buffer, 35, 1, C_CYAN);
  } else if (game.phase == FlightPhase::APOGEE) {
    drawTextCentered("APOGEE", 35, 1, C_GREEN);
  } else if (game.phase == FlightPhase::REENTRY) {
    drawTextCentered("REENTRY", 35, 1, C_ORANGE);
  }

  if (game.feedbackRemaining > 0.0f) {
    drawTextCentered(game.feedback, 48, 1, game.feedbackColour);
  } else if (game.phase == FlightPhase::COASTING) {
    snprintf(buffer, sizeof(buffer), "V %+d KM/S",
             (int)roundf(displayedVelocityAt(game.velocity)));
    drawTextCentered(buffer, 48, 1, C_MUTED);
  }
}

static void drawCountdown() {
  float seconds = (millis() - game.phaseStartedAt) / 1000.0f;

  int countdown = 3 - (int)seconds;

  char buffer[12];

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

static void drawRecoveryClouds(float elapsed, uint32_t now) {
  static const int16_t CLOUD_X[] = {18, 69, 132, 37, 108, 158};
  static const int16_t CLOUD_Y[] = {72, 142, 215, 286, 355, 420};

  for (uint8_t index = 0; index < 6; index++) {
    float speed = 17.0f + (index % 3) * 7.0f;
    float movingY = CLOUD_Y[index] - elapsed * speed;
    while (movingY < -28.0f) {
      movingY += 430.0f;
    }
    int16_t drift = (int16_t)(sinf(now * 0.00025f + index) * 3.0f);
    drawCloudShape(CLOUD_X[index] + drift, (int16_t)movingY,
                   9 + (index % 3) * 3,
                   index % 2 ? C_CLOUD : C565(191, 205, 211));
  }
}

static void drawRecoveryScreen(uint32_t now) {
  float elapsed = (now - game.phaseStartedAt) / 1000.0f;
  float visualAltitude = displayedAltitudeAt(game.altitude);
  drawBackground(visualAltitude, visualAltitude, now,
                 currentMission().flightType, false);
  drawRecoveryClouds(elapsed, now);

  int16_t centreX = SCREEN_W / 2;
  float capsuleY = game.recoveryY;
  float lineOpen = smoothStep(0.0f, 0.55f, elapsed);
  float canopyOpen = game.parachuteOpen;

  if (lineOpen > 0.02f) {
    int16_t lineTopY =
        (int16_t)roundf(capsuleY - lerpFloat(7.0f, 30.0f, lineOpen));
    int16_t lineHalfWidth = (int16_t)roundf(16.0f * canopyOpen + 3.0f);
    uint16_t lineColour =
        blend565(C_MUTED, C_WHITE, (uint8_t)(lineOpen * 190.0f));
    frame.drawLine(centreX - lineHalfWidth, lineTopY, centreX - 6,
                   (int16_t)capsuleY + 8, lineColour);
    frame.drawLine(centreX + lineHalfWidth, lineTopY, centreX + 6,
                   (int16_t)capsuleY + 8, lineColour);
    frame.drawLine(centreX, lineTopY, centreX, (int16_t)capsuleY + 8,
                   lineColour);

    if (canopyOpen < 0.12f) {
      frame.fillCircle(centreX, lineTopY - 2,
                       maxInt16(1, (int16_t)(lineOpen * 3.0f)), C_RED);
    }
  }

  int16_t canopyWidth = (int16_t)roundf(42.0f * canopyOpen);
  int16_t canopyHeight = maxInt16(2, (int16_t)roundf(14.0f * canopyOpen));
  if (canopyWidth > 4) {
    int16_t canopyY = (int16_t)roundf(capsuleY - 30.0f);
    int16_t halfWidth = canopyWidth / 2;
    for (int16_t row = 0; row <= canopyHeight; row++) {
      float normalY = (float)(row - canopyHeight) / canopyHeight;
      int16_t rowHalfWidth = (int16_t)roundf(
          halfWidth * sqrtf(maxFloat(0.0f, 1.0f - normalY * normalY)));
      frame.drawFastHLine(centreX - rowHalfWidth, canopyY - canopyHeight + row,
                          rowHalfWidth * 2 + 1, C_RED);
    }
    frame.drawFastHLine(centreX - canopyWidth / 2, canopyY, canopyWidth,
                        C565(255, 126, 94));
    frame.drawLine(centreX, canopyY - canopyHeight, centreX, canopyY + 1,
                   C565(255, 170, 135));
  }

  RocketGeometry capsuleGeometry = {};
  capsuleGeometry.payloadX = centreX - 9;
  capsuleGeometry.payloadY = capsuleY + 7;
  capsuleGeometry.payloadWidth = 18;
  capsuleGeometry.payloadHeight = 20;
  capsuleGeometry.noseY = capsuleGeometry.payloadY;
  capsuleGeometry.bottomY =
      capsuleGeometry.payloadY + capsuleGeometry.payloadHeight;
  RocketTransform capsuleTransform = {
      (float)centreX, (capsuleGeometry.noseY + capsuleGeometry.bottomY) * 0.5f,
      0.0f};
  drawPayload(capsuleGeometry, true, capsuleTransform);

  frame.fillRoundRect(48, 294, 76, 19, 4, C_PANEL);
  frame.drawRoundRect(48, 294, 76, 19, 4, C_BORDER);
  drawTextCentered("RECOVERY", 300, 1, C_GREEN);
}

static void drawDeploymentScreen(uint32_t now) {
  float visualAltitude = maxFloat(displayedAltitudeAt(game.cameraAltitude),
                                  missionDisplayTargetAltitude() * 0.82f);
  drawBackground(visualAltitude,
                 maxFloat(displayedAltitudeAt(game.altitude), visualAltitude),
                 now, currentMission().flightType, true);

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

static void drawReentryEffects(const RocketGeometry &geometry, uint32_t now) {
  if (game.phase != FlightPhase::REENTRY || game.reentryHeat <= 0.02f) {
    return;
  }

  float pivotX = geometry.payloadX + geometry.payloadWidth * 0.5f;
  float pivotY = (geometry.noseY + geometry.bottomY) * 0.5f;
  RocketTransform transform = {pivotX, pivotY, geometry.pitchRadians};
  PointF payloadCentre = transform.apply(
      pivotX, geometry.payloadY + geometry.payloadHeight * 0.5f);
  PointF payloadTopPoint = transform.apply(pivotX, geometry.payloadY);
  PointF payloadBottomPoint =
      transform.apply(pivotX, geometry.payloadY + geometry.payloadHeight);
  int16_t centreX = (int16_t)roundf(payloadCentre.x);
  int16_t payloadTop = (int16_t)roundf(payloadTopPoint.y);
  int16_t bottomY = (int16_t)roundf(payloadBottomPoint.y);
  int16_t width =
      maxInt16(4, (int16_t)roundf(geometry.payloadWidth *
                                  (0.55f + game.reentryHeat * 0.38f)));
  int16_t length =
      maxInt16(8, (int16_t)roundf(10.0f + game.reentryHeat * 20.0f));
  int16_t flicker = (int16_t)(sinf(now * 0.045f) * 3.0f);
  int16_t wakeTop = payloadTop - length;

  // The capsule is descending. The heat shield leads at the bottom while
  // several narrow plasma streamers trail upward around it.
  for (int8_t stream = -2; stream <= 2; stream++) {
    int16_t startX = centreX + stream * maxInt16(1, width / 3);
    int16_t endX = centreX + stream * maxInt16(2, width / 2) + flicker;
    int16_t endY = wakeTop + abs(stream) * 3;
    frame.drawLine(startX, bottomY - 2, endX, endY,
                   stream == 0 ? C_YELLOW : C_RED);

    if (stream >= -1 && stream <= 1) {
      frame.drawLine(startX + 1, bottomY - 3, endX + 1, endY + length / 4,
                     C_ORANGE);
    }
  }
  frame.drawFastHLine(centreX - width, bottomY - 1, width * 2 + 1, C_YELLOW);
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

  float cameraDisplayAltitude = displayedAltitudeAt(game.cameraAltitude);
  float earthDisplayAltitude = displayedAltitudeAt(game.altitude);
  drawBackground(cameraDisplayAltitude, earthDisplayAltitude, now,
                 currentMission().flightType, false);

  if (game.phase != FlightPhase::REENTRY && game.phase != FlightPhase::APOGEE) {
    drawLaunchPad(game.cameraAltitude, now,
                  game.phase == FlightPhase::COUNTDOWN);
  }

  drawParticles();
  drawFragments();

  if (game.phase != FlightPhase::EXPLOSION) {
    RocketGeometry geometry;
    buildCurrentRocketGeometry(geometry);
    drawReentryEffects(geometry, now);
    drawRocket(currentMission(), geometry, game.activeStage,
               currentFuelFraction(), game.phase == FlightPhase::BURNING, now);
  } else {
    drawShockwave();
  }

  drawHud();

  if (game.phase == FlightPhase::COUNTDOWN) {
    drawCountdown();
  }

  if (game.phase == FlightPhase::APOGEE) {
    drawTextCentered("TURNING FOR HOME", 66, 1, C_GREEN);
  }

  if (game.phase == FlightPhase::REENTRY) {
    drawTextCentered("ATMOSPHERIC ENTRY", 66, 1, C_ORANGE);
  }

  if (game.phase == FlightPhase::DESCENDING) {
    drawTextCentered("TRAJECTORY LOST", 66, 1, C_ORANGE);
    drawTextCentered("ROCKET FALLING", 81, 1, C_RED);
  }

  if (game.phase == FlightPhase::EXPLOSION) {
    drawTextCentered("MISSION FAILURE", 52, 1, C_RED);
    drawTextCentered(game.failureReason, 67, 1, C_TEXT);
  }
}

// =============================================================================
// Splash
// =============================================================================

static void drawSplashScreen(uint32_t now) {
  uint32_t elapsed = now - game.screenStartedAt;

  drawBackground(0.0f, 0.0f, now, MISSIONS[game.selectedMission].flightType,
                 false);

  drawLaunchPad(0.0f, now, elapsed > 650);

  const MissionDefinition &mission = MISSIONS[game.selectedMission];

  float completeHeight = remainingRocketHeight(mission, 0);

  float baseNoseY = 270.0f - completeHeight;

  RocketGeometry geometry;

  buildRocketGeometryFor(mission, 0, baseNoseY, geometry);

  drawRocket(mission, geometry, 0, 1.0f, false, now);

  fillTranslucentRoundRect(11, 28, 150, 85, 10, C_PANEL, 178);

  frame.drawRoundRect(11, 28, 150, 85, 10, C_BORDER);

  frame.drawFastVLine(20, 38, 64, C_OCEAN_LIGHT);

  drawTextLeft("TINY CONSOLE", 28, 39, 1, C_MUTED);

  drawTextLeft("VOID", 28, 55, 3, C_WHITE);

  drawTextLeft("ASCENT", 28, 83, 2, C_OCEAN_LIGHT);

  // if the elapsed time is even, draw the tap to continue text
  if (((elapsed / 400) & 1U) == 0) {
    drawTextCentered("TAP TO CONTINUE", 292, 1, C_WHITE);
  }
}

// =============================================================================
// Briefing
// =============================================================================

static void drawBriefingScreen(uint32_t now) {
  drawBackground(0.0f, 0.0f, now, currentMission().flightType, false);

  drawLaunchPad(0.0f, now, false);

  const MissionDefinition &mission = currentMission();

  float height = remainingRocketHeight(mission, 0);

  RocketGeometry previewGeometry;

  buildRocketGeometryFor(mission, 0, 267.0f - height, previewGeometry);

  drawRocket(mission, previewGeometry, 0, 1.0f, false, now);

  fillTranslucentRoundRect(8, 8, 156, 170, 9, C_PANEL, 188);

  frame.drawRoundRect(8, 8, 156, 170, 9, C_BORDER);

  char levelText[24];

  snprintf(levelText, sizeof(levelText), "LEVEL %u / %u",
           game.selectedMission + 1, MISSION_COUNT);

  drawTextLeft(levelText, 17, 18, 1, C_GREEN);

  drawTextRight(mission.code, 154, 18, 1, C_CYAN);

  drawTextCentered(mission.name, 37, 1, C_TEXT);

  char targetText[28];

  snprintf(targetText, sizeof(targetText), "TARGET %d KM",
           (int)roundf(missionDisplayTargetAltitude()));

  drawTextCentered(targetText, 55, 1, C_YELLOW);

  char stagesText[24];

  snprintf(stagesText, sizeof(stagesText), "%u STAGE STACK",
           mission.stageCount());

  drawTextCentered(stagesText, 70, 1, C_CYAN);

  drawTextLeft("OBJECTIVE", 18, 92, 1, C_MUTED);

  drawTextLeft(mission.objectiveLine1, 18, 109, 1, C_TEXT);

  drawTextLeft(mission.objectiveLine2, 18, 124, 1, C_TEXT);

  drawTextLeft("WATCH THE FUEL GLASS", 18, 146, 1, C_GREEN);

  drawTextLeft("TAP NEAR EMPTY", 18, 160, 1, C_YELLOW);

  fillTranslucentRoundRect(18, 278, 136, 34, 8, C_PANEL, 205);

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
  float resultAltitude = game.success && currentMission().recoverCapsule
                             ? displayedAltitudeAt(game.altitude)
                             : displayedAltitudeAt(game.maximumAltitude);
  drawBackground(resultAltitude, resultAltitude, now,
                 currentMission().flightType,
                 game.success && !currentMission().recoverCapsule);

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

  snprintf(buffer, sizeof(buffer), "%d KM",
           (int)roundf(displayedAltitudeAt(game.maximumAltitude)));

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

static void applyFrameShake(uint32_t now) {
  int16_t amplitude = (int16_t)ceilf(minFloat(7.0f, game.shake));
  if (amplitude <= 0) {
    return;
  }

  int16_t span = amplitude * 2 + 1;
  uint32_t phase = now / FRAME_INTERVAL_MS;
  int16_t offsetX = (int16_t)((phase * 17U + 3U) % span) - amplitude;
  int16_t offsetY = (int16_t)((phase * 29U + 7U) % span) - amplitude;
  uint16_t *buffer = frame.getBuffer();

  if (offsetY > 0) {
    memmove(buffer + offsetY * SCREEN_W, buffer,
            (size_t)(SCREEN_H - offsetY) * SCREEN_W * sizeof(uint16_t));
    frame.fillRect(0, 0, SCREEN_W, offsetY, C_BLACK);
  } else if (offsetY < 0) {
    int16_t rows = -offsetY;
    memmove(buffer, buffer + rows * SCREEN_W,
            (size_t)(SCREEN_H - rows) * SCREEN_W * sizeof(uint16_t));
    frame.fillRect(0, SCREEN_H - rows, SCREEN_W, rows, C_BLACK);
  }

  if (offsetX != 0) {
    for (int16_t y = 0; y < SCREEN_H; y++) {
      uint16_t *row = buffer + y * SCREEN_W;
      if (offsetX > 0) {
        memmove(row + offsetX, row, (SCREEN_W - offsetX) * sizeof(uint16_t));
        for (int16_t x = 0; x < offsetX; x++) {
          row[x] = C_BLACK;
        }
      } else {
        int16_t columns = -offsetX;
        memmove(row, row + columns, (SCREEN_W - columns) * sizeof(uint16_t));
        for (int16_t x = SCREEN_W - columns; x < SCREEN_W; x++) {
          row[x] = C_BLACK;
        }
      }
    }
  }
}

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

  applyFrameShake(now);

  gfx->draw16bitRGBBitmap(0, 0, frame.getBuffer(), SCREEN_W, SCREEN_H);
}

// =============================================================================
// RGB LED
// =============================================================================

class StatusLedController {
public:
  explicit StatusLedController(Adafruit_NeoPixel &pixel) : pixel(pixel) {}

  void begin() {
    pixel.begin();
    pixel.setBrightness(45);
    pixel.clear();
    pixel.show();
    lastColour = 0xFFFFFFFFU;
  }

  void showHardwareFault(bool lit) { set(lit ? 240 : 0, 0, 0); }

  void update(uint32_t now) {
    if (game.screen == ScreenMode::SPLASH) {
      uint8_t pulse = breathe(now, 1550, 28, 150);
      set(0, pulse / 2, pulse);
      return;
    }

    if (game.screen == ScreenMode::BRIEFING) {
      uint8_t pulse = breathe(now, 1800, 24, 120);
      set(pulse / 4, pulse / 2, pulse);
      return;
    }

    if (game.screen == ScreenMode::RESULT) {
      if (game.success) {
        uint8_t pulse = breathe(now, 1150, 35, 190);
        set(0, pulse, pulse / 4);
      } else {
        bool lit = doubleBlink(now, 980);
        set(lit ? 230 : 18, 0, 0);
      }
      return;
    }

    // Failure state has absolute priority over an older separation prompt or
    // result flash that may still have feedback time remaining.
    if (game.phase == FlightPhase::EXPLOSION) {
      bool lit = ((now / 65U) & 1U) == 0;
      set(lit ? 255 : 24, 0, 0);
      return;
    }

    if (game.feedbackRemaining > 0.0f && game.feedback[0] == 'S') {
      bool lit = ((now / 95U) & 1U) == 0;
      if (strstr(game.feedback, "PERFECT") != nullptr) {
        set(0, lit ? 235 : 38, lit ? 80 : 12);
      } else if (strstr(game.feedback, "GOOD") != nullptr) {
        set(0, lit ? 145 : 25, lit ? 225 : 42);
      } else if (strstr(game.feedback, "LATE") != nullptr) {
        set(lit ? 220 : 35, lit ? 150 : 22, 0);
      } else {
        set(lit ? 235 : 28, lit ? 45 : 3, 0);
      }
      return;
    }

    switch (game.phase) {
    case FlightPhase::COUNTDOWN: {
      uint32_t elapsed = now - game.phaseStartedAt;
      bool lit = ((elapsed / (elapsed > 2200 ? 90U : 220U)) & 1U) == 0;
      set(lit ? 220 : 12, lit ? 82 : 3, 0);
      break;
    }

    case FlightPhase::BURNING: {
      float fuel = currentFuelFraction();
      if (fuel < 0.08f) {
        bool lit = ((now / 75U) & 1U) == 0;
        set(lit ? 240 : 20, 0, 0);
      } else if (fuel < 0.20f) {
        bool lit = ((now / 155U) & 1U) == 0;
        set(lit ? 210 : 42, lit ? 92 : 12, 0);
      } else {
        uint8_t pulse = breathe(now, 720, 75, 155);
        set(0, pulse, pulse + 55);
      }
      break;
    }

    case FlightPhase::COASTING: {
      uint8_t pulse = breathe(now, 1350, 22, 145);
      set(0, pulse / 2, pulse);
      break;
    }

    case FlightPhase::APOGEE: {
      uint8_t pulse = breathe(now, 900, 45, 205);
      set(pulse / 2, pulse, pulse / 2);
      break;
    }

    case FlightPhase::REENTRY: {
      uint8_t flicker = 120 + (uint8_t)((now / 47U * 37U) % 110U);
      set(flicker, flicker / 5, 0);
      break;
    }

    case FlightPhase::RECOVERY: {
      uint8_t pulse = breathe(now, 1200, 42, 175);
      set(0, pulse, pulse / 2);
      break;
    }

    case FlightPhase::DEPLOYMENT: {
      uint8_t pulse = breathe(now, 780, 45, 185);
      set(pulse / 2, 0, pulse);
      break;
    }

    case FlightPhase::EXPLOSION: {
      set(255, 0, 0);
      break;
    }

    case FlightPhase::DESCENDING: {
      bool lit = doubleBlink(now, 820);
      set(lit ? 230 : 15, lit ? 12 : 0, 0);
      break;
    }
    }
  }

private:
  Adafruit_NeoPixel &pixel;
  uint32_t lastColour = 0xFFFFFFFFU;

  static uint8_t breathe(uint32_t now, uint16_t period, uint8_t minimum,
                         uint8_t maximum) {
    float phase = (now % period) / (float)period * 6.2831853f;
    float amount = sinf(phase) * 0.5f + 0.5f;
    return minimum + (uint8_t)((maximum - minimum) * amount);
  }

  static bool doubleBlink(uint32_t now, uint16_t period) {
    uint16_t phase = now % period;
    return phase < 95 || (phase >= 175 && phase < 285);
  }

  void set(uint8_t red, uint8_t green, uint8_t blue) {
    if (!onboardLedEnabled) {
      red = 0;
      green = 0;
      blue = 0;
    }

    uint32_t colour = pixel.Color(red, green, blue);
    if (colour == lastColour) {
      return;
    }

    lastColour = colour;
    pixel.setPixelColor(0, colour);
    pixel.show();
  }
};

static StatusLedController statusLed(rgbLed);

// =============================================================================
// Backlight
// =============================================================================

static uint8_t displayBrightnessDuty(uint8_t percentage) {
  uint16_t clamped = percentage > 100 ? 100 : percentage;

  if (clamped == 0) {
    return 0;
  }

  // Perceptual quadratic mapping. The public value is a true 0-100 percent;
  // PWM is calculated only here so 50% does not look almost fully bright.
  uint32_t squared = (uint32_t)clamped * (uint32_t)clamped;
  uint8_t duty = (uint8_t)((squared * 255U + 5000U) / 10000U);
  return maxInt16((int16_t)2, (int16_t)duty);
}

static void initializeBacklight() {
  uint8_t duty = displayBrightnessDuty(DISPLAY_BRIGHTNESS);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_LCD_BL, 5000, 8);
  ledcWrite(PIN_LCD_BL, duty);
#else
  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
  ledcAttachPin(PIN_LCD_BL, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, duty);
#endif
}

// =============================================================================
// Public entry points
// =============================================================================

void voidAscentSetup() {
  Serial.begin(115200);

  randomGenerator.state = esp_random();

  button.begin();

  statusLed.begin();

  initializeBacklight();

  if (!gfx->begin(LCD_SPI_HZ)) {
    Serial.println("LCD initialization failed.");

    while (true) {
      statusLed.showHardwareFault(true);

      delay(160);

      statusLed.showHardwareFault(false);

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

  // Consume edge events immediately. Otherwise one press can be processed
  // repeatedly during the several loop iterations between rendered frames.
  button.clearEvents();

  // auto transition to briefing after 3.9 seconds
  // if (game.screen == ScreenMode::SPLASH && now - game.screenStartedAt > 3900)
  // {
  //   setScreen(ScreenMode::BRIEFING);
  // }

  if (now - lastFrameAt < FRAME_INTERVAL_MS) {
    statusLed.update(now);
    delay(1);
    return;
  }

  float deltaSeconds = (now - lastFrameAt) / 1000.0f;

  if (deltaSeconds > MAX_FRAME_DELTA_SECONDS) {
    deltaSeconds = MAX_FRAME_DELTA_SECONDS;
  }

  lastFrameAt = now;

  if (game.screen == ScreenMode::FLIGHT) {
    updateFlight(deltaSeconds, now);
  }

  renderFrame(now);
  statusLed.update(now);
}
