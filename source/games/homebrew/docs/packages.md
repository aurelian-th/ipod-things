# Packages

Every package builds against a checkout of the [eApp SDK](https://github.com/RevolaOS/eapp-sdk) at the commit in `eapp-sdk.lock`. The program runs on Apple's eApp host of firmware 2.0.5 and reaches it through the runtime's import list: no part of the core is involved while a package runs.

## One directory per package

`games/<id>/` or `apps/<id>/` holds a `Makefile` that sets `ID`, `NAME`, `SOURCES` and `ICON` and includes the SDK's `eapp.mk`, the sources, a 55 x 55 icon and a `README.md`. The directory name is the id, which the package's `eapp_application` repeats. The parent folder decides the kind: the top-level build passes `KIND=game` or `KIND=app`, and a package Makefile does not set it. A game goes to `/Revola/Games/<id>/` and is listed under Extras > Games; an app goes to `/Revola/Apps/<id>/` and under Extras > Apps.

A package's README says what it does for the person playing it, with the attribution line first when the code is ported, a table of the controls, and one closing table:

| | |
|---|---|
| Data | what it writes under `/Revola/Data/Games/<id>/` or `/Revola/Data/Apps/<id>/`, or none |
| Heap | the `HEAP_KIB` it asks the host for |
| Minimum eApp SDK | the eApp SDK version the package needs |

## Build

```sh
make SDK=/path/to/eapp-sdk                      # every package into dist/
make -C games/<id> SDK=/path/to/eapp-sdk KIND=game   # one package into games/<id>/build/
```

`dist/Games/<id>/` and `dist/Apps/<id>/` are the package folders, so `dist/` copies onto `/Revola/` as is; a RevolaOS release builds every package of the checkout it pins into its bundle, and the installer writes them there. `make check SDK=...` runs the host tests of every package that has a `tests/` directory. Requirements are those of the [eApp SDK](https://github.com/RevolaOS/eapp-sdk#build).

## Testing in the emulator

Put the package on a disk that boots the same core build and has the Homebrew extension on, then open it from Extras:

```sh
ipodvm disk --base <image> --copy dist/Games/2048:/Revola/Games/2048 --out test.img
ipodvm launch test.img
```

Saved data lands under `/Revola/Data/Games/<id>/` or `/Revola/Data/Apps/<id>/` on the disk and can be read back from the image after the VM quits.

## Licence

GPL-3.0-only, see the [README](../README.md). Ported Rockbox code keeps its GPL-2.0-or-later notice, its copyright lines and a modified line, names its authors in the README and is listed in NOTICE. A file of our own that follows Rockbox code names the source, carries the source's copyright line and says it is GPL-3.0-only. A built package links GPL-3.0-only code, so the package as a whole is GPL-3.0-only.
