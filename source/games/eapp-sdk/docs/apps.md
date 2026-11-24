# Apps and games

An app is a flat image Apple's eApp host loads into its own 1 MiB mapping and drives with a frame loop, the same host that runs the iPod games from iTunes. The SDK holds the runtime an app links, `eapp.h`, `eapp.mk` and `eapp-pack`. A package is a folder with the image, a `Manifest.plist` and a 55 x 55 icon; the Homebrew extension lists it under Extras > Games from `/Revola/Games/<id>/` or under Extras > Apps from `/Revola/Apps/<id>/` and lets the host load it without an Apple signature. The runtime is linked into the image and imports the host's services the way Apple's games do, by module name through the image's import list; an extension is not involved while the app runs, and nothing in the image names a RevolaOS path or function. The same package runs from `iPod_Control/games_RO/<id>/` on a firmware that only skips the signature check.

Adapted from the [RepriseOS game SDK](https://github.com/lgreitis/osos-lab/tree/main/game-sdk) by lgreitis.

## An app

`main.c` defines `eapp_application`: the package id, the folder name under the root, and up to five callbacks: `init`, `start`, `input`, `frame` and `shutdown`. Every callback runs on the host's task; `frame` draws into the RGB565 buffer it is handed and returns. `start` runs once before the first frame, ahead of its input events, when the host's OpenGL ES context is current, below.

```c
#include "eapp.h"
#include "eapp_ui.h"

static void input(const struct eapp_input_event *event)
{
    if (event->button == EAPP_BUTTON_MENU && event->type == EAPP_INPUT_PRESS)
        eapp_request_exit();
}

static void frame(const struct eapp_frame *frame)
{
    eapp_clear(&frame->framebuffer, 0);
    eapp_draw_text(&frame->framebuffer, 20, 20, "HELLO", 0xffff, 1);
}

const struct eapp eapp_application = {
    .package_id = "hello", .input = input, .frame = frame,
};
```

`Makefile` names the package and includes `eapp.mk`:

```make
ID := hello
NAME := Hello
KIND := game          # game for /Revola/Games/, app for /Revola/Apps/
SOURCES := main.c
ICON := icon.png
include $(SDK)/eapp.mk
```

```sh
make SDK=/path/to/eapp-sdk
```

`build/hello/` is the package. Copy it to `/Revola/Games/hello/` on the iPod (or `/Revola/Apps/hello/` for `KIND := app`), or onto an emulator disk with `ipodvm disk --copy`. The runtime asks the host where the package is, so the folder decides: `KIND` only names the root `eapp-pack --zip` lays the folder out under. Ids share one namespace with extensions.

| Variable | |
|---|---|
| `ID`, `NAME` | the folder name and what Extras lists |
| `KIND` | `game` or `app` |
| `SOURCES` | C sources, built freestanding without a C library |
| `ICON` | a 55 x 55 8-bit RGB or RGBA PNG, `eapp-pack` converts it |
| `HEAP_KIB` | the heap the host gives the app, 5120 by default |
| `FILES` | data files copied into the package, read by their names |
| `NAMES` | a TOML file with the name per language, `de_DE = "Hallo"`, keyed by the locales of Settings > Language (listed under `eapp_language` below); the host lists the name of the iPod's language and `NAME` for the others (needs Python 3.11) |

`eapp-pack` lists the package for the classic (PlatformID 3) and the nano 3G (2) with the same image; the nano is untested.

## What the runtime provides

| Area | Calls |
|---|---|
| Drawing | `frame->framebuffer`: 320 x 240 RGB565 pixels, borrowed for one `frame` call and shown when it returns; `eapp_draw.h` draws on it, below |
| OpenGL ES | `eapp_gl.h`: the host's OpenGL ES 1.1 through the Khronos `<GLES/gl.h>`, drawing into the same pixels; set up in `start`, below |
| Buttons | `input` receives Menu, Center, Next, Previous and Play presses and releases with a millisecond timestamp. After the callback the runtime hands Play, Next and Previous releases to the iPod's music control, as Apple's games do, so music keeps working while an app runs; an app that uses those buttons sets `handles_music_buttons` in `eapp_application` and passes on what it does not use with `eapp_music_pass_button` |
| Wheel | `eapp_read_wheel` (clockwise delta since the last read, position, touched), `eapp_wheel_directions` (eight sectors as Up, Right, Down, Left bits) |
| Hold | `eapp_hold_locked` |
| Heap | `eapp_alloc`, `eapp_realloc`, `eapp_free` from the host's heap, at most 8 MiB per block, all freed when the app ends |
| Time | `eapp_ticks_ms` since the first call; `eapp_sleep_ms` blocks the app's task in the host for at least that long, 0 yields |
| Clock | `eapp_date_time` fills a `struct eapp_date_time` (year, month, day, hour, minute, second, local time), `eapp_clock_24h` reads the clock setting, `eapp_format_time` writes "9:05 PM" or "21:05" after it, below |
| Backlight | `eapp_set_backlight_level` (0..100) and `eapp_set_backlight_dimmed`; a level outlives the app as the user's Brightness setting, below |
| Device | `eapp_platform` (iPod video, nano 3G or classic), `eapp_external_power` (a computer or a charger on the cable; the emulator's USB mode carries no charger classification, so only its adapter mode counts there), `eapp_language` (a locale code such as `de_DE`), `eapp_name` (the package's name as Extras lists it, in the current language when the manifest has it) |
| Log | `eapp_log` writes a line to the host's debug console: the emulator's `qemu.log` in the run folder; an iPod without a debugger shows nothing |
| Random | `eapp_random` (xorshift32, 32 bits per call), `eapp_random_seed`; the stream starts the same every run until seeded, say from the first input's timestamp |
| Files | `eapp_file_open`, `read`, `seek`, `size`, `position`, `close`: read only, paths inside the package, twelve open at once with the data files |
| Data | `eapp_data_open` opens a path inside the package's data folder for reading or writing, `eapp_file_write` on a handle opened for writing, `eapp_data_delete`; writing creates the folder and replaces the file, and the bytes are on disk when the handle is closed. `eapp_data_read`, `eapp_data_write` and `eapp_file_read_all` (a package file) read or replace a whole file with no handle: a read fills what fits and returns the file's whole size. The host names the folder: `/Revola/Data/Games/<id>/` or `/Revola/Data/Apps/<id>/` under RevolaOS, where Homebrew redirects it, and Apple's `iPod_Control/gamedata_RW/<id>/` anywhere else |
| Saves | `eapp_save_open`, `read`, `write`, `size`, `close`: Apple's save store for an app with little to keep, a file of the data folder whose contents the host holds in memory and writes at the next system save or app launch, not when the app exits. Four slots exist on the device, 100 KiB together; `write` replaces the contents. Anything larger, or that must be on disk when the app exits, is a data file |
| Folders | `eapp_directory_open` (recursive or not), `read`, `close`: four open at once; entry names are relative to the folder opened, subfolders are walked depth first, a folder lists at most 500 entries |
| Errors | `eapp_file_error`: the host's status of the last file, data, folder or save call that failed, as the `FileStatus_*` and `GameSaveStatus_*` values of the symbols header |
| Menus | `eapp_ui.h`: `eapp_menu_input`, `eapp_menu_scroll`, `eapp_menu_draw` draw a text list; `eapp_file_picker_open` lists up to 64 files with an extension, sorted |
| Sound | `eapp_sound_load` (a PCM WAV of the package) or `eapp_sound_create` (a buffer of the app's), `play`, `stop`, `pause`, `resume`, `playing`, `position`, `free`, and a setter per property of the host's sound: volume, pan, speed, play count, lead-in, tail, next, music duck. Thirty-two sounds at once on eight mixer voices; the format is below |
| Music | `eapp_music_*`: the iPod's player, below: play_pause, stop, next, previous, play_track, seek, scanning, state, position, duration, track index and count, repeat, shuffle, volume, EQ, speed and gain |
| Library | `eapp_media.h`: the iPod's songs for a selection (artists, albums, genres, composers, TV shows, seasons, tracks, playlists), `eapp_media_set_order` for the tracks list, tracks and playlists as objects with their fields, rating, bookmark, play and skip count writes, and `eapp_music_clear_list`, `eapp_music_append`, `append_file`, `remove`, `keep_list_on_exit` and `eapp_music_track_open` for a playback list of the app's own, below |
| Pictures | `eapp_media_artwork_find`, `album_artwork_open`, the photo library's albums and images, loaded as RGB565 bitmaps for `eapp_draw_bitmap`, below |
| Players | `eapp_media.h`, `eapp_user_*`: the host's one player list for every game, by id: count, id, name, add, rename, remove, below |
| Exit | `eapp_request_exit` ends the app after the current callback |

## Drawing

`eapp_draw.h` works on the framebuffer directly. Colours are RGB565. Every call clips to the framebuffer and takes coordinates outside it.

| Call | Draws |
|---|---|
| `eapp_clear(fb, color)` | the whole framebuffer |
| `eapp_fill_rect(fb, x, y, width, height, color)`, `eapp_draw_rect(...)` | a filled rectangle, or its 1 px outline just inside |
| `eapp_draw_pixel(fb, x, y, color)` | one pixel |
| `eapp_draw_line(fb, x0, y0, x1, y1, color)` | a 1 px line, both ends included |
| `eapp_fill_triangle(fb, x0, y0, x1, y1, x2, y2, color)` | a filled triangle |
| `eapp_blend_triangle(fb, x, y, alpha, color)` | `color` blended over a triangle with corners `x[i]`, `y[i]` in 16.16 pixels, the alpha going linearly from `alpha[i]` (0 to 255) at each corner. Pixels whose centres lie inside are drawn, so triangles that share an edge, such as a fan, blend each pixel once |
| `eapp_draw_circle(fb, cx, cy, radius, color)`, `eapp_fill_circle(...)` | a 1 px circle, or a filled one |
| `eapp_draw_round_rect(fb, x, y, width, height, radius, color)`, `eapp_fill_round_rect(...)` | a rectangle with quarter-circle corners, outlined or filled |
| `eapp_draw_bitmap(fb, x, y, &bitmap)` | a `struct eapp_bitmap`: RGB565 pixels with a width, height and stride, and a `transparent` colour that is skipped (-1 for none) |
| `eapp_draw_bitmap_part(fb, x, y, &bitmap, source_x, source_y, width, height)` | a part of the bitmap, for a sprite sheet |
| `eapp_draw_mono_bitmap(fb, x, y, bits, width, height, color)` | set bits in `color`: one bit per pixel, most significant first, rows padded to a byte |
| `eapp_draw_text(fb, x, y, text, color, scale)` | text in the SDK's 5 x 7 font, every font pixel `scale` pixels: digits, upper-case letters, `-`, `+`, `:`; lower case drawn as upper, other characters leave their cell empty |
| `eapp_draw_text_in(fb, x, y, width, text, color, scale, align)` | the same text at the left, centre or right of the `width` pixels from `x` (`EAPP_ALIGN_LEFT`, `CENTER`, `RIGHT`) |
| `eapp_text_width(text, scale)` | the pixels a text spans: glyph cells 6 font pixels apart, no gap after the last |

## OpenGL ES

The host carries a software OpenGL ES 1.1 renderer (`glGetString` answers `OpenGL ES-CM 1.1`, renderer `Software`, with the `GL_OES_fixed_point`, `single_precision`, `read_format`, `query_matrix`, `point_size_array`, `point_sprite` and `compressed_paletted_texture` extensions) and an app calls it through the Khronos header: `#include "eapp_gl.h"` brings in `<GLES/gl.h>` from `include/` and declares the host's EGL entries. The host owns the EGL side: after `init` and before the first frame it creates the context and a 320 x 240 RGB565 window surface with a 16-bit depth buffer and makes them current, and it tears them down after the last frame. So GL calls in `init` do nothing and set no error; one-time setup (viewport, projection, enables, textures) goes into `start`. The buffer `frame` receives is the surface's colour buffer: gl* calls and `eapp_draw.h` write the same pixels in a frame, text over a GL scene is one `eapp_draw_text` after the draw calls, and the runtime swaps the buffers when `frame` returns. The surface is double buffered, so every frame redraws everything. An app creates no surface and swaps nothing; `eglQuerySurface`, `eglGetError` and the other EGL entries are there for queries.

The host's table lacks a few `gl.h` entries; the runtime supplies `glColor4ub`, `glTexEnvi`, `glTexEnviv`, `glTexParameterfv`, `glTexParameteriv` and `glTexParameterxv` on the host's float and scalar forms, so GL ES 1.1 code links unchanged, and only `glPointSizePointerOES` is an unresolved symbol. `glCompressedTexSubImage2D`, `glGetTexParameterfv` and `glGetTexParameterxv` exist but only raise a GL error. Floats are fine: the SDK's soft-float ABI passes them the way the host expects. The cube example draws a turning cube with the depth test and shows what `glGetString` and `GL_DEPTH_BITS` report. The headers in `include/` are the Khronos registries' files under their MIT and Apache-2.0 licences, listed in `include/README.md`.

## Sound samples

The host mixes a sound's samples over the music on one of eight voices and resamples to the output rate. It takes 8-bit signed or 16-bit little-endian PCM, 1 or 2 channels, 8000..48000 Hz, and nothing else: `eapp_sound_create` refuses other formats, because the host would play them with a stale format instead of failing. `eapp_sound_load` reads a WAV from the package: RIFF/WAVE with a plain PCM format chunk (format 1, not float or extensible) and a data chunk; 8-bit WAVs are unsigned on disk and converted after loading. One `sox` line makes such a file from anything: `sox in.wav -r 22050 -c 1 -b 16 out.wav`. Ship WAVs through `FILES`; the hello example generates its `beep.wav` with `tone.py` at build time.

The host reads the sample memory while a voice plays, so a buffer given to `create` stays the app's until the sound is freed, and until any sound naming it as `next` is freed; `load` keeps the file block itself. `play` starts from the beginning and stops a run still playing; the properties take the host's values as they are (volume 0..`EAPP_SOUND_VOLUME_MAX`, pan -`EAPP_SOUND_PAN_MAX`..`EAPP_SOUND_PAN_MAX`, speed 500..2000 per mille, play count 0 for ever), with the host's defaults. A sound set as another's `next` plays after it and its tail; the host cannot unchain, so freeing such a sound leaves it with the host, still playing in its turn, until nothing names it. The music duck fades the music to a level over a silence inserted before the sound and restores it after. Shutdown stops and frees whatever is left; the host also stops every voice when an app ends.

## Music

`eapp_music_*` drives the music the iPod is playing, through the host. A command (`play_pause`, `stop`, `next`, `previous`, `play_track`, `seek`, `scan_forward`, `scan_backward`, `scan_stop`, and the setters other than `set_volume`, which takes effect at once) is queued for the host's UI task and applied after the frame returns. The host predicts what it can: `eapp_music_state` (stopped, playing, paused), `track_count` and the settings (`repeat_mode`, `shuffle_mode`, `eq`, `speed`, `gain`) read back the new value at once; `position`, `track_index` and `scanning` report the old values until the UI task has run. A player app that seeks on wheel input therefore draws the old position for a frame; compare against what was asked rather than waiting on a flag. Transport commands and the repeat, shuffle, EQ and speed setters do nothing while no playlist is loaded (`has_playlist` is 0), which is also the state after `stop`: it ends playback and drops the playlist, so pause with `play_pause` when the music should come back. `play_track` takes an index from 0 and drops one past the end.

The host saves the player's settings when the app starts and restores repeat, shuffle, speed, gain and an EQ set with `set_eq_until_exit` when it ends. `set_eq` stays on the player after the app ends, while the Settings > EQ list keeps showing the user's own choice, and `set_volume` is not restored: an app that changes the volume for itself puts it back in `shutdown`. Volume is 0..`EAPP_MUSIC_VOLUME_MAX` under the user's volume limit (`volume_limit`; `set_volume` clamps to it), speed `EAPP_MUSIC_SPEED_MIN`..`MAX` per mille for audiobooks (out of range is ignored), gain -`EAPP_MUSIC_GAIN_MAX`..`EAPP_MUSIC_GAIN_MAX` in the host's unit. After a `stop` the host reads repeat and shuffle as off until they are set again.

Play, Next and Previous reach the player through the runtime unless the app handles them (`handles_music_buttons`), and through `eapp_music_pass_button` for an app that keeps some: the host acts on releases of those three buttons and ignores everything else. The hello example shows the state, position and track under its greeting.

## Music library

`eapp_media_*` reads the library through the host, songs only: videos, podcasts, audiobooks and iTunes U are left out. The library is a selection: it starts as the whole library when the app first asks, `eapp_media_select` narrows it by an entry of a list (an artist, an album, a playlist...), selections add up, and `eapp_media_reset` goes back to everything. An index names an entry only until the next select, reset, order change or playlist edit; going back a level is a reset and the selections again. `eapp_media_count` builds a list for the current selection, and the first count of a list after a selection walks every track of it, which takes time on a large library; later counts and reads of that list are quick until the selection or the order changes, so an app counts once and reads the rows it shows with `eapp_media_name`. The Music menus build their own lists when they open, so a selection the app leaves does not reach them. `eapp_media_set_order` sorts the tracks list by title, artist, album or genre, or keeps the order of the selection's list (`EAPP_MEDIA_ORDER_LIST`), which after a playlist is selected is the playlist's own order for a playlist that is not shuffled. A reset keeps the order, and Music > Songs and the Music playlists keep their own; an app that needs an order sets it before reading the list.

Tracks and playlists are objects: `eapp_media_track_open` (an index of the current selection), `track_find` (the iTunes persistent id `eapp_media_track_id` answers), `playlist_open` and `playlist_find`, each closed again with `_close`. They are the host's, kept in the iPod's own memory, not in the app's heap: close each one when it is no longer shown and never hold one per track of a large library. The host frees what is left when the app ends. Text fields are copied as UTF-8 into the app's buffer and cut before a character that does not fit; the call returns the whole length, so a result of `capacity` or more means the text was cut. `EAPP_MEDIA_TEXT_SIZE` fits every track field but a podcast URL. Ratings are 0..`EAPP_MEDIA_RATING_MAX`. `set_rating`, `set_bookmark_ms`, `count_play` and `count_skip` change the library's record of the track; `playlist_remove_track` takes an entry out of a playlist.

An app builds its own playback list: `eapp_music_clear_list` empties it, `eapp_music_append` puts a track at its end (the track can be closed right after, so a long list never holds many objects), `eapp_music_append_file` a file of the app's package with the length it gives, `eapp_music_remove` takes an entry out, and `eapp_music_play_track` starts one; the other `eapp_music_*` calls control it from then on. The host queues every entry by its file and not as a library entry: the audio plays, but the player knows nothing else about it, so Now Playing shows no title, artist or artwork, play and skip counts stay as they are unless the app counts them, and `eapp_music_track_open` gives such an entry back without them, from a later frame once the host has queued it. A file that does not exist is left out and the entries after it move up. A player app therefore draws its own now-playing screen from what it read. The host empties the list when the app ends and Play brings back the user's own list, unless `eapp_music_keep_list_on_exit` came after the last clear, append or remove: then the app's list stays as the player's list.

The library example lists artists and playlists, their tracks with the highlighted track's cover, plays from a track on, steps through the photos and edits the player list.

## Pictures

An image is an entry of the artwork or the photo database: a track's or an album's cover, or a photo by album and index or by its id. `eapp_media_image_load` reads the largest thumbnail the database holds within a size, or the smallest when none fits, from the disk at once, on the app's task, and `eapp_media_picture_bitmap` hands it out as a `struct eapp_bitmap`. The pixels stay in the iPod's memory until `eapp_media_picture_free`, about width × height × 2 bytes (a 200 × 200 cover holds 80 KB), so an app keeps the pictures it shows and frees the rest. Only plain RGB565 thumbnails are loaded.

## Players

`eapp_user_*` is the host's one player list for every game: what an app adds, renames or removes, every game sees. The host writes the list to disk when the iPod enters or leaves disk mode and at a few other host events; until then a restart can lose a change. A player is an id, which stays with the player, and a name of fewer than `EAPP_USER_NAME_SIZE` bytes; indexes move when a player is removed. Scores and anything else per player go into the app's own data files, keyed by the id.

## Clock and device

The clock is the iPod's, in local time; while the user never set it, the host reports a fixed default date. `eapp_format_time` reads the 12/24-hour setting on every call and writes at most `EAPP_TIME_TEXT_SIZE` bytes: no leading zero and ` AM`/` PM` in 12-hour mode, two digits in 24-hour mode, all in characters the SDK font has. There is no date formatter: the date-order setting is not one the host hands to apps, so an app that shows a date picks the order from the fields.

`eapp_set_backlight_level` sets the iPod's brightness, and the level stays when the app ends: the host writes it into the user's Brightness setting, and its level query reports only what the app set, never the user's value, so an app cannot put it back. Set it only when the user asked for it, as a brightness control would. Dimming caps the level at 20 and is undone when the app ends. The backlight timer of Settings keeps running while an app runs, so the screen still goes dark without input after the user's period; nothing in the SDK turns the backlight off or keeps it lit.

`eapp_language` maps the host's language index to the locale code of the Settings > Language list: en_US, da_DK, de_CH, de_DE, es_ES, fi_FI, fr_BE, fr_CH, fr_FR, it_IT, ja_JP, ko_KR, nl_BE, nl_NL, no_NO, pt_PT, sv_SE, zh_CN, zh_TW, cs_CZ, el_GR, hu_HU, pl_PL, ru_RU, tr_TR. `eapp_platform` is the games platform the host reports, the same value as `GamesPlatformID` in the iPod's SysInfoExtended; the emulator reports the classic. The hello example shows all of these, measures one `eapp_sleep_ms(500)` at start, changes the backlight with the wheel and dims while Center is held.

## With a C library

`eapp_libc.h` declares `memcpy`, `memmove`, `memset` and `memcmp`, which the runtime supplies under their standard names: GCC emits calls to them for struct copies and initialisers. `memcpy` moves whole words once both ends share a word boundary, `memset` once its destination reaches one. An app that needs newlib compiles `runtime/newlib_glue.c` with its own sources and supplies `eapp_libc_write` and `eapp_libc_exit` from `eapp_libc.h`. The glue maps `malloc` to the heap, `open` (read only) to the package's files, `write` on the standard streams to `eapp_libc_write` and `exit` to `eapp_libc_exit`, which must unwind to the current callback: the host owns the task and its stack. Such an app writes its own link rule with `EAPP_ARCH_FLAGS`, `EAPP_CFLAGS`, `EAPP_LDFLAGS`, `$(EAPP_STARTUP)` and `$(EAPP_RUNTIME)`, links `-lgcc` and its libraries, runs `objcopy -O binary` and then `eapp-pack` with `--symbols` naming the SDK's `firmware/osos-2.0.5.h`, where the image checks come from.

## The image and the package

`runtime/eapp.lds` links the image at the host's mapping, 1 MiB for code, data and bss, with the eApp header (`runtime/header.c`) first. `runtime/imports.def` lists the host's modules and their functions in the host's order; `runtime/imports.S` turns it into the import list the header points at, one jump stub per function that the host fills in when it loads the image. Every host call goes through a stub, so the image holds no firmware address; an unresolved symbol at link time is a call that bypasses the import list.

Files and folders are requests to the host's file worker, which runs on its own task and hands finished requests back with a later frame. The runtime keeps the calls synchronous: it waits for a request's outputs, yielding to the worker in between. The requests are a small ring that is never freed, because the worker links a finished request to the next one after the fact (`runtime/request.c`). `eapp-pack` checks the header, the entries and the import list, converts the icon, writes `Manifest.plist` and the package folder; `--zip` adds a ZIP with the folder under `Revola/Games/<id>/` or `Revola/Apps/<id>/` for sharing. Its module, `eapp_package.py`, serves an app with a packaging script of its own.

`python3 tests/run.py` checks the package path rules, the request lifecycle, the file, folder, save store, sound, music, clock, settings and device calls, the runtime's button forwarding, its start callback, the GL forwarders, the player list, the library and the pictures against stand-ins for the host, the drawing calls on a small framebuffer, and the memory calls at every alignment.
