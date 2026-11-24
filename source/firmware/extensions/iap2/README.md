# Car Audio (iAP1 / iAP2 Switch)

Native USB car audio and iAP1/iAP2 protocol switcher for Apple RetailOS 2.0.5, built as a RevolaOS extension (`/Revola/Extensions/iap2.rvx`).

## Features

- **iAP2 Mode (Newer Cars, default):** Implements native DWC2 EP3 IN transmit, iAP2 link-layer session management, MFi authentication/identification, USB digital audio (`44.1 kHz` UAC1), Now Playing metadata & live progress/scrubbing, embedded JPEG cover artwork transfer (`<= 512 KiB`), full media library/playlist/queue sync, and steering-wheel / head-unit HID controls. Verified end-to-end on a 2023 Škoda Kodiaq MIB3.
- **iAP1 Mode (Older Cars & Docks):** Cleanly passes all USB HID and control traffic straight through to stock Apple RetailOS for older 30-pin stereos and speaker docks.

## Settings

Adds **Car Audio** under **Settings** (after Sound Check) with an Apple-style car icon preview:

- **Protocol (`iAP1` / `iAP2`):** Switches between classic iAP1 and modern iAP2.
- **Status:** Shows live connection/audio rate (`Ready (iAP2)`, `Classic (iAP1)`, `Connecting...`, `Connected (44k)`, or `Replug USB` when switched mid-connection).
- **About Modes:** Explains older vs. newer car compatibility.

## Requirements and build

Requires RevolaOS core and SDK API `1.74` or newer.

```sh
make SDK=/path/to/revola-sdk
make check SDK=/path/to/revola-sdk
```

Build uses Rockbox ARM GCC 9.5.0 (`CROSS_COMPILE=...`) and enforces Thumb-only module code (`tests/check_veneers.py`) so no unrelocated ARM/Thumb veneers are emitted.
