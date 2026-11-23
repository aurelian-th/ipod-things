# Lyrics (`lyrics.rvx`)

Adds a synced and static `.lrc` lyrics viewer to both the Song list and Now Playing context menus.

## File lookup order

Copy `.lrc` files to your iPod FAT32 volume using the track's **Artist** and **Title** tags. `lyrics.rvx` searches these four paths in order:

1. `/Lyrics/<Artist> - <Title>.lrc`
2. `/Lyrics/<Title>.lrc`
3. `/<Artist> - <Title>.lrc`
4. `/<Title>.lrc`

Reserved characters (`/ \ : * ? " < > |`) in Artist or Title tags are replaced with underscores (`_`), and each tag is capped at 63 bytes for filename matching.

## Supported `.lrc` features and limits

- `[mm:ss.xx]` and `[mm:ss.xxx]` timestamps (including multiple timestamps per line and `[offset:+-ms]` global shifts)
- Metadata header filtering (`[ar:]`, `[ti:]`, `[al:]`, `[by:]`, etc.)
- Plain unsynced `.lrc` files rendered as scrollable static text
- Light and Dark Mode color palettes when paired with `appearance.rvx`
- Maximum file buffer: `16 KiB` (up to `16,383` bytes read), up to `300` lines, and `95` characters per line

## Controls

| Action | Control |
|---|---|
| Open Lyrics | Long-press **Center** on a song or on the **Now Playing** screen and choose **Lyrics** |
| Scroll manually | Turn the **Click Wheel** to browse ahead or back; auto-scroll resumes 3 seconds after your last input |
| Snap back immediately | Press **Center** during manual scrolling to jump back to the active line |
| Go back | Press **Menu** without interrupting playback |

