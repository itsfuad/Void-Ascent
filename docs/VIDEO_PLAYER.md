# PocketGame Video Player

The Video Player streams optimized `.pgv` files from the onboard microSD/TF
card. Game saves and PocketGame settings still use the configured
`PocketStorage` backend, so enabling video playback does not move saves to SD.

## SD card layout

Format the card as FAT32 and create:

```text
/videos
  intro.pgv
  animation.pgv
  trailer.pgv
```

The player scans only the root of `/videos` and sorts files alphabetically.
The maximum indexed video count is 24.

## Convert videos

Place ordinary videos in the project folder:

```text
video_src/
```

Then run:

```bash
./convert_videos.sh
```

Converted files appear in:

```text
videos/
```

Copy the `.pgv` files from that directory to `/videos` on the card.

### Useful options

```bash
# Fill the display by cropping instead of letterboxing
./convert_videos.sh --mode crop

# Higher color quality at twice the storage/bandwidth
./convert_videos.sh --format rgb565

# Use 15 frames per second
./convert_videos.sh --fps 15

# Delete old converted files before processing
./convert_videos.sh --clean
```

Defaults are 172×320, 12 FPS, aspect-fit with black padding, and RGB332. The
compact RGB332 mode uses about 0.63 MiB/s at 12 FPS. RGB565 uses about
1.26 MiB/s at 12 FPS.

The board has no speaker in this project, so audio is removed during
conversion.

## Controls

### Library

- Click: next video, then PocketGame back item
- Hold: play selected video or return to PocketGame

### Playback

- Click: immediately play the next video
- Hold: pause
- End of file: automatically advance and loop through the library

### Pause menu

- Click: cycle Resume, Restart, Video Library, PocketGame
- Hold: select

## PGV1 format

The converter writes a small 32-byte little-endian header followed by raw
frames. The firmware accepts:

- RGB332: one byte per pixel, converted to RGB565 while streaming
- RGB565LE: two bytes per pixel, read directly into the shared framebuffer

PGV is deliberately simple so the ESP32-C6 does not need a large video codec,
PSRAM, or another Arduino decoding library.
