VOID ASCENT - Stage Stack build

This sketch is split into .ino + .cpp specifically to bypass Arduino IDE's
automatic .ino prototype generator, which caused the LevelDef, StageDef,
ScreenMode, FlightPhase, and RocketStageGeometry errors.

Install:
1. Delete or close the older Void_Ascent_C6 sketch folder.
2. Open Void_Ascent_C6_StageStack/Void_Ascent_C6_StageStack.ino.
3. Confirm this folder contains only:
   - Void_Ascent_C6_StageStack.ino
   - VoidAscentGame.cpp
   - VoidAscentGame.h
   - README.txt
4. Compile for the Waveshare ESP32-C6 board configuration.

Required libraries:
- Arduino_GFX_Library
- Adafruit GFX Library
- Adafruit NeoPixel

Command-line build and flash (no Arduino IDE):
1. Install Arduino CLI plus the `esp32:esp32` platform and the libraries above.
2. Connect the board and run `./flash.sh`. It detects the serial port; when
   several are connected, it shows a numbered selector. Pass a port explicitly
   (for example, `./flash.sh /dev/ttyACM0`) to skip selection.
3. If uploading does not start, hold the board's BOOT button while connecting
   it, then run the command again.

If the screen is upside down, change LCD_ROTATION from 0 or 2 in
VoidAscentGame.cpp.
