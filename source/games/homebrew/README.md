# homebrew

First-party games and apps for RevolaOS. One directory per package under `games/` or `apps/`, built with the [eApp SDK](https://github.com/RevolaOS/eapp-sdk) at the commit in `eapp-sdk.lock`. A package is a folder on the iPod, `/Revola/Games/<id>/` or `/Revola/Apps/<id>/`, that the Homebrew extension lists under Extras and Apple's game runtime starts.

| Directory | Game | What it is |
|---|---|---|
| `games/2048` | 2048 | Rockbox's 2048 by Franklin Wei: slide tiles with the wheel, join equal numbers, reach 2048 |

| Directory | App | What it is |
|---|---|---|
| `apps/dice` | Dice | Rockbox's Dice by Brandon Low: roll up to twelve dice of 3 to 100 sides |

## Build

```sh
make SDK=/path/to/eapp-sdk
```

`dist/` then mirrors `/Revola/`: copy `dist/Games/<id>` to `/Revola/Games/<id>` or `dist/Apps/<id>` to `/Revola/Apps/<id>` on the iPod, or onto an emulator disk with `ipodvm disk --copy`. One package builds with `make -C games/<id> SDK=... KIND=game` or `make -C apps/<id> SDK=... KIND=app`. Details in [Packages](docs/packages.md).

## Documentation

| Page | Contents |
|---|---|
| [docs/](docs/README.md) | What a package is, how it is built and tested |
| [STYLE.md](STYLE.md) | Principles and rules for every file |

## Development

Large language models are used for much of the coding on RevolaOS. Their output is checked in the emulator, its memory use comes from the heap profile, and every release is tested on an iPod before it ships. Contributions written with the same tools are welcome and tested in the same way.

## License

GPL-3.0-only. See COPYING and NOTICE.
