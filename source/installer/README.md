# RevisionOS Installer

Desktop installer and mini app store for RevisionOS on the iPod Classic 7th Generation (`MC297LL/A`, RetailOS 2.0.5).

## Features

- **Install or reapply RevisionOS:** Guides users through Apple 2.0.5 IPSW verification, custom `revola.bin` and `loader.bin` assembly, NOR backup, extension/homebrew selection, and DFU installation.
- **Migrate from RepriseOS or RevolaOS:** Automatically detects existing RepriseOS (`/osos-cfw.bin`, `/iPod_Control/Games_RO/`) or legacy root RevolaOS (`/revola.bin`, `/loader.bin`) layouts, copies games into `/Revola/Games/`, installs/updates extensions (including `homebrew.rvx`), and imports existing selections into the DFU installer.
- **Extension and homebrew selection:** Filter extensions by category (`Audio`, `Customization`, `Media`, `Utility`, `Connectivity`, `Developer`), choose presets (`Recommended`, `Minimal`, `Select all`), import from a connected iPod, and bundle games (`Doodle Jump`, `2048`) and apps (`Dice`) with automatic `homebrew.rvx` inclusion.
- **Store and updates:** Fetches `catalog.json` from `aurelian-th/ipod-things`, highlights what is new and version bumps on a connected Disk Mode iPod, and installs, updates, or removes extensions, games, apps, and `revola.bin` in place.
- **Uninstall or restore:** Removes RevisionOS files from a Disk Mode iPod while optionally keeping `/Revola/Data/`, lists saved NOR backups, and explains how to restore RetailOS.

RevisionOS temporarily runs on the RevolaOS core and bootloader backbone (`/Revola/` paths) until upstream RepriseOS implements all required features.

## Project structure

- `crates/revisionos-core/`: Pure-Rust library for GitHub catalog fetching, `.rvx` (`RVMF` v3) parsing, `Manifest.plist` parsing, firmware recipe assembly, Disk Mode iPod scanning, and atomic sync/uninstall.
- `src-tauri/`: Tauri 2 desktop backend linking `revisionos-core` with `reprise-bundle` and `reprise-device` for DFU USB checks, IPSW decryption, NOR backups, and uploads.
- `src/`: React + TypeScript + Tailwind CSS UI and dev preview harness.
- `tests/`: Node test suite covering frontend store logic, installation state, firmware validation, and scenarios.

## Development and tests

Run the Rust domain crate tests natively on any OS:

```bash
cd crates/revisionos-core && cargo test
```

Run the frontend typecheck, Node tests, and production bundle build:

```bash
npm install
npm run check
```

To preview the UI and Store flows in a browser with simulated iPod state:

```bash
npm run dev
```

Building the full Tauri desktop binary (`npm run tauri build`) on Linux requires system WebKitGTK and GTK development packages (`libwebkit2gtk-4.1-dev`, `libgtk-3-dev`, `libayatana-appindicator3-dev`, `librsvg2-dev`, `libusb-1.0-0-dev`).
