#!/usr/bin/env python3
"""Convert ordinary videos into PocketGame Video (.pgv) files.

Inputs default to ./video_src and outputs default to ./videos.
Portrait sources become 172x320 PGV files. Landscape sources become 320x172
PGV files and are rotated by the player so they fill the display when the
PocketGame is held sideways.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

PORTRAIT_WIDTH = 172
PORTRAIT_HEIGHT = 320
LANDSCAPE_WIDTH = 320
LANDSCAPE_HEIGHT = 172
MAGIC = b"PGV1"
HEADER_STRUCT = struct.Struct("<4sHHHBBII12s")
PIXEL_FORMATS = {
    "rgb332": (1, "rgb8", 1),
    "rgb565": (2, "rgb565le", 2),
}
VIDEO_EXTENSIONS = {
    ".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".mpeg",
    ".mpg", ".wmv", ".flv", ".gif",
}


def safe_stem(stem: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", stem).strip("._-")
    return cleaned or "video"


def build_filter(mode: str, fps: float, width: int, height: int) -> str:
    if mode == "crop":
        geometry = (
            f"scale={width}:{height}:force_original_aspect_ratio=increase:"
            "flags=lanczos,"
            f"crop={width}:{height}"
        )
    else:
        geometry = (
            f"scale={width}:{height}:force_original_aspect_ratio=decrease:"
            "flags=lanczos,"
            f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black"
        )
    return f"fps={fps:g},{geometry},setsar=1"


def probe_display_dimensions(source: Path, ffprobe: str) -> tuple[int, int]:
    command = [
        ffprobe,
        "-v", "error",
        "-select_streams", "v:0",
        "-show_entries",
        "stream=width,height:stream_tags=rotate:stream_side_data=rotation",
        "-of", "json",
        str(source),
    ]
    result = subprocess.run(
        command,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    data = json.loads(result.stdout)
    streams = data.get("streams", [])
    if not streams:
        raise RuntimeError("FFprobe found no video stream")

    stream = streams[0]
    width = int(stream.get("width", 0))
    height = int(stream.get("height", 0))
    if width <= 0 or height <= 0:
        raise RuntimeError("FFprobe returned invalid video dimensions")

    rotation = 0
    tags = stream.get("tags") or {}
    if "rotate" in tags:
        try:
            rotation = int(round(float(tags["rotate"])))
        except (TypeError, ValueError):
            rotation = 0

    for side_data in stream.get("side_data_list") or []:
        if "rotation" in side_data:
            try:
                rotation = int(round(float(side_data["rotation"])))
            except (TypeError, ValueError):
                pass
            break

    if abs(rotation) % 180 == 90:
        width, height = height, width

    return width, height


def choose_output_dimensions(
    source: Path,
    orientation: str,
    ffprobe: str,
) -> tuple[int, int, str]:
    if orientation == "portrait":
        return PORTRAIT_WIDTH, PORTRAIT_HEIGHT, "portrait"
    if orientation == "landscape":
        return LANDSCAPE_WIDTH, LANDSCAPE_HEIGHT, "landscape"

    source_width, source_height = probe_display_dimensions(source, ffprobe)
    if source_width > source_height:
        return LANDSCAPE_WIDTH, LANDSCAPE_HEIGHT, "landscape"
    return PORTRAIT_WIDTH, PORTRAIT_HEIGHT, "portrait"


def convert_one(
    source: Path,
    destination: Path,
    fps: float,
    pixel_format_name: str,
    mode: str,
    orientation: str,
    ffmpeg: str,
    ffprobe: str,
) -> tuple[int, int, str, int, int]:
    width, height, chosen_orientation = choose_output_dimensions(
        source, orientation, ffprobe
    )
    format_id, ffmpeg_pixel_format, bytes_per_pixel = PIXEL_FORMATS[pixel_format_name]
    frame_bytes = width * height * bytes_per_pixel
    fps_times_100 = round(fps * 100)
    if not 100 <= fps_times_100 <= 6000:
        raise ValueError("FPS must be between 1 and 60")

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        prefix=f".{destination.stem}-", suffix=".raw", dir=destination.parent,
        delete=False,
    ) as temporary:
        raw_path = Path(temporary.name)

    try:
        command = [
            ffmpeg,
            "-hide_banner",
            "-loglevel", "error",
            "-y",
            "-i", str(source),
            "-an",
            "-vf", build_filter(mode, fps, width, height),
            "-pix_fmt", ffmpeg_pixel_format,
            "-f", "rawvideo",
            str(raw_path),
        ]
        subprocess.run(command, check=True)

        raw_size = raw_path.stat().st_size
        if raw_size == 0 or raw_size % frame_bytes != 0:
            raise RuntimeError(
                f"FFmpeg produced an invalid raw stream ({raw_size} bytes)"
            )

        frame_count = raw_size // frame_bytes
        if frame_count > 0xFFFFFFFF:
            raise RuntimeError("Video contains too many frames for PGV1")

        header = HEADER_STRUCT.pack(
            MAGIC,
            width,
            height,
            fps_times_100,
            format_id,
            0,
            frame_count,
            frame_bytes,
            b"\0" * 12,
        )

        with destination.open("wb") as output, raw_path.open("rb") as raw:
            output.write(header)
            shutil.copyfileobj(raw, output, length=1024 * 1024)

        return (
            frame_count,
            destination.stat().st_size,
            chosen_orientation,
            width,
            height,
        )
    finally:
        raw_path.unlink(missing_ok=True)


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    project_dir = script_dir.parent

    parser = argparse.ArgumentParser(
        description="Convert ./video_src files into PocketGame ./videos/*.pgv"
    )
    parser.add_argument(
        "--input", type=Path, default=project_dir / "video_src",
        help="source directory (default: project/video_src)",
    )
    parser.add_argument(
        "--output", type=Path, default=project_dir / "videos",
        help="output directory (default: project/videos)",
    )
    parser.add_argument(
        "--fps", type=float, default=12.0,
        help="output frame rate, 1-60 (default: 12)",
    )
    parser.add_argument(
        "--format", choices=sorted(PIXEL_FORMATS), default="rgb332",
        help="rgb332 is smaller/faster; rgb565 is larger/higher quality",
    )
    parser.add_argument(
        "--mode", choices=("fit", "crop"), default="fit",
        help="fit adds black bars; crop fills the selected orientation",
    )
    parser.add_argument(
        "--orientation", choices=("auto", "portrait", "landscape"),
        default="auto",
        help="output orientation (default: auto from each source video)",
    )
    parser.add_argument(
        "--clean", action="store_true",
        help="remove existing .pgv files from the output directory first",
    )
    parser.add_argument(
        "--ffmpeg", default="ffmpeg",
        help="ffmpeg executable name/path (default: ffmpeg)",
    )
    parser.add_argument(
        "--ffprobe", default="ffprobe",
        help="ffprobe executable name/path (default: ffprobe)",
    )
    return parser.parse_args()


def executable_exists(value: str) -> bool:
    return shutil.which(value) is not None or Path(value).is_file()


def main() -> int:
    args = parse_args()

    if not executable_exists(args.ffmpeg):
        print(f"error: ffmpeg not found: {args.ffmpeg}", file=sys.stderr)
        return 2
    if args.orientation == "auto" and not executable_exists(args.ffprobe):
        print(f"error: ffprobe not found: {args.ffprobe}", file=sys.stderr)
        return 2

    input_dir = args.input.resolve()
    output_dir = args.output.resolve()
    input_dir.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.clean:
        for old_file in output_dir.glob("*.pgv"):
            old_file.unlink()

    sources = sorted(
        path for path in input_dir.iterdir()
        if path.is_file() and not path.name.startswith(".")
        and path.suffix.lower() in VIDEO_EXTENSIONS
    )

    if not sources:
        print(f"No supported videos found in {input_dir}")
        print("Put MP4/MOV/MKV/AVI/WebM/GIF files there and run again.")
        return 0

    failures = 0
    for source in sources:
        destination = output_dir / f"{safe_stem(source.stem)}.pgv"
        try:
            frame_count, byte_count, chosen, width, height = convert_one(
                source=source,
                destination=destination,
                fps=args.fps,
                pixel_format_name=args.format,
                mode=args.mode,
                orientation=args.orientation,
                ffmpeg=args.ffmpeg,
                ffprobe=args.ffprobe,
            )
            seconds = frame_count / args.fps
            mib = byte_count / (1024 * 1024)
            print(
                f"OK  {source.name} -> {destination.name}  "
                f"{chosen} {width}x{height}, "
                f"{frame_count} frames, {seconds:.1f}s, {mib:.1f} MiB"
            )
        except (
            subprocess.CalledProcessError,
            json.JSONDecodeError,
            OSError,
            RuntimeError,
            ValueError,
        ) as error:
            failures += 1
            print(f"FAIL {source.name}: {error}", file=sys.stderr)

    if failures:
        print(f"{failures} conversion(s) failed", file=sys.stderr)
        return 1

    print()
    print(f"Copy the generated .pgv files from {output_dir}")
    print("to the SD card directory /videos (FAT32 card).")
    print("Portrait videos play upright; landscape videos fill the screen sideways.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
