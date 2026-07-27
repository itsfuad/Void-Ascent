#!/usr/bin/env python3
"""Convert ordinary videos into PocketGame Video (.pgv) files.

Inputs default to ./video_src and outputs default to ./videos.
The generated files are copied to the SD card's /videos directory.
"""

from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

WIDTH = 172
HEIGHT = 320
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


def build_filter(mode: str, fps: float) -> str:
    if mode == "crop":
        geometry = (
            f"scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=increase:"
            "flags=lanczos,"
            f"crop={WIDTH}:{HEIGHT}"
        )
    else:
        geometry = (
            f"scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=decrease:"
            "flags=lanczos,"
            f"pad={WIDTH}:{HEIGHT}:(ow-iw)/2:(oh-ih)/2:color=black"
        )
    return f"fps={fps:g},{geometry},setsar=1"


def convert_one(
    source: Path,
    destination: Path,
    fps: float,
    pixel_format_name: str,
    mode: str,
    ffmpeg: str,
) -> tuple[int, int]:
    format_id, ffmpeg_pixel_format, bytes_per_pixel = PIXEL_FORMATS[pixel_format_name]
    frame_bytes = WIDTH * HEIGHT * bytes_per_pixel
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
            "-vf", build_filter(mode, fps),
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
            WIDTH,
            HEIGHT,
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

        return frame_count, destination.stat().st_size
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
        help="fit adds black bars; crop fills the whole display",
    )
    parser.add_argument(
        "--clean", action="store_true",
        help="remove existing .pgv files from the output directory first",
    )
    parser.add_argument(
        "--ffmpeg", default="ffmpeg",
        help="ffmpeg executable name/path (default: ffmpeg)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if shutil.which(args.ffmpeg) is None and not Path(args.ffmpeg).is_file():
        print(f"error: ffmpeg not found: {args.ffmpeg}", file=sys.stderr)
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
            frame_count, byte_count = convert_one(
                source=source,
                destination=destination,
                fps=args.fps,
                pixel_format_name=args.format,
                mode=args.mode,
                ffmpeg=args.ffmpeg,
            )
            seconds = frame_count / args.fps
            mib = byte_count / (1024 * 1024)
            print(
                f"OK  {source.name} -> {destination.name}  "
                f"{frame_count} frames, {seconds:.1f}s, {mib:.1f} MiB"
            )
        except (subprocess.CalledProcessError, OSError, RuntimeError, ValueError) as error:
            failures += 1
            print(f"FAIL {source.name}: {error}", file=sys.stderr)

    if failures:
        print(f"{failures} conversion(s) failed", file=sys.stderr)
        return 1

    print()
    print(f"Copy the generated .pgv files from {output_dir}")
    print("to the SD card directory /videos (FAT32 card).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
