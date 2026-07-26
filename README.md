# PocketGame + Void Ascent

A single Arduino sketch containing a reusable **PocketGame system layer** and the existing **Void Ascent** game.

The current firmware stores campaign progress in the ESP32-C6's on-chip NVS through `PreferencesStorageBackend`. There is no SD-card requirement. Void Ascent only calls `PocketStorage`, so changing the storage backend does not change game code.

## Open and compile

1. Keep the complete `PocketGame_VoidAscent` folder together.
2. Open `PocketGame_VoidAscent.ino` in Arduino IDE.
3. Select the Waveshare ESP32-C6 board configuration you already use.
4. Install these libraries:
   - Arduino_GFX_Library
   - Adafruit GFX Library
   - Adafruit NeoPixel
5. Compile and upload.

The sketch and every source file under `src/` are compiled into one firmware by Arduino IDE.

## Default controls

| Physical input | Shared control | Void Ascent use |
|---|---|---|
| Tap | Select | Continue, launch, retry |
| Hold | Cycle | Cycle unlocked missions |
| Press edge | Action | Separate the active rocket stage |

Games consume semantic controls, not GPIO pins. To add or replace physical buttons later, update the control-source implementation or board configuration rather than every game.

## Folder structure

```text
PocketGame_VoidAscent/
├── PocketGame_VoidAscent.ino       # firmware composition and game registry
├── src/
│   ├── PocketGame/                 # reusable system library
│   │   ├── PocketGame.*            # lifecycle, registry, shared launcher
│   │   ├── PocketGameConfig.h      # all board pins and display settings
│   │   ├── PocketControls.*        # input abstraction and mappings
│   │   ├── PocketDisplay.*         # LCD, backlight, RAM framebuffer, present
│   │   ├── PocketLed.*             # RGB LED hardware and animation helpers
│   │   ├── PocketNavigation.*      # screen and cycle/select navigation
│   │   ├── PocketStorage.*         # storage API visible to games
│   │   ├── PreferencesStorageBackend.* # default on-chip NVS backend
│   │   └── SdStorageBackend.*      # optional removable-card backend
│   └── Games/
│       └── VoidAscent/             # game-only physics, art, rules and screens
├── docs/
└── flash.sh
```

## What was moved out of Void Ascent

The game no longer initializes or directly owns:

- LCD pins, SPI bus, backlight, display driver, or screen presentation
- GPIO button and debounce/hold detection
- RGB LED hardware
- `Preferences` or any storage medium
- generic cycle/select menu state
- firmware/game registration and startup

Void Ascent still owns its game-specific rules, rendering, flight state, mission data, scoring, and LED *meaning*. It asks the shared LED API to show the selected colour.

## Adding games

Implement `pocketgame::IGame`, place the game under `src/Games/<GameName>/`, create one global game object in the `.ino`, then register it before `pocketGame.begin()`.

When two or more games are registered, PocketGame automatically shows its shared one-button launcher. With one registered game, it boots directly into that game to preserve the current experience.

See `docs/ADDING_A_GAME.md`.

## Storage

Default:

```cpp
pocketgame::PreferencesStorageBackend storageBackend;
pocketgame::PocketGameSystem pocketGame(controls, storageBackend);
```

The game continues to use calls such as:

```cpp
system.storage().getUInt8("unlocked", 1);
system.storage().putInt32("best0", score);
```

It has no knowledge of NVS, SD, LittleFS, files, mount points, or chip-select pins. The new Preferences backend also reads the old `UChar` and `Int` values created by the original Void Ascent build, so existing progress can migrate into the abstraction.

See `docs/STORAGE_BACKENDS.md` for swapping backends.

## Notes

- RAM rendering is preserved: the complete 172×320 frame is drawn into `GFXcanvas16` and sent to the LCD once per rendered frame.
- The original mission validation remains compile-time checked.
- `flash.sh` remains available for Arduino CLI users.
