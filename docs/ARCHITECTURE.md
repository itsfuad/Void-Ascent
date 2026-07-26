# Architecture

```mermaid
flowchart TD
    INO[PocketGame_VoidAscent.ino] --> SYS[PocketGameSystem]
    INO --> GAME[VoidAscentGame]
    SYS --> REG[Game registry / launcher]
    SYS --> CTRL[PocketControls]
    SYS --> DISP[PocketDisplay]
    SYS --> LED[PocketLed]
    SYS --> STORE[PocketStorage]
    CTRL --> SOURCE[IControlSource]
    SOURCE --> ONE[SingleButtonControlSource]
    STORE --> BACKEND[IStorageBackend]
    BACKEND --> NVS[PreferencesStorageBackend\non-chip NVS]
    BACKEND -. optional .-> SD[SdStorageBackend]
    GAME --> CTRL
    GAME --> DISP
    GAME --> LED
    GAME --> STORE
    GAME --> NAV[MenuNavigator / ScreenNavigator]
```

## Ownership boundary

`PocketGameSystem` owns hardware-facing services and the active storage namespace. A game receives the system by reference and accesses only stable APIs.

`VoidAscentGame` owns only its domain:

- missions and compile-time validation
- physics and timing
- particles, fragments, rocket geometry and drawing
- scoring and progression decisions
- splash, briefing, flight and result presentation

## Main loop

```mermaid
sequenceDiagram
    participant Arduino
    participant PocketGameSystem
    participant Controls
    participant VoidAscent
    participant Display

    Arduino->>PocketGameSystem: loop()
    PocketGameSystem->>Controls: update(now)
    PocketGameSystem->>VoidAscent: loop(system, now)
    VoidAscent->>VoidAscent: process semantic controls
    VoidAscent->>VoidAscent: update physics when frame is due
    VoidAscent->>Display: draw into canvas()
    VoidAscent->>Display: present()
    PocketGameSystem->>Controls: consume events
```
