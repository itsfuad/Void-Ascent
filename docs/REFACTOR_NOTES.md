# Refactor notes

## Preserved

- Existing Void Ascent missions, physics, scoring, particles, rocket rendering, campaign flow, and 30 FPS frame interval
- Full-frame `GFXcanvas16` RAM rendering to avoid animation flicker
- Existing one-button behavior: hold cycles missions, tap selects, press separates a stage
- Existing on-chip campaign keys (`unlocked`, `current`, and `bestN`)
- Mission reachability validation

## Changed

- The root sketch now registers games with `PocketGameSystem`.
- LCD setup and presentation moved to `PocketDisplay`.
- Input debounce, hold detection, and semantic mapping moved to `PocketControls`.
- RGB LED hardware and reusable animation helpers moved to `PocketLed`.
- Menu and screen transition state moved to `PocketNavigation`.
- Direct `Preferences` calls were replaced with `PocketStorage`.
- On-chip NVS is now only one replaceable backend.
- An optional SD backend is supplied but disabled, so the default firmware has no SD dependency.
- Board pins and hardware values are centralized in `PocketGameConfig.h`.

## Validation performed

- All project C++ translation units and the `.ino` were checked together with a host-side Arduino API shim under GNU C++17.
- The optional SD backend was also syntax-checked with its feature flag enabled.
- Menu wrap/select behavior and typed storage fallback/read/write behavior were exercised with a small in-memory backend test.

A real ESP32-C6 Arduino toolchain and physical board were not available in the sandbox, so final board compilation and hardware behavior still need the normal Arduino IDE upload check.
