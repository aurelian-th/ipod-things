#!/usr/bin/env python3
"""Extract assets and 55x55 icon from Doodle Jump JAR for native iPod eApp."""

import os
import struct
import sys
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
JAR_PATH = ROOT / "doodlejump_s2d8e1yz.jar"
ICON_SRC = ROOT / "icon.png"
ICON_OUT = ROOT / "icon_55.png"
ASSETS_DIR = ROOT / "assets"
SDK_DIR = ROOT.parent / "eapp-sdk"

RAW_ASSETS = ("i", "p", "g0", "g1", "g2", "g3", "b0", "b1", "l")


def _encode_rgba_png(width: int, height: int, rgba_bytes: bytes) -> bytes:
    """Write a non-interlaced 8-bit RGBA PNG accepted by eapp-pack."""
    def chunk(kind: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
        )

    raw_rows = bytearray()
    stride = width * 4
    for y in range(height):
        raw_rows.append(0)  # filter type 0 (None)
        raw_rows.extend(rgba_bytes[y * stride : (y + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    idat = zlib.compress(bytes(raw_rows), 9)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", idat)
        + chunk(b"IEND", b"")
    )


def build_icon_55() -> None:
    """Downscale the 512x512 user icon.png into a non-interlaced 55x55 RGBA PNG."""
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError(
            "Pillow (PIL) is required to resize icon.png to 55x55"
        ) from exc

    img = Image.open(ICON_SRC).convert("RGBA")
    resampling = getattr(getattr(Image, "Resampling", Image), "LANCZOS", Image.LANCZOS)
    img55 = img.resize((55, 55), resampling)
    png_bytes = _encode_rgba_png(55, 55, img55.tobytes("raw", "RGBA"))
    ICON_OUT.write_bytes(png_bytes)

    sys.path.insert(0, str(SDK_DIR))
    import eapp_package  # noqa: E402

    lcd5 = eapp_package.artwork(ICON_OUT.read_bytes())
    print(f"[+] Wrote {ICON_OUT.name} ({len(png_bytes)} B), validated lcd5 ({len(lcd5)} B)")


def extract_jar_assets() -> None:
    ASSETS_DIR.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(JAR_PATH, "r") as zf:
        for name in RAW_ASSETS:
            data = zf.read(name)
            (ASSETS_DIR / name).write_bytes(data)
            print(f"[+] Extracted assets/{name} ({len(data)} bytes)")

        i_data = (ASSETS_DIR / "i").read_bytes()
        d_arr = struct.unpack("<3898b", i_data[:3898])
        a_arr = struct.unpack(">3893h", i_data[3898 : 3898 + 3893 * 2])
        e_arr = struct.unpack(">169i", i_data[3898 + 3893 * 2 :])

        s_data = zf.read("s")
        extracted_slots = set()
        for bank in range(2):
            count = a_arr[2403 + bank]
            start = a_arr[2405 + bank]
            for idx in range(start, start + count):
                if d_arr[3875 + idx] == 0:
                    continue
                slot = a_arr[2407 + idx]
                offset = e_arr[4 + idx]
                size = e_arr[50 + idx]
                wav_bytes = s_data[offset : offset + size]
                if wav_bytes[:4] != b"RIFF" or wav_bytes[8:12] != b"WAVE":
                    raise ValueError(
                        f"Sound idx={idx} slot={slot} at {offset}+{size} is not RIFF WAVE"
                    )
                out_path = ASSETS_DIR / f"s{slot:02d}.wav"
                out_path.write_bytes(wav_bytes)
                extracted_slots.add(slot)
        print(f"[+] Extracted {len(extracted_slots)} WAV files: s00.wav..s{max(extracted_slots):02d}.wav")


def main() -> int:
    build_icon_55()
    extract_jar_assets()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

