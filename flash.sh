#!/usr/bin/env bash
set -euo pipefail

readonly FQBN="esp32:esp32:esp32c6"
readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly BUILD_DIR="$SCRIPT_DIR/build/${FQBN//:/.}"

if ! command -v arduino-cli >/dev/null; then
  echo "arduino-cli is required. Install it from https://arduino.github.io/arduino-cli/" >&2
  exit 1
fi

port="${1:-}"
if [[ -z "$port" ]]; then
  shopt -s nullglob
  ports=(/dev/ttyACM* /dev/ttyUSB*)

  case "${#ports[@]}" in
    0)
      echo "No serial ports found. Connect the board, then run: $0 /dev/ttyACM0" >&2
      exit 1
      ;;
    1)
      port="${ports[0]}"
      echo "Using serial port: $port"
      ;;
    *)
      echo "Multiple serial ports found:"
      for index in "${!ports[@]}"; do
        printf '  %d) %s\n' "$((index + 1))" "${ports[index]}"
      done

      while :; do
        read -r -p "Select the ESP32 port [1-${#ports[@]}]: " selection
        if [[ "$selection" =~ ^[0-9]+$ ]] &&
            ((10#$selection >= 1 && 10#$selection <= ${#ports[@]})); then
          port="${ports[$((10#$selection - 1))]}"
          break
        fi
        echo "Enter a number from 1 to ${#ports[@]}." >&2
      done
      ;;
  esac
fi

echo "Compiling for $FQBN..."
if ! arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_DIR" "$SCRIPT_DIR"; then
  echo "Compilation failed; nothing was flashed." >&2
  exit 1
fi

echo "Flashing $port..."
arduino-cli upload --fqbn "$FQBN" --port "$port" --input-dir "$BUILD_DIR" "$SCRIPT_DIR"
