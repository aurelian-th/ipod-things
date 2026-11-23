# Recovery and boot controls

RevisionOS keeps Apple's firmware intact on the iPod and includes automatic crash isolation for extensions.

## Boot keys

Start with the Hold switch unlocked. While the iPod boots, hold one of these buttons:

| Hold at startup | System started |
|---|---|
| Nothing | RevisionOS with enabled extensions |
| Previous | RevisionOS in core-only mode with all extensions disabled |
| Menu | Stock Apple 2.0.5 firmware |
| Play/Pause | Rockbox, if installed under `/.rockbox/` |

## Crash screen and automatic isolation

If an extension triggers an error, RevisionOS shows a crash screen naming the likely extension and the log path, then saves a report under `/Revola/Crashes/<report-id>.txt` on the next boot.

- Press **Center** once on the error screen to restart and write the crash report. Holding Menu and Center forces a hardware reset and can lose the pending report.
- On the next boot, RevisionOS leaves the attributed extension turned **Off after crash** in **Settings > Extensions**.
- You can re-enable it from **Settings > Extensions** to test again, or connect the iPod in disk mode and delete the `.rvx` file from `/Revola/Extensions/`.

## Troubleshooting

If the screen freezes or fails to respond, hold **Menu + Center** until the iPod resets, then hold **Previous** to start without extensions or **Menu** to start stock Apple firmware. If USB sync is blocked while playing, check whether `usb-debug.rvx` is enabled under **Settings > Extensions** and turn it off before syncing. Attach the matching file from `/Revola/Crashes/` if you open an issue for a crash.

