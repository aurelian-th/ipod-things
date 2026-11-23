# Extensions

Compiled `.rvx` modules for RevisionOS (API 1.74). Copy any extension file into `/Revola/Extensions/` on your iPod, safely eject, and toggle it on or off under **Settings > Extensions**. Disabled extensions are not loaded and use zero RAM.

Verify all extension checksums with `sha256sum -c SHA256SUMS.txt`.

## Bundled extensions

| File | Name | Type | Version | Author | Default | Description |
|---|---|---|---|---|---|---|
| `iap2.rvx` | Car Audio | Connectivity | 1.0.0 | 0xAu | On | Wired USB iAP1/iAP2 switch with 44.1 kHz digital audio, album artwork up to 512 KiB, library sync, and steering-wheel controls. Tested on a 2023 Škoda Kodiaq MIB3; should work on most cars with wired iAP2 media support, and a diagnostic build (`iap2-diagnostics`) is available if another car needs log capture |
| `lyrics.rvx` | Lyrics | Media | 0.2.0 | 0xAu | On | Synced and static `.lrc` lyrics viewer in Song and Now Playing context menus with Click Wheel scrub and snap-back |
| `playback-queue.rvx` | Playback Queue | Media | 0.1.0 | 0xAu | On | Adds Play Next, Play Last, and an editable Queue screen |
| `iap-auth-bypass.rvx` | iAP Auth Bypass | Connectivity | 0.1.0 | 0xAu | On | Allows iAP1 accessories to run auth-gated commands without an auth chip (leave off when using `iap2.rvx` in iAP2 mode) |
| `appearance.rvx` | Appearance | Customization | 0.1.0 | RevolaOS | Off | Dark Mode, Accent Color, Menu Covers, and `.rvt` themes from `/Revola/Themes/` |
| `channels.rvx` | Channels | Audio | 0.1.0 | RevolaOS | Off | Stereo width control (0% to 250%), mono, karaoke, and channel swap modes |
| `chapter-marks.rvx` | Chapter Marks | Media | 0.1.0 | RevolaOS | On | Hides chapter marks on the Now Playing bar when tracks have too many to distinguish |
| `chapter-menu.rvx` | Chapter Menu | Media | 0.1.0 | RevolaOS | On | Opens a browsable chapter list from podcast, iTunes U, or playing track context menus |
| `clicker-modes.rvx` | Clicker Modes | Audio | 0.1.0 | RevolaOS | On | Routes click wheel sound to speaker, headphones, both, or off |
| `compressor.rvx` | Compressor | Audio | 0.1.0 | RevolaOS | Off | Dynamic range compressor with an interactive editor |
| `cover-grid.rvx` | Cover Grid | Customization | 0.1.0 | RevolaOS | Off | Browses albums as a grid of cover art in Music and the Main Menu |
| `crossfeed.rvx` | Crossfeed | Audio | 0.1.0 | RevolaOS | Off | Blends left and right channels for more natural headphone listening |
| `equalizer.rvx` | Equalizer | Audio | 0.1.0 | RevolaOS | On | Distortion-free Apple EQ presets plus a 5-band Custom EQ editor |
| `eu-volume-limit.rvx` | EU Volume Limit | Audio | 0.1.0 | RevolaOS | On | Removes the EU volume cap and warning when the volume limit is off |
| `fast-seek.rvx` | Fast Seek | Media | 0.1.0 | RevolaOS | On | Jumps directly to seek targets in long AAC and ALAC files instead of stepping frame by frame |
| `file-info.rvx` | File Info | Media | 0.1.0 | RevolaOS | On | Shows tags, codec details, and file properties from a track context menu |
| `hold-standby-clock.rvx` | Hold Standby Clock | Customization | 1.0.0 | 0xAu | Off | Jumps straight to Apple's standby clock when Hold is locked while keeping the Backlight Timer; requires this custom RevisionOS `revola.bin` build |
| `homebrew.rvx` | Homebrew | Utility | 0.1.0 | RevolaOS | On | Lists and launches unsigned `.eapp` apps and games from `/Revola/Apps/` and `/Revola/Games/` under Extras |
| `long-audio-fix.rvx` | Long Audio Fix | Media | 0.1.0 | RevolaOS | On | Fixes playback stopping early on AAC and ALAC files longer than 13.5 hours |
| `progress-indicator.rvx` | Progress Indicator | Media | 0.1.0 | RevolaOS | On | Draws half-filled circles on partially played podcasts and iTunes U episodes |
| `silent-remote-volume.rvx` | Silent Remote Volume | Utility | 1.0.0 | 0xAu | Off | Stops wired headphone inline and dock remote volume presses from waking the screen or resetting the backlight timer; requires this custom RevisionOS `revola.bin` build |
| `usb-debug.rvx` | USB Debug | Developer | 0.1.0 | RevolaOS | Off | Developer tool for inspecting the iPod over USB while playing (blocks disk sync while enabled) |

See [the extensions guide](../docs/extensions.md) for installation, memory usage, and state troubleshooting.

