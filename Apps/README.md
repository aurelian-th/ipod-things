# Apps

Prebuilt `.eapp` utilities ready to copy onto an iPod Classic running RevisionOS or RevolaOS with FAT32 storage.

## Available apps

| Folder | Title | Author | Notes |
|---|---|---|---|
| `dice/` | Dice | Rockbox / RevolaOS | Rolls up to 12 polyhedral dice (d3, d4, d6, d8, d10, d12, d20, d100) rendered in OpenGL ES |

## Installation

1. Connect the iPod to your computer and open its storage root.
2. Copy the `dice/` folder into `/Revola/Apps/dice/`.
3. Safely eject and unplug the iPod.
4. Make sure `homebrew.rvx` is enabled under **Settings > Extensions**.
5. Open **Extras > Apps** and launch **Dice**.

## Controls (Dice)

- **Menus:** Click Wheel or **Previous / Next** to move, **Center** to select, **Menu** to go back or quit.
- **Rolling:** Press **Center** to roll again or stop settling dice early.

## Credits

- **Brandon Low** ([Rockbox](https://github.com/Rockbox/rockbox)): Original `dice` plugin (`GPL-2.0-or-later`).
- **Khronos Group** ([OpenGL-Registry](https://github.com/KhronosGroup/OpenGL-Registry), [EGL-Registry](https://github.com/KhronosGroup/EGL-Registry)): OpenGL ES 1.1 and KHR platform headers (`MIT` / `Apache-2.0`).
- **RevolaOS** ([`eapp-sdk`](https://github.com/RevolaOS/eapp-sdk), [`homebrew`](https://github.com/RevolaOS/homebrew)): `.eapp` runtime and software OpenGL ES support.

