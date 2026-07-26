# Storage backends

## Contract

Games use `PocketStorage`, which delegates to an `IStorageBackend` selected by the firmware composition. The game API is typed key/value storage:

- `getUInt8` / `putUInt8`
- `getUInt16` / `putUInt16`
- `getUInt32` / `putUInt32`
- `getInt32` / `putInt32`
- `getBool` / `putBool`
- `getBytes` / `putBytes`
- `contains`, `remove`, and `clear`

Each game supplies a namespace through `IGame::storageNamespace()`. PocketGame opens the appropriate namespace before calling the game's `begin()`.

## Default: on-chip NVS

No SD card is required.

```cpp
pocketgame::PreferencesStorageBackend storageBackend;
pocketgame::PocketGameSystem pocketGame(controls, storageBackend);
```

This is the configuration already used by the supplied sketch.

## Optional SD backend

The optional backend is isolated behind `POCKETGAME_ENABLE_SD_STORAGE`. To use it:

1. Set `POCKETGAME_ENABLE_SD_STORAGE` to `1` in `PocketGameConfig.h` or in your build flags.
2. Include `SdStorageBackend.h` in the `.ino`.
3. Replace only the backend composition:

```cpp
pocketgame::SdStorageBackend storageBackend(YOUR_SD_CS_PIN);
pocketgame::PocketGameSystem pocketGame(controls, storageBackend);
```

No game source changes are needed.

## Other backends

A LittleFS, external EEPROM, database, network, or test-memory backend can be added by implementing `IStorageBackend`. Only the top-level firmware composition changes.
