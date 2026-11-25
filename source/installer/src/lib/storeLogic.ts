export type ExtensionType =
  | "audio"
  | "customization"
  | "media"
  | "utility"
  | "connectivity"
  | "developer";

export interface CategoryInfo {
  id: ExtensionType;
  label: string;
  description: string;
}

export interface WhatsNewItem {
  id: string;
  title: string;
  summary: string;
  version: string;
}

export interface FirmwareFile {
  path: string;
  bytes: number;
  sha256: string;
  url: string;
}

export interface FirmwareCatalog {
  name: string;
  version: string;
  api: string;
  target: string;
  checks: number;
  output_bytes: number;
  requires_custom_build: boolean;
  files: Record<string, FirmwareFile>;
}

export interface ExtensionItem {
  id: string;
  name: string;
  type: ExtensionType;
  version: string;
  author: string;
  stability: string;
  api: string;
  default_enabled: boolean;
  requires_custom_core: boolean;
  description: string;
  changelog: string;
  file: string;
  bytes: number;
  sha256: string;
  url: string;
  source_repo?: string | null;
  source_url?: string | null;
}

export interface BundleFile {
  name: string;
  path: string;
  bytes: number;
  sha256: string;
  url: string;
}

export interface BundleItem {
  id: string;
  folder: string;
  kind: "game" | "app";
  name: string;
  version: string;
  author: string;
  description: string;
  changelog: string;
  total_bytes: number;
  files: BundleFile[];
  source_repo?: string | null;
  source_url?: string | null;
}

export interface RepoSourceStatus {
  repo: string;
  url: string;
  ok: boolean;
  error?: string | null;
  added_extensions: number;
  added_games: number;
  added_apps: number;
}

export interface Catalog {
  schema: number;
  repo: string;
  branch: string;
  backbone_disclaimer: string;
  custom_build_notice: string;
  whats_new: WhatsNewItem[];
  categories: CategoryInfo[];
  firmware: FirmwareCatalog;
  extensions: ExtensionItem[];
  games: BundleItem[];
  apps: BundleItem[];
  sources?: RepoSourceStatus[];
}

export const CUSTOM_REPOS_STORAGE_KEY = "revisionos.custom_repos.v1";

