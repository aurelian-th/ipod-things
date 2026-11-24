#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build, sign and publish a firmware release: the bundle the installer fetches,
plus the SDK tarball.

Nothing in the release contains Apple bytes, and the recipes come out the same with
or without the decrypted Apple image; the image only lets the build check the site
table against it (expected words, no branch into an entry site's second word). A
release built without the image therefore needs a checked build on a developer
machine first. The script refuses a dirty tree or a missing tag, builds the recipes,
the Rockbox bootloader images, the SDK, every extension of a pinned extensions
checkout, every first-party theme of a pinned themes checkout, every game and app of
a pinned homebrew checkout against the eApp SDK commit it locks and the USB helper of
a pinned installer checkout, writes the bundle specification, exports, signs and
packs the bundle with the installer's CLI, and with --upload publishes everything as
a GitHub release of this repository.

Beside the bundle the release carries the licence, NOTICE, the MIT text of the
bundled glyphs, and the source of everything it contains: one archive of each
pinned checkout, of the themes revision the Appearance extension's Classic theme is
built from, of the eApp SDK revision the games and apps link, and of each Rockbox
revision the bundle's binaries come from.
"""

import argparse
import contextlib
import getpass
import hashlib
import json
import os
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

import sdklock
import setup
from toolchain import run
from version import identity

import build
import firmware

ROOT = build.ROOT
REPOSITORY = "RevolaOS/revola"
TARGET = "classic"


def git(directory, *args):
    return subprocess.check_output(
        ["git", "-C", str(directory), *args], text=True
    ).strip()


def pinned(directory, name):
    """A clean checkout and its revision, so the release records what it packaged."""
    if not (directory / ".git").exists():
        raise ValueError(f"{name}: {directory} is not a git checkout")
    if git(directory, "status", "--porcelain", "--untracked-files=normal"):
        raise ValueError(f"{name}: the checkout at {directory} has uncommitted changes")
    return git(directory, "rev-parse", "HEAD")


def nor_descriptor(image):
    """The installer's descriptor of the dual-boot installer: where the bootloader
    starts inside the DFU image, behind its own IMG1 header."""
    header = image.find(b"87021.0\x02", 0x800)
    if header < 0:
        raise ValueError("No bootloader header in the dual-boot installer image")
    offset = header + 0x800
    (length,) = struct.unpack_from("<I", image, header + 12)
    if offset + length != len(image):
        raise ValueError("Dual-boot installer image does not end with its bootloader")
    return {
        "schema": 1,
        "interface": "classic7g-file-v1",
        "bytes": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "bootloader_offset": offset,
    }


def build_helper(installer, toolchain, out, jobs):
    """The USB upload helper from the installer checkout, into a fresh directory."""
    directory = out / "usb-helper"
    if directory.exists():
        shutil.rmtree(directory)
    run(
        [
            sys.executable,
            installer / "usb-helper/build.py",
            "--out",
            directory,
            "--toolchain",
            toolchain,
            "--rockbox",
            installer / "vendor/rockbox",
            "--jobs",
            str(jobs),
        ],
        installer,
    )
    return directory


def locked_commit(lock):
    """The commit a lock file of another checkout names."""
    match = re.search(r'^commit = "([0-9a-f]{40})"', lock.read_text(), re.M)
    if match is None:
        raise ValueError(f"{lock} names no commit")
    return match[1]


def themes_lock(extensions):
    """The themes commit the Appearance extension's Classic theme is built from."""
    return locked_commit(extensions / "appearance/themes.lock")


def eapp_sdk_lock(homebrew):
    """The eApp SDK commit the games and apps are built against."""
    return locked_commit(homebrew / "eapp-sdk.lock")


def has_commit(repository, commit, name):
    """A locked commit, checked before the long builds start."""
    if not (repository / ".git").exists():
        raise ValueError(f"{name}: {repository} is not a git checkout")
    found = subprocess.run(
        ["git", "-C", str(repository), "cat-file", "-e", f"{commit}^{{commit}}"],
        capture_output=True,
    )
    if found.returncode != 0:
        raise ValueError(f"{name}: {repository} has no commit {commit}; fetch it")
    return commit


@contextlib.contextmanager
def locked_checkout(repository, commit):
    """A temporary checkout of a repository at a locked commit."""
    with tempfile.TemporaryDirectory(prefix="revola-") as parent:
        directory = Path(parent) / repository.name
        run(
            ["git", "-C", repository, "worktree", "add", "--detach", directory, commit],
            repository,
        )
        try:
            yield directory
        finally:
            run(
                ["git", "-C", repository, "worktree", "remove", "--force", directory],
                repository,
            )


def build_extensions(extensions, sdk, out, api_major, themes):
    """Every extension of the checkout, built against the staged SDK, with the
    Classic theme rebuilt from the locked themes checkout."""
    dist = out / "extensions"
    if dist.exists():
        shutil.rmtree(dist)
    (extensions / "appearance/build/classic.rvt").unlink(missing_ok=True)
    run(["make", f"SDK={sdk}", f"THEMES={themes}", f"DIST={dist}", "-s"], extensions)
    packages = {}
    for manifest in sorted(extensions.glob("*/manifest.toml")):
        package = tomllib.loads(manifest.read_text())
        file = dist / f"{package['id']}.rvx"
        if not file.is_file():
            raise ValueError(f"{package['id']}: no .rvx was built")
        packages[package["id"]] = {
            "format": "revola-extension-v1",
            "version": package["version"],
            "api": api_major,
            "type": package.get("type", ""),
            "file": str(file),
        }
    return packages


def build_themes(themes, out):
    """Every theme folder of the checkout but classic, which the Appearance extension
    carries itself, compiled to one .rvt each with the version of its theme.toml."""
    dist = out / "themes"
    if dist.exists():
        shutil.rmtree(dist)
    packages = {}
    for manifest in sorted(themes.glob("themes/*/theme.toml")):
        folder = manifest.parent
        theme = tomllib.loads(manifest.read_text())
        if "id" not in theme:
            raise ValueError(f"{folder.name}: theme.toml has no id")
        if theme["id"] == "classic":
            continue
        if not isinstance(theme.get("version"), str):
            raise ValueError(f"{theme['id']}: theme.toml needs a version string")
        run([sys.executable, "-m", "rvt", "compile", folder, "--out", dist], themes)
        file = dist / theme["id"] / f"{theme['id']}.rvt"
        if not file.is_file():
            raise ValueError(f"{theme['id']}: no .rvt was built")
        packages[theme["id"]] = {
            "format": "revola-theme-v1",
            "version": theme["version"],
            "file": str(file),
        }
    return packages


def build_homebrew(homebrew, sdk, out):
    """Every game and app of the checkout, built against the locked eApp SDK: one
    folder each, with the version its Manifest.plist carries."""
    dist = out / "homebrew"
    if dist.exists():
        shutil.rmtree(dist)
    run(["make", f"SDK={sdk}", f"DIST={dist}", "-s"], homebrew)
    packages = {"Games": {}, "Apps": {}}
    for root, entries in packages.items():
        for folder in sorted(p for p in (dist / root).glob("*") if p.is_dir()):
            manifest = plistlib.loads((folder / "Manifest.plist").read_bytes())
            if not isinstance(manifest.get("Version"), str):
                raise ValueError(
                    f"{folder.name}: Manifest.plist needs a Version string"
                )
            entries[folder.name] = {
                "format": "revola-eapp-v1",
                "version": manifest["Version"],
                "files": {
                    file.name: str(file)
                    for file in sorted(folder.iterdir())
                    if file.is_file()
                },
            }
    return packages["Games"], packages["Apps"]


def catalog_fingerprint(themes):
    """The themes catalog fingerprint every .rvt header carries, from the checkout's
    catalog of the pinned firmware."""
    catalog = json.loads((themes / f"rvt/data/{firmware.NAME}.json").read_text())
    fingerprint = catalog["fingerprint"]
    if not isinstance(fingerprint, str) or not re.fullmatch(
        r"[0-9a-f]{16}", fingerprint
    ):
        raise ValueError(f"themes: catalog fingerprint {fingerprint!r} is not 8 bytes")
    return fingerprint


def specification(version, minimum, out, helper, extensions, themes, games, apps, core):
    return {
        "purpose": "release",
        "version": version,
        "minimum_installer_version": minimum,
        "compatibility": firmware.load()["compatibility"],
        "core": core,
        "components": {
            "usb_helper": {
                "format": "revola-upload-v1",
                "files": {
                    "image": str(helper / "upload.dfu"),
                    "descriptor": str(helper / "manifest.json"),
                },
            },
            "firmware": {
                "format": "revola-firmware-recipe-v1",
                "files": {
                    "recipe": str(out / "firmware.json"),
                    "data": str(out / "firmware.data"),
                },
            },
            "companion": {
                "format": "revola-companion-recipe-v1",
                "files": {
                    "recipe": str(out / "companion.json"),
                    "data": str(out / "companion.data"),
                },
            },
            "nor": {
                "format": "revola-nor-template-v1",
                "files": {
                    "image": str(out / "install-rockbox-cfw.dfu"),
                    "descriptor": str(out / "nor.json"),
                    "uninstaller": str(out / "uninstall-rockbox-cfw.dfu"),
                },
            },
        },
        "extensions": extensions,
        "themes": themes,
        "games": games,
        "apps": apps,
    }


def bundle(cli, out, spec, seed, version):
    """Export, sign and pack with the installer's CLI; lay the release assets out
    under the names the installer fetches: <target>-manifest.json, its signature and
    one <sha256>.blob per file."""
    directory = out / "bundle"
    if directory.exists():
        shutil.rmtree(directory)
    spec_file = out / "spec.json"
    spec_file.write_text(json.dumps(spec, indent=2) + "\n")
    run(
        [
            cli,
            "bundle",
            "export",
            "--spec",
            spec_file,
            "--base",
            out,
            "--out",
            directory,
        ],
        out,
    )
    if seed is not None:
        run([cli, "bundle", "sign", "--directory", directory, "--seed", seed], out)
    package = out / f"revola-{version}.zip"
    package.unlink(missing_ok=True)
    run([cli, "bundle", "pack", "--directory", directory, "--out", package], out)
    assets = out / "release"
    if assets.exists():
        shutil.rmtree(assets)
    assets.mkdir()
    shutil.copyfile(directory / "manifest.json", assets / f"{TARGET}-manifest.json")
    if seed is not None:
        shutil.copyfile(
            directory / "manifest.json.sig", assets / f"{TARGET}-manifest.json.sig"
        )
    for blob in directory.glob("*.blob"):
        shutil.copyfile(blob, assets / blob.name)
    shutil.copyfile(package, assets / package.name)
    sdk = out / f"revola-sdk-{version}.tar.gz"
    shutil.copyfile(sdk, assets / sdk.name)
    return assets


def archive(directory, revision, name, assets):
    """The tree of a checkout at one revision, as a release asset. Follows
    RepriseOS tools/release.py archive_sources (8fc840d) by lgreitis."""
    run(
        [
            "git",
            "-C",
            directory,
            "archive",
            "--format=tar.gz",
            f"--prefix={name}/",
            "--output",
            assets / f"{name}-source.tar.gz",
            revision,
        ],
        directory,
    )


def archive_sources(assets, version, pins, args):
    """The corresponding source of the bundle: every pinned checkout, which holds
    the Rockbox patches it applies, the locked eApp SDK, and the unpatched Rockbox
    tree of each revision a binary is built from (the boot images from this
    repository's lock, the USB helper from the installer's)."""
    checkouts = {
        "revola": ROOT,
        "installer": args.installer,
        "extensions": args.extensions,
        "themes": args.themes,
        "homebrew": args.homebrew,
    }
    for name, directory in checkouts.items():
        archive(directory, pins[name], f"{name}-{version}", assets)
    classic = themes_lock(args.extensions)
    if classic != pins["themes"]:
        archive(args.themes, classic, f"themes-{classic[:10]}", assets)
    archive(
        args.eapp_sdk, pins["eapp-sdk"], f"eapp-sdk-{pins['eapp-sdk'][:10]}", assets
    )
    rockbox = {}
    for lock, checkout in (
        (ROOT / "boot/dependencies.lock", ROOT / "vendor/rockbox"),
        (args.installer / "dependencies.lock", args.installer / "vendor/rockbox"),
    ):
        commit = json.loads(lock.read_text())["rockbox"]["commit"]
        rockbox.setdefault(commit, checkout)
    for commit, checkout in rockbox.items():
        archive(checkout, commit, f"rockbox-{commit[:10]}", assets)


def licences(assets):
    """The licence texts and NOTICE that go with the binaries."""
    for source, name in (
        (ROOT / "COPYING", "COPYING"),
        (ROOT / "NOTICE", "NOTICE"),
        (ROOT / "core/assets/LICENSE.cupertino-icons", "LICENSE.cupertino-icons"),
    ):
        shutil.copyfile(source, assets / name)


def upload(tag, version, assets, notes):
    command = [
        "gh",
        "release",
        "create",
        tag,
        "--repo",
        REPOSITORY,
        "--title",
        f"RevolaOS {version}",
        "--verify-tag",
    ]
    command += ["--notes-file", str(notes)] if notes else ["--generate-notes"]
    if "-" in version:
        command.append("--prerelease")
    command += sorted(str(path) for path in assets.iterdir())
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tag",
        required=True,
        help="release tag at HEAD, vYYYY.M.N with an optional -beta.N",
    )
    parser.add_argument(
        "--minimum-installer-version",
        required=True,
        help="oldest installer that may use the bundle; a prerelease installer needs a prerelease here",
    )
    parser.add_argument(
        "--inputs",
        type=Path,
        help="decrypted 2.0.5 inputs; with them the site table is checked against the image",
    )
    parser.add_argument(
        "--without-image",
        action="store_true",
        help="build the recipes without the Apple image; run a checked build before the tag",
    )
    parser.add_argument("--out", type=Path, default=ROOT / "build/release")
    parser.add_argument("--installer", type=Path, default=ROOT.parent / "installer")
    parser.add_argument("--extensions", type=Path, default=ROOT.parent / "extensions")
    parser.add_argument("--themes", type=Path, default=ROOT.parent / "themes")
    parser.add_argument("--homebrew", type=Path, default=ROOT.parent / "homebrew")
    parser.add_argument("--eapp-sdk", type=Path, default=ROOT.parent / "eapp-sdk")
    signing = parser.add_mutually_exclusive_group()
    signing.add_argument(
        "--seed",
        type=Path,
        help="ed25519 seed that signs the manifest; omit for an unsigned trial",
    )
    signing.add_argument(
        "--seed-prompt",
        action="store_true",
        help="ask for the seed instead of reading it from a file",
    )
    parser.add_argument(
        "--notes", type=Path, help="release notes; generated from commits otherwise"
    )
    parser.add_argument(
        "--upload", action="store_true", help="publish the GitHub release"
    )
    parser.add_argument(
        "--cross-prefix", default=os.environ.get("CROSS_COMPILE", "arm-elf-eabi-")
    )
    parser.add_argument("--assembler", default="revola", help="installer CLI on PATH")
    parser.add_argument("--jobs", type=int, choices=range(1, 9), default=8)
    args = parser.parse_args()
    # Absolute, because git runs with both -C and the checkout as its cwd.
    for name in ("installer", "extensions", "themes", "homebrew", "eapp_sdk"):
        setattr(args, name, getattr(args, name).resolve())
    try:
        if (args.inputs is None) == (not args.without_image):
            raise ValueError(
                "give --inputs for a checked build, or --without-image to skip it"
            )
        target = firmware.load()
        sdklock.check()
        build_identity = identity(ROOT, args.tag)
        if args.upload and args.seed is None and not args.seed_prompt:
            raise ValueError(
                "--upload needs --seed or --seed-prompt; the installer accepts only signed bundles"
            )
        if not shutil.which(args.assembler):
            raise ValueError(
                f"{args.assembler} is not on PATH; build the installer CLI"
            )
        has_commit(args.themes, themes_lock(args.extensions), "themes")
        pins = {
            "revola": git(ROOT, "rev-parse", "HEAD"),
            "installer": pinned(args.installer, "installer"),
            "extensions": pinned(args.extensions, "extensions"),
            "themes": pinned(args.themes, "themes"),
            "homebrew": pinned(args.homebrew, "homebrew"),
            "eapp-sdk": has_commit(
                args.eapp_sdk, eapp_sdk_lock(args.homebrew), "eapp-sdk"
            ),
        }
        prefix = build.compiler_prefix(args.cross_prefix, target["toolchain"]["gcc"])
        if args.seed_prompt:
            with seed_file() as args.seed:
                release(args, build_identity, pins, prefix)
        else:
            release(args, build_identity, pins, prefix)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


