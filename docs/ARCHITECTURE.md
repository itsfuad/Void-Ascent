# PocketGame architecture

## Composition root

`PocketGame_Arcade.ino` is the only place that chooses concrete hardware and
storage implementations. It creates one control source, one storage backend,
one PocketGame system, and the game objects.

## System ownership

`PocketGameSystem` owns:

- boot screen
- main menu
- game library
- display settings
- game registration and launching
- shared display framebuffer
- shared controls
- RGB LED
- active storage namespace
- removable-media access through `PocketMediaCard`

A game receives `PocketGameSystem&` and uses the public APIs. It does not own or
reinitialize hardware.

## System screens

```text
BOOT -> HOME -> GAMES -> GAME
          |
          `-> SETTINGS -> HOME
```

`requestLauncher()` closes the current game namespace, reopens the system
namespace, restores the safe brightness value, and returns to HOME.

## Input boundary

`IControlSource` translates physical hardware into `ControlEvents`.
The default single-button source maps:

```text
short click -> cycle
timed hold  -> select
```

Raw press/release edges remain in the event object for future input devices or
games, but menu code uses only semantic events.

## Storage and media boundaries

`IStorageBackend` is implemented by concrete save media. `PocketStorage`
provides the fixed-width values/byte API used by the system and games.

```text
Game -> PocketStorage -> IStorageBackend -> Preferences/NVS
                                      `-> optional SD save backend
```

Large sequential assets use a separate API:

```text
Video Player -> PocketMediaCard -> onboard TF card -> /videos/*.pgv
```

This separation lets settings and save data remain in reliable on-chip NVS
while videos are streamed from removable storage.

## Display safety

`PocketDisplay::setBrightness()` accepts a logical 5–100 value. The PWM mapping
caps 100 at 60% electrical duty. The policy remains in the driver rather than
in a menu or game.
