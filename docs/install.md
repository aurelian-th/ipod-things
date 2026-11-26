# Installing RevisionOS

RevisionOS runs on the iPod Classic 6G, 6.5G, and 7G with FAT32 storage and Apple firmware 2.0.5. Stock Apple firmware remains untouched on the iPod and can always be booted by holding **Menu** at startup.

## Requirements

- iPod Classic 6G, 6.5G, or 7G formatted as **FAT32** (Windows-formatted iPod; HFS+ is not supported)
- Direct 30-pin USB cable (avoid USB hubs during DFU flashing)
- Your own copy of Apple's `iPod_38.2.0.5.ipsw`
- A backup of your music library and iPod data

RevisionOS currently uses the `/Revola/` folder layout on the iPod FAT32 volume (`/Revola/Firmware/revola.bin`, `/Revola/Extensions/`, `/Revola/Games/`, `/Revola/Apps/`, `/Revola/Themes/`) so existing bootloaders and `.rvx` extensions remain drop-in compatible.

## Hardware limitations

The 2007 iPod Classic 6th Generation (80 GB and thick 160 GB) logic board physically lacks the Mikey accessory controller chip introduced on the 2008 6.5G (120 GB) and 2009 7G (thin 160 GB). That chip powers headset microphones and reads resistance and ultrasonic signals from wired inline headphone remotes.

Flashing Apple firmware 2.0.2 to 2.0.5 or RevisionOS onto a 6G will not enable inline headphone controls or microphone input, and replacing the 3.5 mm headphone jack cannot add them because the hardware is missing from the motherboard (sources: Olsro iPod Modding Guide on GitHub and Retro Refresh).

---

## Option 1: Fresh install with RevisionOS Installer (recommended)

1. Download **RevisionOS Installer** for macOS (Universal), Windows, or Linux from [GitHub Releases](https://github.com/aurelian-th/ipod-things/releases).
2. Open the installer and choose your `iPod_38.2.0.5.ipsw` file.
3. Select the extensions, games, and apps you want to bundle (or start with the recommended preset).
4. Put your iPod into **DFU mode**: unlock the Hold switch, then hold **Menu + Select (Center)** for about 12 seconds through the Apple logo until the screen goes completely black with the backlight off.
5. Follow the on-screen DFU steps. The installer automatically backs up your NOR flash first, assembles `revola.bin` and `loader.bin` locally from your IPSW, and installs RevisionOS onto your iPod.
6. Safely eject and reboot normally.

---

## Option 2: Migrating from RepriseOS or RevolaOS

1. Connect your iPod in **USB Disk Mode** and open **RevisionOS Installer**.
2. Open **Migrate**. The installer automatically detects:
   - RepriseOS (`/osos-cfw.bin`, `/iPod_Control/Games_RO/`)
   - Legacy root RevolaOS (`/revola.bin`, `/loader.bin`)
   - Existing `/Revola/` installations
3. Choose what to migrate (copy RepriseOS homebrew games into `/Revola/Games/`, update extensions, write RevisionOS `revola.bin`, and clean up legacy root files), then run the migration or import your selection into the DFU installer.

---

## Option 3: Updating extensions, games, and firmware later

- **Using the Store:** Connect your iPod in USB Disk Mode, open **RevisionOS Installer -> Store**, and install, update, or remove extensions, games, apps, or `revola.bin` in place.
- **Manual copy:** Copy `.rvx` files from `Extensions/` into `/Revola/Extensions/`, `.eapp` folders from `Games/` and `Apps/` into `/Revola/Games/` and `/Revola/Apps/`, and `.lrc` lyrics files into `/Lyrics/` on the drive root, then safely eject.

## Advanced: Manual CLI assembly

If you prefer building and assembling `revola.bin` or `loader.bin` from the command line without the desktop installer, see [Firmware/README.md](../Firmware/README.md) and [source/README.md](../source/README.md).