@contextlib.contextmanager
def seed_file():
    """Ask for the ed25519 seed and hold it in a private file for the installer CLI
    while the release builds."""
    seed = getpass.getpass("ed25519 seed (64 hex digits): ").strip()
    if not re.fullmatch(r"[0-9a-fA-F]{64}", seed):
        raise ValueError("the seed is 32 bytes as 64 hex digits")
    handle, path = tempfile.mkstemp(prefix="revola-seed-")
    try:
        with os.fdopen(handle, "w") as file:
            file.write(seed)
        yield Path(path)
    finally:
        os.unlink(path)


def release(args, build_identity, pins, prefix):
    version = build_identity["version"]
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    config = build.BuildConfig(ROOT / ".build", prefix, args.jobs, args.assembler)
    api_major = int(sdklock.api_version().split(".")[0])

    build.build_firmware(
        config, out, args.inputs, build_identity, recipe_only=args.without_image
    )
    build.build_apple(config, out, args.inputs, recipe_only=True)
    dependencies = json.loads((ROOT / "boot/dependencies.lock").read_text())
    build.build_rockbox(config, out, setup.verify("rockbox", dependencies["rockbox"]))
    (out / "nor.json").write_text(
        json.dumps(nor_descriptor((out / "install-rockbox-cfw.dfu").read_bytes()))
        + "\n"
    )
    build.build_sdk(config, out, version)
    sdk = config.work / "sdk"
    with locked_checkout(args.themes, themes_lock(args.extensions)) as checkout:
        extensions = build_extensions(args.extensions, sdk, out, api_major, checkout)
    themes = build_themes(args.themes, out)
    with locked_checkout(args.eapp_sdk, pins["eapp-sdk"]) as eapp_sdk:
        games, apps = build_homebrew(args.homebrew, eapp_sdk, out)
    helper = build_helper(args.installer, Path(prefix).parent, out, args.jobs)
    core = {
        "api": api_major,
        "firmware_build_id": firmware.build_id(),
        "catalog_fingerprint": catalog_fingerprint(args.themes),
    }
    spec = specification(
        version,
        args.minimum_installer_version,
        out,
        helper,
        extensions,
        themes,
        games,
        apps,
        core,
    )
    assets = bundle(args.assembler, out, spec, args.seed, version)
    (assets / "build.json").write_text(
        json.dumps({"version": version, "pins": pins}, indent=2) + "\n"
    )
    licences(assets)
    archive_sources(assets, version, pins, args)
    print(f"Release assets: {assets}", flush=True)
    if args.upload:
        upload(args.tag, version, assets, args.notes)
        print(f"Published https://github.com/{REPOSITORY}/releases/tag/{args.tag}")


if __name__ == "__main__":
    main()