export function normalizeRepoInput(raw: string): string {
  const trimmed = raw.trim();
  if (!trimmed) {
    throw new Error("Enter a repository (owner/repo[@branch]) or HTTPS catalog.json URL.");
  }
  if (trimmed.startsWith("https://")) {
    if (!trimmed.endsWith(".json")) {
      throw new Error("Custom HTTPS URL must point to a .json catalog file.");
    }
    return trimmed;
  }
  if (trimmed.startsWith("http://")) {
    throw new Error("Only HTTPS catalog URLs are allowed.");
  }
  const clean = trimmed.replace(/^https:\/\/github\.com\//, "").replace(/\/+$/, "");
  const atIdx = clean.indexOf("@");
  const repoPart = atIdx >= 0 ? clean.slice(0, atIdx) : clean;
  const branch = atIdx >= 0 ? clean.slice(atIdx + 1).trim() : "main";
  if (atIdx >= 0 && !branch) {
    throw new Error("Missing branch name after @.");
  }
  const segments = repoPart.split("/");
  if (segments.length !== 2 || !segments[0] || !segments[1]) {
    throw new Error("Use owner/repo, owner/repo@branch, or https://.../catalog.json.");
  }
  const validSegment = /^[A-Za-z0-9_.-]+$/;
  if (!validSegment.test(segments[0]) || !validSegment.test(segments[1]) || !validSegment.test(branch)) {
    throw new Error("Repository name or branch contains invalid characters.");
  }
  return `https://raw.githubusercontent.com/${segments[0]}/${segments[1]}/${branch}/catalog.json`;
}

export function isApiCompatible(itemApi: string, coreApi: string): boolean {
  const parseApi = (raw: string): [number, number] | null => {
    const parts = raw.trim().replace(/^v/i, "").split(".");
    if (parts.length < 2) return null;
    const maj = Number.parseInt(parts[0], 10);
    const min = Number.parseInt(parts[1], 10);
    if (Number.isNaN(maj) || Number.isNaN(min)) return null;
    return [maj, min];
  };
  const a = parseApi(itemApi);
  const c = parseApi(coreApi);
  if (!a || !c) return false;
  return a[0] === c[0] && a[1] <= c[1];
}

export interface InstalledExtension {
  id: string;
  name: string;
  version: string;
  author: string;
  extension_type: ExtensionType | null;
  stability: string | null;
  file_name: string;
  bytes: number;
  sha256: string;
}

export interface InstalledBundle {
  id: string;
  folder: string;
  kind: string;
  name: string;
  version: string;
  total_bytes: number;
}

export type FirmwareLayout =
  | "revola_backbone"
  | "reprise_bootloader_with_revision_os"
  | "revola_root_legacy"
  | "reprise_os"
  | "mixed"
  | "fresh";

export interface IpodDriveSummary {
  root: string;
  label: string;
  layout: FirmwareLayout;
  has_revola_bin: boolean;
  has_loader_bin: boolean;
  has_reprise_osos: boolean;
  has_root_revola_bin: boolean;
  reprise_osos_matches_revola: boolean;
  migration_recommended: boolean;
  extensions_count: number;
  games_count: number;
  reprise_games_count: number;
  apple_games_count: number;
  apps_count: number;
}

export interface IpodScan {
  root: string;
  layout: FirmwareLayout;
  has_revola_bin: boolean;
  revola_bin_sha256: string | null;
  has_loader_bin: boolean;
  has_reprise_osos: boolean;
  has_reprise_loader: boolean;
  has_root_revola_bin: boolean;
  has_root_loader_bin: boolean;
  reprise_osos_matches_revola: boolean;
  migration_recommended: boolean;
  has_ipod_control: boolean;
  extensions: InstalledExtension[];
  games: InstalledBundle[];
  reprise_games: InstalledBundle[];
  apple_games_count: number;
  apps: InstalledBundle[];
}

export interface ProgressEvent {
  step: string;
  detail: string;
  completed: number;
  total: number;
}

export interface CachedFirmwareStatus {
  has_ipsw: boolean;
  has_decrypted_osos: boolean;
}

export interface MigrationOptions {
  migrate_games_ro: boolean;
  cleanup_legacy_bins: boolean;
  upgrade_firmware: boolean;
  update_extensions: boolean;
}

export interface MigrationReport {
  source_layout: FirmwareLayout;
  migrated_games: string[];
  updated_extensions: string[];
  installed_homebrew: boolean;
  firmware_updated: boolean;
  cleaned_legacy_files: string[];
  notes: string[];
}

export type StoreAction =
  | { action: "install_extension"; id: string }
  | { action: "update_extension"; id: string }
  | { action: "remove_extension"; id: string }
  | { action: "install_game"; id: string }
  | { action: "update_game"; id: string }
  | { action: "remove_game"; id: string }
  | { action: "install_app"; id: string }
  | { action: "update_app"; id: string }
  | { action: "remove_app"; id: string };

export type ItemStatus =
  | { status: "up_to_date"; installed: string }
  | { status: "update_available"; installed: string; latest: string }
  | { status: "not_installed" };

export interface UpdateCandidate {
  kind: "extension" | "game" | "app";
  id: string;
  name: string;
  installedVersion: string;
  latestVersion: string;
  changelog: string;
}

export interface ContentSelection {
  extensions: string[];
  games: string[];
  apps: string[];
}

export function compareVersions(a: string, b: string): number {
  const parse = (raw: string) =>
    raw
      .trim()
      .replace(/^v/i, "")
      .split("-")[0]
      .split(".")
      .map((part) => Number.parseInt(part, 10) || 0);
  const pa = parse(a);
  const pb = parse(b);
  const len = Math.max(pa.length, pb.length);
  for (let i = 0; i < len; i += 1) {
    const da = pa[i] ?? 0;
    const db = pb[i] ?? 0;
    if (da < db) return -1;
    if (da > db) return 1;
  }
  return 0;
}

export function getExtensionStatus(item: ExtensionItem, scan: IpodScan | null): ItemStatus {
  if (!scan) return { status: "not_installed" };
  const installed = scan.extensions.find((ext) => ext.id === item.id);
  if (!installed) return { status: "not_installed" };
  if (
    compareVersions(installed.version, item.version) < 0 ||
    (installed.version === item.version && installed.sha256 !== item.sha256)
  ) {
    return { status: "update_available", installed: installed.version, latest: item.version };
  }
  return { status: "up_to_date", installed: installed.version };
}

export function getBundleStatus(
  item: BundleItem,
  scan: IpodScan | null,
  kind: "game" | "app",
): ItemStatus {
  if (!scan) return { status: "not_installed" };
  const list = kind === "game" ? scan.games : scan.apps;
  const installed = list.find((entry) => entry.id === item.id);
  if (!installed) return { status: "not_installed" };
  if (compareVersions(installed.version, item.version) < 0) {
    return { status: "update_available", installed: installed.version, latest: item.version };
  }
  return { status: "up_to_date", installed: installed.version };
}

export function computeUpdatesAvailable(catalog: Catalog, scan: IpodScan | null): UpdateCandidate[] {
  if (!scan) return [];
  const updates: UpdateCandidate[] = [];
  for (const ext of catalog.extensions) {
    const st = getExtensionStatus(ext, scan);
    if (st.status === "update_available") {
      updates.push({
        kind: "extension",
        id: ext.id,
        name: ext.name,
        installedVersion: st.installed,
        latestVersion: st.latest,
        changelog: ext.changelog,
      });
    }
  }
  for (const game of catalog.games) {
    const st = getBundleStatus(game, scan, "game");
    if (st.status === "update_available") {
      updates.push({
        kind: "game",
        id: game.id,
        name: game.name,
        installedVersion: st.installed,
        latestVersion: st.latest,
        changelog: game.changelog,
      });
    }
  }
  for (const app of catalog.apps) {
    const st = getBundleStatus(app, scan, "app");
    if (st.status === "update_available") {
      updates.push({
        kind: "app",
        id: app.id,
        name: app.name,
        installedVersion: st.installed,
        latestVersion: st.latest,
        changelog: app.changelog,
      });
    }
  }
  return updates;
}

export function buildUpdateAllActions(catalog: Catalog, scan: IpodScan): StoreAction[] {
  return computeUpdatesAvailable(catalog, scan).map((u) => {
    if (u.kind === "extension") return { action: "update_extension", id: u.id };
    if (u.kind === "game") return { action: "update_game", id: u.id };
    return { action: "update_app", id: u.id };
  });
}

export function ensureHomebrewRequirement(selection: ContentSelection): {
  selection: ContentSelection;
  autoAddedHomebrew: boolean;
} {
  const needsHomebrew = selection.games.length > 0 || selection.apps.length > 0;
  if (!needsHomebrew || selection.extensions.includes("homebrew")) {
    return { selection, autoAddedHomebrew: false };
  }
  return {
    selection: {
      ...selection,
      extensions: [...selection.extensions, "homebrew"],
    },
    autoAddedHomebrew: true,
  };
}

export function computePresetSelection(
  catalog: Catalog,
  preset: "recommended" | "minimal" | "everything",
): ContentSelection {
  if (preset === "minimal") {
    return { extensions: [], games: [], apps: [] };
  }
  if (preset === "everything") {
    return {
      extensions: catalog.extensions.map((e) => e.id),
      games: catalog.games.map((g) => g.id),
      apps: catalog.apps.map((a) => a.id),
    };
  }
  const extensions = catalog.extensions.filter((e) => e.default_enabled).map((e) => e.id);
  const games = catalog.games.map((g) => g.id);
  const apps = catalog.apps.map((a) => a.id);
  return ensureHomebrewRequirement({ extensions, games, apps }).selection;
}

export function filterExtensions(
  extensions: ExtensionItem[],
  category: "all" | ExtensionType,
  query: string,
): ExtensionItem[] {
  const q = query.trim().toLowerCase();
  return extensions.filter((ext) => {
    if (category !== "all" && ext.type !== category) return false;
    if (!q) return true;
    return (
      ext.name.toLowerCase().includes(q) ||
      ext.id.toLowerCase().includes(q) ||
      ext.description.toLowerCase().includes(q) ||
      ext.author.toLowerCase().includes(q)
    );
  });
}

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MiB`;
}

export function describeDetectedLayout(layout: FirmwareLayout): {
  title: string;
  summary: string;
} {
  switch (layout) {
    case "reprise_bootloader_with_revision_os":
      return {
        title: "RevisionOS (booted via RepriseOS bootloader)",
        summary:
          "Your NOR uses the RepriseOS bootloader to boot /osos-cfw.bin, which already matches /Revola/Firmware/revola.bin. Keep /osos-cfw.bin and /cfw-loader.bin intact; core updates automatically update both.",
      };
    case "reprise_os":
      return {
        title: "RepriseOS installation detected",
        summary:
          "Found /osos-cfw.bin or /iPod_Control/Games_RO/ homebrew. Apple Click Wheel archival games stay untouched while unsigned homebrew and extensions migrate into /Revola/.",
      };
    case "revola_root_legacy":
      return {
        title: "Legacy root RevolaOS installation detected",
        summary:
          "Found root /revola.bin or /loader.bin. Migration moves them into /Revola/Firmware/, updates extensions, and cleans up the old root files.",
      };
    case "mixed":
      return {
        title: "Mixed RepriseOS and RevolaOS files detected",
        summary:
          "Found both /Revola/ and legacy root or RepriseOS files. Migration consolidates firmware, extensions, and games cleanly into /Revola/.",
      };
    case "revola_backbone":
      return {
        title: "RevisionOS / RevolaOS layout detected",
        summary: "Your iPod already uses the /Revola/ directory structure.",
      };
    default:
      return {
        title: "Stock or fresh iPod storage",
        summary: "No RepriseOS or RevolaOS firmware files were found on this drive yet.",
      };
  }
}

export function importSelectionFromScan(catalog: Catalog, scan: IpodScan): ContentSelection {
  const catalogExtIds = new Set(catalog.extensions.map((e) => e.id));
  const catalogGameIds = new Set(catalog.games.map((g) => g.id));
  const catalogAppIds = new Set(catalog.apps.map((a) => a.id));

  const extensions = scan.extensions
    .map((e) => e.id)
    .filter((id, idx, arr) => catalogExtIds.has(id) && arr.indexOf(id) === idx);
  const games = [...scan.games, ...scan.reprise_games]
    .map((g) => g.id)
    .filter((id, idx, arr) => catalogGameIds.has(id) && arr.indexOf(id) === idx);
  const apps = scan.apps
    .map((a) => a.id)
    .filter((id, idx, arr) => catalogAppIds.has(id) && arr.indexOf(id) === idx);

  return ensureHomebrewRequirement({ extensions, games, apps }).selection;
}

