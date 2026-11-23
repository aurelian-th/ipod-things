# Games

Prebuilt `.eapp` games ready to copy onto an iPod Classic running RevisionOS, RevolaOS, or RepriseOS with FAT32 storage.

## Available games

| Folder | Title | Version | Author | Notes |
|---|---|---|---|---|
| `doodlejump/` | Doodle Jump | 1.3.0 | 0xAu | Full-screen 320x240 port with sound effects, saved settings, and Classic / Full high scores |
| `2048/` | 2048 | 1.0 | Rockbox / RevolaOS | 4x4 sliding tile puzzle with touch-quadrant controls, one-move undo, and saved board state |

## Installation

1. Connect the iPod to your computer and open its storage root.
2. Copy the game folder (`doodlejump/` or `2048/`) into the games directory for your firmware:
   - **RevisionOS / RevolaOS:** `/Revola/Games/<game>/`
   - **RepriseOS:** `/iPod_Control/Games_RO/<game>/`
3. Safely eject and unplug the iPod.
4. On RevisionOS or RevolaOS, make sure `homebrew.rvx` is enabled under **Settings > Extensions**.
5. Open **Extras > Games** and select the game.

Copy the game folder directly so `*.eapp`, `Manifest.plist`, and the asset files sit inside `<game>/` rather than nested inside an extra folder. To update a game, replace its folder with the new copy.

## Controls

### Doodle Jump

- **Menus:** Click Wheel or **Previous / Next** to move, **Center** to select, **Menu** to go back.
- **In game:** **Previous** moves left, **Next** moves right, **Center** shoots, **Menu** or **Play/Pause** pauses.

### 2048

- **Move tiles:** Touch the top, right, bottom, or left edge of the Click Wheel without clicking.
- **Undo:** Press **Center** to take back the last move.
- **Pause:** Press **Menu** to resume, start a new game, view high scores, or quit.

## Credits

- **Lima Sky** and **Mr. Goodliving Ltd.**: Original creators of Doodle Jump. The Doodle Jump port included here is an unofficial hobby project and is not affiliated with or endorsed by Lima Sky.
- **Franklin Wei** and **Linus Nielsen Feltzing** ([Rockbox](https://github.com/Rockbox/rockbox)): Original 2048 plugin, tile artwork, and high score table (`GPL-2.0-or-later`).
- **RevolaOS** ([`eapp-sdk`](https://github.com/RevolaOS/eapp-sdk), [`homebrew`](https://github.com/RevolaOS/homebrew)) and **RepriseOS** ([`osos-lab`](https://github.com/lgreitis/osos-lab)): `.eapp` runtime, SDK, toolchain, and signature bypass.
