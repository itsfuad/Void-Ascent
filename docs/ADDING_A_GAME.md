# Adding another game

Create a directory under `src/Games/` and implement `pocketgame::IGame`.

```cpp
class NewGame final : public pocketgame::IGame {
public:
  const char *id() const override { return "new-game"; }
  const char *title() const override { return "NEW GAME"; }
  const char *description() const override { return "SHORT DESCRIPTION"; }
  const char *storageNamespace() const override { return "newgame"; }

  void begin(pocketgame::PocketGameSystem &system) override;
  void loop(pocketgame::PocketGameSystem &system, uint32_t now) override;
};
```

Register it in `PocketGame_Arcade.ino`:

```cpp
NewGame newGame;

void setup() {
  pocketGame.registerGame(voidAscent);
  pocketGame.registerGame(cityTower);
  pocketGame.registerGame(newGame);
  pocketGame.begin();
}
```

The PocketGame game library automatically expands to include it.

## Required rules

- Draw only through `system.display().canvas()`.
- Call `system.display().present()` after a complete RAM frame is rendered.
- Read controls through `system.controls().events()`.
- Use click/cycle and hold/select for menus.
- Save through `system.storage()` only.
- Drive status light through `system.led()` only.
- Call `system.requestLauncher()` to return to PocketGame.
- Do not include Preferences, SD, LCD drivers, NeoPixel drivers, or board pins.

## Recommended game screens

A one-button game should normally provide:

```text
GAME MENU -> PLAY
          -> HOW TO
          -> POCKETGAME

PLAY --hold--> PAUSE -> RESUME / RESTART / POCKETGAME
RESULT -> RETRY / GAME MENU / POCKETGAME
```
