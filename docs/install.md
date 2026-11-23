# Installing RevisionOS

RevisionOS runs on the iPod Classic 6G, 6.5G, and 7G with FAT32 storage and Apple firmware 2.0.5. Stock Apple firmware remains untouched on the iPod and can always be booted by holding **Menu** at startup.

## Requirements

- iPod Classic 6G, 6.5G, or 7G formatted as FAT32 (Windows/macOS standard iPod restore, not HFS+)
- Direct 30-pin USB cable (avoid USB hubs during DFU flashing)
- Your own copy of Apple's `iPod_38.2.0.5.ipsw`
- A backup of your music library and iPod data

RevisionOS currently uses the `/Revola/` folder hierarchy on the iPod FAT32 volume so existing bootloaders and `.rvx` extensions remain drop-in compatible.

> **Use this RevisionOS firmware build (not upstream RevolaOS or RepriseOS):** Whenever you install RevisionOS extensions that rely on RevisionOS-specific core hook sites (such as `iap2.rvx`, `lyrics.rvx`, `playback-queue.rvx`, `hold-standby-clock.rvx`, and `silent-remote-volume.rvx`), you must assemble and install the updated `revola.bin` from this repo's `Firmware/` recipe. Upstream RevolaOS `revola.bin` builds do not include these hook sites yet, and RepriseOS `osos-cfw.bin` does not support `.rvx` core hook sites at all.

> **Do I update `/osos-cfw.bin` on the drive root?** Only if your iPod still uses the **RepriseOS NOR bootloader** (Path 2 below), because that bootloader reads `/osos-cfw.bin` from the root while the core reads `/Revola/Firmware/revola.bin`. If you installed the **RevolaOS NOR bootloader** (Path 1 or Path 3), it boots `/Revola/Firmware/revola.bin` directly and you do not need `/osos-cfw.bin` on the root.

---

## Path 1: Migrating from RevolaOS to RevisionOS (fastest)

If your iPod already boots RevolaOS:

1. Connect the iPod in USB disk mode and back up your existing `/Revola/Firmware/revola.bin` and `/Revola/Extensions/`.
2. Assemble `revola.bin` locally from `Firmware/firmware.json` and `Firmware/firmware.data` using `iPod_38.2.0.5.ipsw` (see [Firmware/README.md](../Firmware/README.md)).
3. Copy the newly assembled `revola.bin` over `/Revola/Firmware/revola.bin`. Your existing `loader.bin` and NOR bootloader stay unchanged.
4. Copy any `.rvx` extensions you want from `Extensions/` into `/Revola/Extensions/`.
5. Copy any games or apps from `Games/` and `Apps/` into `/Revola/Games/` and `/Revola/Apps/`.
6. Safely eject and reboot normally.

---

## Path 2: Migrating from RepriseOS without touching NOR flash

If your iPod already has the RepriseOS NOR bootloader installed (booting `/osos-cfw.bin` and `/cfw-loader.bin` from the drive root), it uses the same staging layout and entry ABI:

1. Connect the iPod in USB disk mode and back up `/osos-cfw.bin` and `/cfw-loader.bin` from the root of the drive.
2. Assemble `revola.bin` locally from `Firmware/firmware.json` and `Firmware/firmware.data` (see [Firmware/README.md](../Firmware/README.md)).
   - Keep your existing personalized `/cfw-loader.bin`, or assemble a fresh `loader.bin` from `Firmware/companion.json` and `Firmware/companion.data` using your device NOR backup.
3. Copy the assembled `revola.bin` to the root of the drive as `/osos-cfw.bin` AND into `/Revola/Firmware/revola.bin`.
4. Copy your loader (or assembled `loader.bin`) as `/cfw-loader.bin` AND `/Revola/Firmware/loader.bin`.
5. Create `/Revola/Extensions/`, `/Revola/Games/`, `/Revola/Apps/`, and `/Revola/Themes/`, then copy the `.rvx` extensions from `Extensions/` into `/Revola/Extensions/`.
6. Safely eject and reboot without holding any keys.

To roll back to RepriseOS, connect in disk mode and restore your backed-up `/osos-cfw.bin` and `/cfw-loader.bin`.

---

## Path 3: Fresh install from stock Apple firmware

1. Download the [RevolaOS desktop installer](https://github.com/RevolaOS/installer/releases) for Linux, macOS, or Windows.
2. Put the iPod into DFU mode (unlock Hold, then hold **Menu + Center** for about 12 seconds through the Apple logo until the screen goes completely black with backlight off) and install the base bootloader + RevolaOS environment using your `iPod_38.2.0.5.ipsw`. The installer automatically backs up your NOR flash first.
3. Once installed, reconnect in USB disk mode and follow **Path 1** above to replace `/Revola/Firmware/revola.bin` with RevisionOS and copy the extensions from `Extensions/` and games from `Games/` and `Apps/`.

