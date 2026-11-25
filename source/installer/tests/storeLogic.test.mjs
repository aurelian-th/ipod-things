import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import { mockCatalog } from "../src/dev/fixtures.ts";
import {
  buildUpdateAllActions,
  compareVersions,
  computePresetSelection,
  computeUpdatesAvailable,
  describeDetectedLayout,
  ensureHomebrewRequirement,
  filterExtensions,
  getExtensionStatus,
  importSelectionFromScan,
  isApiCompatible,
  normalizeRepoInput,
} from "../src/lib/storeLogic.ts";

const catalogPath = path.resolve(import.meta.dirname, "../../ipod-things/catalog.json");
const realCatalog = fs.existsSync(catalogPath)
  ? JSON.parse(fs.readFileSync(catalogPath, "utf8"))
  : mockCatalog;

test("compareVersions orders semver and short versions", () => {
  assert.equal(compareVersions("0.1.0", "1.0.0"), -1);
  assert.equal(compareVersions("1.0", "1.0.0"), 0);
  assert.equal(compareVersions("1.3.0", "0.2.0"), 1);
});

test("computeUpdatesAvailable detects outdated extension versions and hash changes", () => {
  const scan = {
    root: "/media/IPOD",
    layout: "revola_backbone",
    has_revola_bin: true,
    revola_bin_sha256: null,
    has_loader_bin: true,
    has_reprise_osos: false,
    has_reprise_loader: false,
    has_root_revola_bin: false,
    has_root_loader_bin: false,
    reprise_osos_matches_revola: false,
    migration_recommended: false,
    has_ipod_control: true,
    extensions: [
      {
        id: "lyrics",
        name: "Lyrics",
        version: "0.1.0",
        author: "0xAu",
        extension_type: "media",
        stability: "beta",
        file_name: "lyrics.rvx",
        bytes: 100,
        sha256: "old",
      },
      {
        id: "hold-standby-clock",
        name: "Hold Standby Clock",
        version: "1.0.0",
        author: "0xAu",
        extension_type: "customization",
        stability: "beta",
        file_name: "hold-standby-clock.rvx",
        bytes: realCatalog.extensions.find((e) => e.id === "hold-standby-clock").bytes,
        sha256: realCatalog.extensions.find((e) => e.id === "hold-standby-clock").sha256,
      },
    ],
    games: [
      { id: "doodlejump", folder: "doodlejump", kind: "game", name: "Doodle Jump", version: "0.2.0", total_bytes: 100 },
    ],
    reprise_games: [],
    apple_games_count: 0,
    apps: [],
  };

  const expectedLyricsVersion = realCatalog.extensions.find((e) => e.id === "lyrics").version;
  const expectedDoodleVersion = realCatalog.games.find((g) => g.id === "doodlejump").version;
  const updates = computeUpdatesAvailable(realCatalog, scan);
  assert.deepEqual(
    updates.map((u) => [u.id, u.installedVersion, u.latestVersion]),
    [
      ["lyrics", "0.1.0", expectedLyricsVersion],
      ["doodlejump", "0.2.0", expectedDoodleVersion],
    ],
  );
  assert.deepEqual(buildUpdateAllActions(realCatalog, scan), [
    { action: "update_extension", id: "lyrics" },
    { action: "update_game", id: "doodlejump" },
  ]);

  const hscItem = realCatalog.extensions.find((e) => e.id === "hold-standby-clock");
  assert.equal(getExtensionStatus(hscItem, scan).status, "up_to_date");
});

test("computePresetSelection and ensureHomebrewRequirement handle games/apps", () => {
  const minimal = computePresetSelection(realCatalog, "minimal");
  assert.deepEqual(minimal, { extensions: [], games: [], apps: [] });

  const withGame = ensureHomebrewRequirement({
    extensions: ["hold-standby-clock"],
    games: ["doodlejump"],
    apps: [],
  });
  assert.equal(withGame.autoAddedHomebrew, true);
  assert.ok(withGame.selection.extensions.includes("homebrew"));

  const everything = computePresetSelection(realCatalog, "everything");
  assert.equal(everything.extensions.length, realCatalog.extensions.length);
  assert.equal(everything.games.length, realCatalog.games.length);
  assert.equal(everything.apps.length, realCatalog.apps.length);
});

test("filterExtensions filters by type and query", () => {
  const custom = filterExtensions(realCatalog.extensions, "customization", "");
  assert.ok(custom.every((e) => e.type === "customization"));
  assert.ok(custom.some((e) => e.id === "hold-standby-clock"));
  const searched = filterExtensions(realCatalog.extensions, "all", "0xAu");
  assert.ok(searched.some((e) => e.id === "silent-remote-volume"));
});

test("importSelectionFromScan and describeDetectedLayout import RepriseOS and RevolaOS content", () => {
  assert.match(describeDetectedLayout("reprise_os").title, /RepriseOS/);
  assert.match(describeDetectedLayout("revola_root_legacy").title, /Legacy root RevolaOS/);
  assert.match(
    describeDetectedLayout("reprise_bootloader_with_revision_os").title,
    /RepriseOS bootloader/,
  );

  const scan = {
    root: "/media/IPOD",
    layout: "reprise_os",
    has_revola_bin: false,
    revola_bin_sha256: null,
    has_loader_bin: false,
    has_reprise_osos: true,
    has_reprise_loader: true,
    has_root_revola_bin: false,
    has_root_loader_bin: false,
    reprise_osos_matches_revola: false,
    migration_recommended: true,
    has_ipod_control: true,
    extensions: [{ id: "iap2" }],
    games: [],
    reprise_games: [{ id: "2048" }],
    apple_games_count: 49,
    apps: [],
  };
  const imported = importSelectionFromScan(realCatalog, scan);
  assert.deepEqual(imported.games, ["2048"]);
  assert.ok(imported.extensions.includes("iap2"));
  assert.ok(imported.extensions.includes("homebrew"));
});

test("normalizeRepoInput and isApiCompatible validate custom Git repositories", () => {
  assert.equal(
    normalizeRepoInput("alice/ipod-mods"),
    "https://raw.githubusercontent.com/alice/ipod-mods/main/catalog.json",
  );
  assert.equal(
    normalizeRepoInput("alice/ipod-mods@beta"),
    "https://raw.githubusercontent.com/alice/ipod-mods/beta/catalog.json",
  );
  assert.equal(
    normalizeRepoInput("https://example.com/custom/catalog.json"),
    "https://example.com/custom/catalog.json",
  );
  assert.throws(() => normalizeRepoInput("http://example.com/catalog.json"));
  assert.throws(() => normalizeRepoInput("not-a-repo"));

  assert.equal(isApiCompatible("1.74", "1.74"), true);
  assert.equal(isApiCompatible("1.70", "1.74"), true);
  assert.equal(isApiCompatible("1.80", "1.74"), false);
  assert.equal(isApiCompatible("2.0", "1.74"), false);
});

