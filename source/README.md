# RevisionOS source code

This directory contains the GPL-3.0 corresponding source code for the RevisionOS binaries, recipes, extensions, games, and desktop installer distributed in this repository. Apple copyrighted firmware bytes and personal NOR backups are never included; building firmware images reads from your own copy of `iPod_38.2.0.5.ipsw`.

## Layout

| Folder | Contents |
|---|---|
| `firmware/core/` | RevisionOS core firmware, extension loader, UI runtime, settings store, SDK, and recipe generator |
| `firmware/extensions/` | Loadable `.rvx` modules (`iap2`, `lyrics`, `appearance`, `playback-queue`, `hold-standby-clock`, `silent-remote-volume`, and upstream extensions) |
| `firmware/themes/` | `.rvt` theme compiler and Classic dark theme rules |
| `games/eapp-sdk/` | Native `.eapp` SDK and packaging tools |
| `games/homebrew/` | Native `.eapp` homebrew games and apps (`2048`, `dice`) |
| `games/doodle-jump/` | Native `.eapp` Doodle Jump port source (requires your own extracted J2ME assets) |
| `host/` | Rust host crates (`reprise-cli` recipe assembler, `reprise-bundle`, and `reprise-device`) |
| `installer/` | Cross-platform RevisionOS Installer (Tauri 2, Rust `revisionos-core`, and React/TypeScript UI) |

## Prerequisites

- Bare-metal ARM GCC toolchain (`arm-elf-eabi-` or `arm-none-eabi-`) for firmware, extensions, and `.eapp` games
- Python 3.10+ (with `pytest` for core unit tests)
- Rust toolchain (stable) for `host/` and `installer/`
- Node.js 20+ for the `installer/` frontend

## Build examples

### 1. Build the `reprise` host assembler

```bash
cd source/host
cargo build --release -p reprise-cli
```

### 2. Test core and build the extension SDK

```bash
cd source/firmware/core
PYTHONPATH=tools pytest -q
python3 tools/build.py sdk --cross-prefix arm-elf-eabi- --out .build/sdk
```

To build non-distributive firmware recipes (`firmware.json`, `firmware.data`) or a local `revola.bin`, pass your decrypted local Apple 2.0.5 inputs via `--inputs` and the `reprise` binary via `--assembler`.

### 3. Build `.rvx` extensions

```bash
make -C source/firmware/extensions SDK="$(pwd)/source/firmware/core/.build/sdk" CROSS_COMPILE=arm-elf-eabi-
```

Run host unit tests for individual extensions such as `lyrics` and `iap2`:

```bash
make -C source/firmware/extensions/lyrics check SDK="$(pwd)/source/firmware/core/.build/sdk"
make -C source/firmware/extensions/iap2 check SDK="$(pwd)/source/firmware/core/.build/sdk"
```

### 4. Build `.eapp` games and apps

```bash
make -C source/games/homebrew SDK="$(pwd)/source/games/eapp-sdk" CROSS_COMPILE=arm-elf-eabi-
```

### 5. Test and build the desktop installer

```bash
cd source/installer/crates/revisionos-core && cargo test
cd ../.. && npm install && npm run check
```

To build native desktop installers (`deb`, `rpm`, `AppImage`, `dmg`, `exe`), run `npm run tauri build` inside `source/installer/`. See `source/installer/README.md` for Linux WebKitGTK system dependencies.

