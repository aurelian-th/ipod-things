# eapp-sdk

The SDK for apps and games on Apple's eApp host, the runtime in the iPod Classic 2.0.5 firmware that runs the iPod games from iTunes. An app links the SDK's runtime, which reaches the host's services through the image's import list the way Apple's games do, by module name. Nothing in a package names a RevolaOS path or function: it runs under RevolaOS, where the Homebrew extension lists it under Extras, and from `iPod_Control/games_RO/<id>/` on any 2.0.5 firmware that skips the game signature check.

## Layout

| Path | Contents |
|---|---|
| the root | The headers an app includes (`eapp.h` and the `eapp_*.h` beside it), `eapp.mk`, and `eapp-pack` with its module `eapp_package.py` |
| `runtime/` | The runtime linked into every image: startup, import list, host calls, drawing |
| `include/` | The Khronos OpenGL ES 1.1 headers |
| `firmware/` | The symbols header copied from the reversing repository: host module ids, types and image limits |
| `examples/` | Example apps |
| `tests/` | Host checks of the runtime against stand-ins for the host |
| `docs/` | How to write and package an app |

## Build

Requires GNU make, Python 3, Rockbox's `arm-elf-eabi-` GCC 9.5.0, and for the host checks a C compiler with AddressSanitizer (`HOST_CC`, `cc` by default). The checkout is the SDK: an app's Makefile includes `$(SDK)/eapp.mk`.

```sh
make -C examples/hello CROSS_COMPILE=/path/to/arm-elf-eabi-
python3 tests/run.py
```

## Versions

`EAPP_SDK_MAJOR` and `EAPP_SDK_MINOR` in `eapp.h` are the SDK version. `api.lock` records it with the hash of every file an app builds against, and `python3 tools/sdklock.py check` (run by the `sdk` workflow) fails when one of them changes at the same version: bump the minor for additions, the major when a call or a layout changes meaning, then run `python3 tools/sdklock.py update`. A release is the tag `v<version>` and the tarball `git archive --prefix=eapp-sdk-<version>/ -o eapp-sdk-<version>.tar.gz v<version>`, which leaves out the workflows, hooks and agent files.

## Documentation

| Page | Contents |
|---|---|
| [docs/](docs/README.md) | Writing, building and packaging an app |
| [STYLE.md](STYLE.md) | Principles and rules for every file |

## Development

Large language models are used for much of the coding on RevolaOS. Their output is checked in the emulator, its memory use comes from the heap profile, and every release is tested on an iPod before it ships. Contributions written with the same tools are welcome and tested in the same way.

## License

GPL-3.0-only. See COPYING and NOTICE.
