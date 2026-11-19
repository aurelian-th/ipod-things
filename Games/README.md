# Games

Prebuilt games packaged for the iPod Classic 2.0.5 `.eapp` runtime. Works on [RepriseOS](https://github.com/lgreitis/osos-lab) and [RevolaOS](https://github.com/RevolaOS/revola) (requires FAT32 storage).

## Available

- `doodlejump/` — Unofficial port of Doodle Jump (v1.3.0, full-screen 320x240) with sound effects, saved settings, and separate Classic / Full high score lists.

## How to install

1. Connect your iPod to your computer and open its storage root.
2. Copy the `doodlejump` folder into the right path for your firmware:
   - **RepriseOS:** `iPod_Control/Games_RO/doodlejump/`
   - **RevolaOS:** `Revola/Games/doodlejump/`
3. Safely eject your iPod.
4. On the iPod, open **Extras -> Games** and launch **Doodle Jump**.

Make sure `doodlejump.eapp`, `Manifest.plist`, and the asset files sit directly inside `doodlejump/` rather than nested inside another folder. To update a game, replace the existing folder with the new copy.

## Controls (Doodle Jump)

- **Menus:** Click Wheel or **Previous / Next** to move, **Center** to select, **Menu** to go back.
- **In game:** **Previous** moves left, **Next** moves right, **Center** shoots, **Menu** or **Play/Pause** pauses.

