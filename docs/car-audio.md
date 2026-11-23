# Car Audio (`iap2.rvx`)

Car Audio adds native wired USB iAP2 car integration alongside a Settings toggle so you can switch between older iAP1 accessories and modern iAP2 head units on the fly.

## Compatibility

The extension implements standard wired USB iAP2 media playback, 44.1 kHz UAC1 digital audio, Now Playing metadata, embedded JPEG album artwork (up to 512 KiB), library/playlist browsing, and HID transport controls.

Because it follows standard Apple iAP2 over USB, it should work with most cars and head units that support wired iPhone or iPod USB media playback. CarPlay screen projection is a separate protocol and is not part of this extension.

- Verified end-to-end on a 2023 Škoda Kodiaq (VW Group MIB3) over a USB-C-to-30-pin cable with a 5,070-track library.
- Not yet tested on other car brands or head units. If your car fails to connect, refuses digital audio, or shows a USB accessory error, open an issue in this repository. I can share setup instructions and the diagnostic build (`iap2-diagnostics`) to capture connection logs and add support for your head unit.

## How to use

1. Copy `Extensions/iap2.rvx` into `/Revola/Extensions/` on your iPod.
2. Disconnect safely and open **Settings > Extensions > Car Audio**, then turn **Enabled** to **On**.
3. Open **Settings > Car Audio** (below Sound Check) to configure the mode:

| Option | Behavior |
|---|---|
| Protocol (`iAP2` / `iAP1`) | Choose **iAP2** for modern car stereos with USB digital audio and artwork, or **iAP1** for older 30-pin stereos, docks, and analog accessories |
| Status | Shows the active link state (`Ready (iAP2)`, `Classic (iAP1)`, `Connecting...`, `Connected (44k)`, or `Replug USB` after switching mid-connection) |
| About Modes | On-device summary explaining when to pick iAP1 or iAP2 |

Keep `iap-auth-bypass.rvx` turned off while using Car Audio in iAP2 mode, since Car Audio handles iAP2 session authentication itself.

