# PocketGame Arcade

A single Arduino firmware for the Waveshare ESP32-C6 1.47-inch LCD board.
It boots into a reusable PocketGame console UI and contains two games plus an
optional Wi-Fi prank app:

1. **Void Ascent** — the existing one-button rocket campaign.
2. **City Tower** — a detailed crane-and-tower stacking game inspired by classic
   phone tower builders.
3. **Free Wifi** — an open captive-portal access point that redirects HTTP
   connectivity checks to the rickroll video. It does not provide internet
   routing itself, so the client needs cellular/upstream connectivity to load
   YouTube after the redirect.

The games do not own display pins, the physical button, RGB LED, backlight PWM,
or storage hardware. Those are provided by the shared PocketGame system.

## Open and compile

Open:

`PocketGame_Arcade/PocketGame_Arcade.ino`

Required Arduino libraries:

- Arduino_GFX_Library
- Adafruit GFX Library
- Adafruit NeoPixel

Select the same ESP32-C6/Waveshare board configuration used by the previous
Void Ascent build, then compile and upload.

The root `.clangd` uses the Arduino-generated compilation database at
`build/esp32.esp32.esp32c6/compile_commands.json`, so Zed/clangd receives the
same ESP32 core and library paths as the firmware build. Refresh it after
changing the board or installed libraries with:

```bash
arduino-cli compile --only-compilation-database \
  --fqbn esp32:esp32:esp32c6 \
  --build-path build/esp32.esp32.esp32c6 .
```

Project `.zed/settings.json` passes the ESP32 RISC-V compiler to clangd so it
can locate the toolchain C++ headers. Reload the Zed project after refreshing
the compilation database.

## Boot flow

PocketGame never launches a game immediately.

```text
POCKETGAME BOOT
       |
       v
MAIN MENU
  |-- GAMES
  |     |-- VOID ASCENT
  |     |-- CITY TOWER
  |     |-- FREE WIFI
  |     `-- BACK
  `-- SETTINGS
        `-- DISPLAY BRIGHTNESS
```

## One-button controls

The shared physical mapping is now consistent everywhere:

- **Click / short press:** cycle to the next menu item.
- **Press and hold:** select or confirm the highlighted item.

Games consume these semantic events instead of reading GPIO directly.

### During City Tower

- Click while a floor is swinging: drop it.
- Hold during play: pause.
- In pause/result menus: click cycles, hold selects.

### During Void Ascent

- Click during powered flight: separate the active stage.
- Game and mission menus: click cycles, hold selects.
- The Void Ascent opening menu includes a PocketGame return option.

## Safe brightness setting

Settings contains only display brightness:

- User range: **5% to 100%**
- Step: **5%**
- User-facing **100% maps to 60% of the hardware PWM range**
- The value is stored through `PocketStorage`

This limit is enforced inside `PocketDisplay`; games cannot bypass it through
normal PocketGame APIs.

## Free Wifi prank app

The **Free Wifi** app is off when opened. If no upstream is saved, the
controller offers `START SETUP` and `EXIT`. `START SETUP` creates the open
`FreeWifi` AP in setup mode; connecting to it opens the setup portal through
`connectivitycheck.gstatic.com`. If an upstream is saved, the controller offers
`START PRANK`, `FORGET UPSTREAM`, and `EXIT`.

Prank mode runs AP+STA with NAPT routing and a split DNS proxy: the gstatic
probe remains local while normal DNS queries are forwarded through the
upstream network.

The app exposes exactly two web pages: `/setup` configures the upstream
SSID/password, prank URL, and forget action; `/prank` is the destination page
that redirects clients to the saved URL. Credentials and the URL persist in
the `wifi-prank` storage namespace. Because setup is served over the open AP,
use a guest or test upstream network rather than a sensitive home password.

## Storage abstraction

The complete firmware currently composes:

```cpp
pocketgame::PreferencesStorageBackend storageBackend;
```

This stores settings and game saves in on-chip ESP32 NVS. Each game only uses:

```cpp
system.storage().getUInt32(...);
system.storage().putUInt32(...);
```

It has no knowledge of Preferences, NVS, SD cards, files, or paths.
`SdStorageBackend` remains available as an optional replacement. Changing the
backend in the sketch does not require changing either game.

Namespaces:

- `pocketgame` — brightness and future system settings
- `voidascent` — Void Ascent progression and best scores
- `citytower` — City Tower records and lifetime population

## City Tower features

- Pendulum-like crane swing and release velocity
- Increasing swing speed and difficulty as the tower grows
- Three lives
- Perfect, good, rough, and missed landings
- Perfect-drop combos and score bonuses
- Residents parachuting into successful floors
- Population, score, floor count, high score, and best height
- Four detailed floor styles with lit windows, doors, trim, depth, and damage
- Tower sway, accumulated lean, particles, impact effects, and LED feedback
- Scrolling camera as the tower rises
- Day-to-night skyline transition
- Animated clouds, layered city silhouettes, roads, windows, crane truss, and
  tiny parachuting residents
- In-game pause, restart, game menu, and PocketGame return paths
- How-to pages using the same single-button navigation

## Folder structure

```text
PocketGame_Arcade/
├── PocketGame_Arcade.ino
├── src/
│   ├── PocketGame/
│   │   ├── PocketGame.*              # console lifecycle and system UI
│   │   ├── PocketControls.*          # hardware-independent input mapping
│   │   ├── PocketDisplay.*           # LCD, RAM framebuffer, safe brightness
│   │   ├── PocketLed.*               # shared RGB LED API
│   │   ├── PocketNavigation.*        # cycle/select menu logic
│   │   ├── PocketStorage.*           # game-facing storage API
│   │   ├── PreferencesStorageBackend.*
│   │   └── SdStorageBackend.*
│   ├── Games/
│       ├── VoidAscent/
│       │   ├── VoidAscentGame.*
│       │   └── MissionValidation.h
│       └── CityTower/
│           └── CityTowerGame.*
│   └── Apps/
│       ├── VideoPlayer/
│       │   └── VideoPlayerGame.*
│       └── WifiPrank/
│           └── WifiPrank.*
└── docs/
```

## Validation performed

The complete sketch and every C++ translation unit were syntax-compiled under
GNU C++17 with strict warnings. The optional SD backend was also compiled in
its enabled configuration. Final validation still requires compiling with the
installed ESP32 Arduino core and testing on the physical board.
