# RevolaOS SDK

What extensions build against, for one revola release. Nothing in it reaches into the core.

| File | Contents |
|---|---|
| `rv_api.h` and the other `rv_*.h` | The `rv_api` table, the declaration helpers and the C library |
| `rv.mk`, `rvx.lds`, `rvx-pack` | The Make include, the module linker script and the packer |
| `osos-2.0.5.h`, `osos-2.0.5.ld`, `rv_firmware.h`, `rv_firmware.mk` | The symbols header and linker script of the Apple firmware and its id |
| `COPYING`, `NOTICE` | The licence |

The guides ship in `docs/` of the SDK tarball and live at [docs/sdk](https://github.com/RevolaOS/revola/tree/main/docs/sdk) in the repository, starting with [Getting started](https://github.com/RevolaOS/revola/blob/main/docs/sdk/README.md). Apps and games build against the [eApp SDK](https://github.com/RevolaOS/eapp-sdk).
