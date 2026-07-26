# Refactor notes

## Corrected system behavior

The first reusable build launched Void Ascent immediately when only one game
was registered and exposed games directly when multiple games were registered.
This build always starts as PocketGame and always shows the system hierarchy:

```text
Boot -> Main Menu -> Games or Settings
```

## Corrected control semantics

The shared mapping is now:

- click = cycle
- hold = select

Void Ascent was adapted so stage separation uses the completed click event,
while its menus use the shared cycle/select policy.

## Brightness safety

The setting is restricted to 5–100 in 5% steps. The display driver maps the
maximum logical value to 60% PWM duty, keeping the safety limit independent of
UI and game code.

## Added second game

City Tower is compiled into the same firmware and registered beside Void
Ascent. It reuses PocketDisplay, PocketControls, PocketLed, PocketNavigation,
and PocketStorage without direct hardware access.
