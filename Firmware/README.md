# Firmware

Non-distributive patch recipes and data for **RevisionOS** (Apple RetailOS 2.0.5, core API 1.74, 172 hook sites).

Apple copyrighted firmware bytes are never distributed in this repository. You assemble the bootable firmware locally on your computer from your own copy of `iPod_38.2.0.5.ipsw`.

> **Why you need this RevisionOS `revola.bin` build:** RevisionOS adds custom hook sites in the core firmware (for features including Car Audio iAP2, synced Lyrics, Playback Queue, `hold-standby-clock.rvx`, and `silent-remote-volume.rvx`). `.rvx` modules bind to hook sites compiled into `revola.bin`, so upstream RevolaOS `revola.bin` builds lack those sites and RepriseOS `osos-cfw.bin` builds do not support `.rvx` hook sites at all. Assemble and install this build for now instead of upstream RevolaOS or RepriseOS binaries.

## Files in this folder

| File | Purpose |
|---|---|
| `firmware.json` | Non-distributive RevisionOS core patch recipe |
| `firmware.data` | Compiled RevisionOS core payload and patch bytes |
| `companion.json` | Non-distributive companion loader patch recipe |
| `companion.data` | Compiled companion loader bytes |
| `SHA256SUMS.txt` | SHA-256 verification checksums for all four files |

Verify checksums before assembling:

```sh
sha256sum -c SHA256SUMS.txt
```

## Assembling the firmware locally

1. Install or build the `revola` / `reprise` CLI from the [RevolaOS installer](https://github.com/RevolaOS/installer/releases) (or build from the source workspace).
2. Unpack your own copy of Apple's `iPod_38.2.0.5.ipsw` into an `inputs/` directory (`osos.bin`, `apple_loader.bin`, and EFI modules).
3. Assemble the RevisionOS core firmware (`revola.bin`) and, if installing fresh or updating the companion, your device-personalized `loader.bin` using your NOR dump:

```sh
revola bundle apply-recipe \
  --recipe firmware.json \
  --data firmware.data \
  --inputs /path/to/inputs \
  --out revola.bin

revola bundle apply-recipe \
  --recipe companion.json \
  --data companion.data \
  --inputs /path/to/inputs \
  --nor /path/to/nor.bin \
  --out loader.bin
```

Note on disk paths: RevisionOS currently keeps the `/Revola/` folder layout and `/Revola/Firmware/revola.bin` filename on the iPod so existing bootloaders and third-party `.rvx` extensions work as drop-in replacements.

For fresh installation, upgrading from RevolaOS, or switching from RepriseOS without modifying NOR flash, see [the installation guide](../docs/install.md).

