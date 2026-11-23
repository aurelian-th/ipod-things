# Using extensions

Every extension is a single `.rvx` file placed in `/Revola/Extensions/`. Removing an extension is as simple as deleting its file, and any extension turned off in Settings uses zero RAM.

## Managing extensions on the iPod

Open **Settings > Extensions** to manage installed modules. **Usage** shows current core and extension heap usage, **Optimize** selects whether optional modules stay resident (`Performance`), unload when idle (`Memory`), or decide based on storage (`Auto`: Performance on hard drives, Memory on flash), and the extension list below shows each `.rvx` file with its state and memory footprint.

Select an extension to view its version, author, description, memory usage, and toggle **Enabled** between **On** and **Off**. Changes apply immediately.

## Installing or updating extensions

1. Connect the iPod to your computer in USB disk mode.
2. Copy `.rvx` files from `Extensions/` into `/Revola/Extensions/` on the iPod.
3. Safely eject and unplug. RevisionOS rescans `/Revola/Extensions/` when disk mode ends: new files register (starting Off unless bundled defaults apply), updated files reload, and deleted files unload.

If an extension triggers a crash, RevisionOS isolates it automatically on the next boot and shows **Off after crash**. See [Recovery](recovery.md) for details.

