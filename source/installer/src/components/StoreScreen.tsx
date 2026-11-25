import React from "react";
import { useHeadingFocus } from "../hooks/useHeadingFocus";
import { errorDetail } from "../lib/errors";
import {
  applyStoreChanges,
  cancelStoreTask,
  detectIpodDrives,
  diskModeUpgradeFirmware,
  fetchRevisionCatalog,
  getCachedFirmwareStatus,
  loadCustomRepoUrls,
  pickIpodDrive,
  saveCustomRepoUrls,
  scanIpodDrive,
} from "../lib/store";
import {
  type CachedFirmwareStatus,
  type Catalog,
  type ExtensionType,
  type IpodDriveSummary,
  type IpodScan,
  type ProgressEvent,
  type StoreAction,
  buildUpdateAllActions,
  computeUpdatesAvailable,
  describeDetectedLayout,
  filterExtensions,
  formatBytes,
  getBundleStatus,
  getExtensionStatus,
  isApiCompatible,
  normalizeRepoInput,
} from "../lib/storeLogic";
import { Button } from "./ui/Button";
import { InstallerScreen } from "./ui/InstallerScreen";

interface IStoreScreenProps {
  onBack: () => void;
  onOpenMigrate: () => void;
}

const categoryTabs: { id: "all" | ExtensionType; label: string }[] = [
  { id: "all", label: "All categories" },
  { id: "audio", label: "Audio" },
  { id: "customization", label: "Customization" },
  { id: "media", label: "Media" },
  { id: "utility", label: "Utility" },
  { id: "connectivity", label: "Connectivity" },
  { id: "developer", label: "Developer" },
];

const StoreScreen: React.FC<IStoreScreenProps> = ({ onBack, onOpenMigrate }) => {
  const { headingRef } = useHeadingFocus();
  const [catalog, setCatalog] = React.useState<Catalog | null>(null);
  const [drives, setDrives] = React.useState<IpodDriveSummary[]>([]);
  const [selectedRoot, setSelectedRoot] = React.useState("");
  const [scan, setScan] = React.useState<IpodScan | null>(null);
  const [fwCache, setFwCache] = React.useState<CachedFirmwareStatus>({
    has_ipsw: false,
    has_decrypted_osos: false,
  });
  const [tab, setTab] = React.useState<"updates" | "extensions" | "games" | "apps">("extensions");
  const [category, setCategory] = React.useState<"all" | ExtensionType>("all");
  const [statusFilter, setStatusFilter] = React.useState<"all" | "installed" | "available">("all");
  const [query, setQuery] = React.useState("");
  const [loading, setLoading] = React.useState(true);
  const [busy, setBusy] = React.useState(false);
  const [progress, setProgress] = React.useState<ProgressEvent | null>(null);
  const [logLines, setLogLines] = React.useState<string[]>([]);
  const [statusMessage, setStatusMessage] = React.useState("");
  const [error, setError] = React.useState("");
  const [customRepos, setCustomRepos] = React.useState<string[]>(() => loadCustomRepoUrls());
  const [repoInput, setRepoInput] = React.useState("");
  const [showRepos, setShowRepos] = React.useState(false);

  const appendLog = React.useCallback((line: string) => {
    const time = new Date().toLocaleTimeString();
    setLogLines((prev) => [...prev.slice(-80), `[${time}] ${line}`]);
  }, []);

  const refreshAll = React.useCallback(
    async (preferredRoot?: string, reposOverride?: string[]) => {
      setLoading(true);
      setError("");
      try {
        const activeRepos = reposOverride ?? customRepos;
        const [cat, foundDrives, cache] = await Promise.all([
          fetchRevisionCatalog(undefined, activeRepos),
          detectIpodDrives(),
          getCachedFirmwareStatus(),
        ]);
        setCatalog(cat);
        setDrives(foundDrives);
        setFwCache(cache);
        const nextRoot = preferredRoot || selectedRoot || foundDrives[0]?.root || "";
        setSelectedRoot(nextRoot);
        if (nextRoot) {
          const scanned = await scanIpodDrive(nextRoot);
          setScan(scanned);
        } else {
          setScan(null);
        }
      } catch (err) {
        setError(errorDetail(err));
      } finally {
        setLoading(false);
      }
    },
    [customRepos, selectedRoot],
  );

  React.useEffect(() => {
    void refreshAll();
  }, [refreshAll]);

  const handleAddRepo = async () => {
    setError("");
    try {
      const normalized = normalizeRepoInput(repoInput);
      if (customRepos.includes(normalized)) {
        setError("Repository is already added.");
        return;
      }
      const next = [...customRepos, normalized];
      saveCustomRepoUrls(next);
      setCustomRepos(next);
      setRepoInput("");
      await refreshAll(undefined, next);
    } catch (err) {
      setError(errorDetail(err));
    }
  };

  const handleRemoveRepo = async (url: string) => {
    const next = customRepos.filter((entry) => entry !== url);
    saveCustomRepoUrls(next);
    setCustomRepos(next);
    await refreshAll(undefined, next);
  };

  const handlePickDrive = async () => {
    try {
      const picked = await pickIpodDrive();
      if (!picked) return;
      await refreshAll(picked.root);
    } catch (err) {
      setError(errorDetail(err));
    }
  };

  const runActions = async (actions: StoreAction[], label: string) => {
    if (!selectedRoot) {
      setError("Connect your iPod in Disk Mode or choose its drive folder first.");
      return;
    }
    setBusy(true);
    setError("");
    setStatusMessage("");
    setLogLines([]);
    setProgress({ step: "start", detail: label, completed: 0, total: actions.length });
    appendLog(`${label} (${actions.length} step(s))`);
    try {
      await applyStoreChanges(selectedRoot, actions, (ev) => {
        setProgress(ev);
        appendLog(ev.detail);
      });
      const updated = await scanIpodDrive(selectedRoot);
      setScan(updated);
      setStatusMessage(`${label} finished cleanly. Safely eject your iPod when done.`);
      appendLog("Completed.");
    } catch (err) {
      const msg = errorDetail(err);
      setError(msg);
      appendLog(`Error: ${msg}`);
    } finally {
      setBusy(false);
    }
  };

  const upgradeFirmware = async () => {
    if (!selectedRoot) return;
    setBusy(true);
    setError("");
    setStatusMessage("");
    appendLog("Assembling RevisionOS revola.bin from cached Apple 2.0.5 firmware…");
    try {
      await diskModeUpgradeFirmware(selectedRoot);
      const updated = await scanIpodDrive(selectedRoot);
      setScan(updated);
      setStatusMessage(
        updated.reprise_osos_matches_revola
          ? "Updated /Revola/Firmware/revola.bin and synced /osos-cfw.bin."
          : "Updated /Revola/Firmware/revola.bin on your iPod.",
      );
      appendLog("Core firmware update complete.");
    } catch (err) {
      const msg = errorDetail(err);
      setError(msg);
      appendLog(`Error: ${msg}`);
    } finally {
      setBusy(false);
    }
  };

  const updates = catalog && scan ? computeUpdatesAvailable(catalog, scan) : [];
  const visibleExtensions = React.useMemo(() => {
    if (!catalog) return [];
    return filterExtensions(catalog.extensions, category, query).filter((ext) => {
      if (statusFilter === "all") return true;
      const st = getExtensionStatus(ext, scan);
      if (statusFilter === "installed") return st.status !== "not_installed";
      return st.status === "not_installed";
    });
  }, [catalog, category, query, scan, statusFilter]);

  const layoutMeta = scan ? describeDetectedLayout(scan.layout) : null;
  const pct =
    progress && progress.total > 0
      ? Math.min(100, Math.round((progress.completed * 100) / progress.total))
      : 0;

  return (
    <InstallerScreen.Root aria-labelledby="store-title">
      <InstallerScreen.Title id="store-title" ref={headingRef} tabIndex={-1}>
        Store and updates
      </InstallerScreen.Title>
      <InstallerScreen.Description className="mt-2 max-w-160 text-[13px] leading-5">
        Manage RevisionOS extensions, games, and apps from GitHub (
        <code className="text-ink">aurelian-th/ipod-things</code>) on a Disk Mode iPod.
      </InstallerScreen.Description>

      <div className="mt-5 space-y-4 text-left">
        <div className="installer-well flex flex-wrap items-center justify-between gap-3 rounded-xl p-4">
          <div className="min-w-0 flex-1 text-[13px]">
            <div className="flex flex-wrap items-center gap-2">
              <span className="font-semibold text-ink">
                {scan ? selectedRoot : "No Disk Mode iPod selected"}
              </span>
              {layoutMeta && (
                <span className="rounded-full bg-[#e8dfd1] px-2.5 py-0.5 text-[11px] font-medium text-[#704915]">
                  {layoutMeta.title}
                </span>
              )}
            </div>
            <p className="mt-1 text-[12px] text-body">
              {scan
                ? `${scan.extensions.length} extensions, ${scan.games.length} homebrew games, ${scan.apps.length} apps${scan.apple_games_count > 0 ? ` (${scan.apple_games_count} Apple archival games untouched)` : ""}`
                : "Mount your iPod in Disk Mode or browse for its mounted storage folder."}
            </p>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            {drives.length > 1 && (
              <select
                aria-label="Select connected iPod drive"
                value={selectedRoot}
                onChange={(event) => void refreshAll(event.target.value)}
                className="rounded-lg border border-[#c2bfba] bg-white px-2.5 py-1.5 text-[12px] text-ink"
              >
                {drives.map((d) => (
                  <option key={d.root} value={d.root}>
                    {d.label} ({d.root})
                  </option>
                ))}
              </select>
            )}
            <Button.Root
              size="compact"
              disabled={busy || loading}
              onClick={() => void refreshAll()}
            >
              <Button.Label>Refresh</Button.Label>
            </Button.Root>
            <Button.Root
              size="compact"
              disabled={busy || loading}
              onClick={() => void handlePickDrive()}
            >
              <Button.Label>Choose iPod folder…</Button.Label>
            </Button.Root>
            {scan && (
              <Button.Root
                size="compact"
                disabled={busy || loading || !fwCache.has_decrypted_osos}
                title={
                  fwCache.has_decrypted_osos
                    ? "Rebuild and update revola.bin on your iPod"
                    : "Run DFU install once to cache Apple 2.0.5 firmware first"
                }
                onClick={() => void upgradeFirmware()}
              >
                <Button.Label>
                  {fwCache.has_decrypted_osos
                    ? "Update revola.bin"
                    : "Update revola.bin (no cache)"}
                </Button.Label>
              </Button.Root>
            )}
            <Button.Root
              size="compact"
              disabled={busy || loading}
              onClick={() => setShowRepos((v) => !v)}
            >
              <Button.Label>
                {showRepos
                  ? "Hide repos"
                  : `Repositories (${1 + customRepos.length})`}
              </Button.Label>
            </Button.Root>
          </div>
        </div>

        {showRepos && (
          <section className="installer-well space-y-3 rounded-xl p-4 text-[12px]">
            <div>
              <h2 className="text-[13px] font-bold text-ink">Extension &amp; homebrew repositories</h2>
              <p className="mt-0.5 text-body">
                Add custom Cydia-style Git repositories using <code className="text-ink">owner/repo</code>,{" "}
                <code className="text-ink">owner/repo@branch</code>, or an HTTPS <code className="text-ink">catalog.json</code> URL. Official items always stay authoritative, and all downloads verify SHA-256 checksums.
              </p>
            </div>
            <div className="flex flex-wrap items-center gap-2">
              <input
                type="text"
                value={repoInput}
                onChange={(e) => setRepoInput(e.target.value)}
                onKeyDown={(e) => {
                  if (e.key === "Enter") {
                    e.preventDefault();
                    void handleAddRepo();
                  }
                }}
                placeholder="owner/repo or https://.../catalog.json"
                aria-label="Custom repository URL"
                className="min-w-64 flex-1 rounded-lg border border-[#c2bfba] bg-white px-3 py-1.5 text-[12px] text-ink outline-none focus:border-focus"
              />
              <Button.Root
                size="compact"
                disabled={busy || loading || !repoInput.trim()}
                onClick={() => void handleAddRepo()}
              >
                <Button.Label>Add source</Button.Label>
              </Button.Root>
            </div>
            {catalog?.sources && catalog.sources.length > 0 && (
              <div className="space-y-1.5">
                {catalog.sources.map((src, idx) => (
                  <div
                    key={src.url}
                    className="flex flex-wrap items-center justify-between gap-2 rounded-lg border border-[#dedbd5] bg-white px-3 py-2"
                  >
                    <div className="min-w-0 flex-1">
                      <span className="font-semibold text-ink">{src.repo}</span>
                      {idx === 0 ? (
                        <span className="ml-2 rounded bg-[#e8dfd1] px-1.5 py-0.5 text-[10px] font-medium text-[#704915]">
                          Official
                        </span>
                      ) : src.ok ? (
                        <span className="ml-2 rounded bg-[#eef7ef] px-1.5 py-0.5 text-[10px] font-medium text-[#225129]">
                          +{src.added_extensions} ext, +{src.added_games} games, +{src.added_apps} apps
                        </span>
                      ) : (
                        <span className="ml-2 rounded bg-[#fbeeed] px-1.5 py-0.5 text-[10px] font-medium text-[#aa3e36]">
                          Failed
                        </span>
                      )}
                      <p className="truncate text-[11px] text-muted">{src.url}</p>
                      {src.error && <p className="text-[11px] text-[#aa3e36]">{src.error}</p>}
                    </div>
                    {idx > 0 && (
                      <Button.Root
                        size="compact"
                        disabled={busy || loading}
                        onClick={() => void handleRemoveRepo(src.url)}
                      >
                        <Button.Label>Remove</Button.Label>
                      </Button.Root>
                    )}
                  </div>
                ))}
              </div>
            )}
          </section>
        )}

        {scan?.migration_recommended && (
          <div className="flex flex-wrap items-center justify-between gap-3 rounded-xl border border-[#bd8b3d] bg-[#fdf7ec] p-3.5 text-[12px]">
            <p className="text-ink">
              Older root firmware or unmigrated homebrew files were detected on this drive.
            </p>
            <Button.Root size="compact" onClick={onOpenMigrate}>
              <Button.Label>Open Migrate</Button.Label>
            </Button.Root>
          </div>
        )}

        {(busy || logLines.length > 0) && (
          <div className="installer-well space-y-2 rounded-xl p-3.5 text-[12px]">
            <div className="flex items-center justify-between gap-2">
              <span className="font-semibold text-ink">
                {progress ? progress.detail : "Sync activity"}
              </span>
              <div className="flex items-center gap-2">
                {progress && <span className="tabular-nums text-muted">{pct}%</span>}
                {busy && (
                  <Button.Root
                    size="compact"
                    intent="danger"
                    onClick={() => void cancelStoreTask()}
                  >
                    <Button.Label>Cancel</Button.Label>
                  </Button.Root>
                )}
              </div>
            </div>
            {progress && (
              <div className="h-2 overflow-hidden rounded-full bg-[#dad6cf]">
                <div
                  className="h-full bg-[#aa7435] transition-all duration-200"
                  style={{ width: `${pct}%` }}
                />
              </div>
            )}
            <pre className="max-h-28 overflow-y-auto rounded-lg border border-[#dedbd5] bg-[#1e1d1b] p-2.5 font-mono text-[11px] leading-5 text-[#eae6df]">
              {logLines.join("\n")}
            </pre>
          </div>
        )}

        {statusMessage && (
          <p
            role="status"
            className="rounded-lg border border-[#3e7d47]/40 bg-[#eef7ef] px-4 py-2.5 text-[13px] text-[#225129]"
          >
            {statusMessage}
          </p>
        )}
        {error && (
          <p
            role="alert"
            className="rounded-lg border border-[#aa3e36]/40 bg-[#fbeeed] px-4 py-2.5 text-[13px] text-[#aa3e36]"
          >
            {error}
          </p>
        )}

        {loading && !catalog ? (
          <div className="installer-well rounded-xl p-8 text-center text-[13px] text-body">
            Fetching GitHub catalog…
          </div>
        ) : catalog ? (
          <>
            <div className="flex flex-wrap items-center justify-between gap-3 border-b border-[#dad6cf] pb-3">
              <div className="flex flex-wrap gap-2">
                <button
                  type="button"
                  onClick={() => setTab("updates")}
                  className={`cursor-pointer rounded-lg px-3.5 py-1.5 text-[13px] font-medium transition ${
                    tab === "updates"
                      ? "bg-[#aa7435] text-white"
                      : updates.length > 0
                        ? "border border-[#bd8b3d] bg-[#fdf7ec] text-[#704915]"
                        : "border border-[#cbc8c2] bg-white text-ink"
                  }`}
                >
                  Updates &amp; news{updates.length > 0 ? ` (${updates.length})` : ""}
                </button>
                <button
                  type="button"
                  onClick={() => setTab("extensions")}
                  className={`cursor-pointer rounded-lg px-3.5 py-1.5 text-[13px] font-medium transition ${
                    tab === "extensions"
                      ? "bg-[#aa7435] text-white"
                      : "border border-[#cbc8c2] bg-white text-ink"
                  }`}
                >
                  Extensions ({catalog.extensions.length})
                </button>
                <button
                  type="button"
                  onClick={() => setTab("games")}
                  className={`cursor-pointer rounded-lg px-3.5 py-1.5 text-[13px] font-medium transition ${
                    tab === "games"
                      ? "bg-[#aa7435] text-white"
                      : "border border-[#cbc8c2] bg-white text-ink"
                  }`}
                >
                  Games ({catalog.games.length})
                </button>
                <button
                  type="button"
                  onClick={() => setTab("apps")}
                  className={`cursor-pointer rounded-lg px-3.5 py-1.5 text-[13px] font-medium transition ${
                    tab === "apps"
                      ? "bg-[#aa7435] text-white"
                      : "border border-[#cbc8c2] bg-white text-ink"
                  }`}
                >
                  Apps ({catalog.apps.length})
                </button>
              </div>

              {tab === "extensions" && (
                <div className="flex flex-wrap items-center gap-2">
                  <select
                    aria-label="Filter by install status"
                    value={statusFilter}
                    onChange={(e) =>
                      setStatusFilter(e.target.value as "all" | "installed" | "available")
                    }
                    className="rounded-lg border border-[#c2bfba] bg-white px-2.5 py-1.5 text-[12px] text-ink"
                  >
                    <option value="all">All statuses</option>
                    <option value="installed">Installed only</option>
                    <option value="available">Not installed</option>
                  </select>
                  <input
                    type="search"
                    value={query}
                    onChange={(event) => setQuery(event.target.value)}
                    placeholder="Search name or author..."
                    aria-label="Search store extensions"
                    className="w-52 rounded-lg border border-[#c2bfba] bg-white px-3 py-1.5 text-[12px] text-ink outline-none focus:border-focus"
                  />
                </div>
              )}
            </div>

            {tab === "updates" && (
              <div className="space-y-4">
                {scan && updates.length > 0 ? (
                  <section className="rounded-xl border border-[#bd8b3d] bg-[#fdf7ec] p-4">
                    <div className="flex flex-wrap items-center justify-between gap-3">
                      <div>
                        <h2 className="text-[14px] font-bold text-ink">
                          {updates.length} package update(s) ready
                        </h2>
                        <p className="text-[12px] text-body">
                          Older versions or mismatched hashes found on your iPod.
                        </p>
                      </div>
                      <Button.Root
                        size="compact"
                        disabled={busy}
                        onClick={() =>
                          void runActions(
                            buildUpdateAllActions(catalog, scan),
                            `Updating ${updates.length} packages`,
                          )
                        }
                      >
                        <Button.Label>Update all ({updates.length})</Button.Label>
                      </Button.Root>
                    </div>
                    <div className="mt-3 grid gap-2 sm:grid-cols-2">
                      {updates.map((u) => (
                        <div
                          key={`${u.kind}-${u.id}`}
                          className="rounded-lg border border-[#e6d5b8] bg-white p-3 text-[12px]"
                        >
                          <div className="flex items-center justify-between gap-2">
                            <strong className="text-ink">{u.name}</strong>
                            <span className="rounded bg-[#f6ebd8] px-2 py-0.5 font-medium text-[#704915]">
                              {u.installedVersion} -&gt; {u.latestVersion}
                            </span>
                          </div>
                          <p className="mt-1 text-body">{u.changelog}</p>
                        </div>
                      ))}
                    </div>
                  </section>
                ) : (
                  <div className="installer-well rounded-xl p-4 text-[13px] text-body">
                    {scan
                      ? "All installed extensions, games, and apps on this iPod match the latest catalog."
                      : "Connect an iPod in Disk Mode to check installed package versions."}
                  </div>
                )}

                <section className="installer-well rounded-xl p-4">
                  <h2 className="text-[13px] font-bold text-ink">What is new in RevisionOS</h2>
                  <div className="mt-2.5 grid gap-2.5 sm:grid-cols-2">
                    {catalog.whats_new.map((entry) => (
                      <div
                        key={entry.id}
                        className="rounded-lg border border-[#dedbd5] bg-white/90 p-3 text-[12px]"
                      >
                        <div className="flex items-center justify-between gap-2">
                          <p className="font-semibold text-ink">{entry.title}</p>
                          <span className="text-[11px] text-muted">v{entry.version}</span>
                        </div>
                        <p className="mt-1 text-body">{entry.summary}</p>
                      </div>
                    ))}
                  </div>
                  <p className="mt-3 text-[11px] leading-4 text-muted">
                    {catalog.backbone_disclaimer} {catalog.custom_build_notice}
                  </p>
                </section>
              </div>
            )}

            {tab === "extensions" && (
              <div className="space-y-3">
                <div className="flex flex-wrap gap-1.5">
                  {categoryTabs.map((c) => (
                    <button
                      key={c.id}
                      type="button"
                      onClick={() => setCategory(c.id)}
                      className={`cursor-pointer rounded-full border px-3 py-1 text-[12px] transition ${
                        category === c.id
                          ? "border-[#aa7435] bg-[#aa7435] font-medium text-white"
                          : "border-[#cbc8c2] bg-white text-body hover:text-ink"
                      }`}
                    >
                      {c.label}
                    </button>
                  ))}
                </div>

                <div className="grid max-h-[26rem] gap-3 overflow-y-auto pr-1 sm:grid-cols-2">
                  {visibleExtensions.length === 0 ? (
                    <div className="installer-well col-span-full rounded-xl p-6 text-center text-[13px] text-muted">
                      No extensions match your filters.
                    </div>
                  ) : (
                    visibleExtensions.map((ext) => {
                      const st = getExtensionStatus(ext, scan);
                      const compat = !catalog || isApiCompatible(ext.api, catalog.firmware.api);
                      return (
                        <article
                          key={ext.id}
                          className="flex flex-col justify-between gap-3 rounded-xl border border-[#d6d2cb] bg-white p-3.5 shadow-xs"
                        >
                          <div>
                            <div className="flex flex-wrap items-start justify-between gap-2">
                              <div>
                                <div className="flex flex-wrap items-center gap-1.5">
                                  <h3 className="text-[13px] font-bold text-ink">{ext.name}</h3>
                                  {ext.source_repo && (
                                    <span className="rounded bg-[#eef4fb] px-1.5 py-0.5 text-[10px] font-medium text-[#22507d]">
                                      {ext.source_repo}
                                    </span>
                                  )}
                                </div>
                                <p className="text-[11px] text-muted">
                                  v{ext.version} · {ext.author} · {formatBytes(ext.bytes)}
                                </p>
                              </div>
                              <span className="rounded bg-[#eeedea] px-2 py-0.5 text-[11px] capitalize text-body">
                                {ext.type}
                              </span>
                            </div>
                            <p className="mt-2 text-[12px] leading-5 text-body">
                              {ext.description}
                            </p>
                          </div>

                          <div className="flex items-center justify-between gap-2 border-t border-[#efece6] pt-2.5">
                            {!compat ? (
                              <>
                                <span className="text-[11px] font-medium text-[#aa3e36]">
                                  Requires API {ext.api}
                                </span>
                                <Button.Root size="compact" disabled>
                                  <Button.Label>Incompatible</Button.Label>
                                </Button.Root>
                              </>
                            ) : st.status === "update_available" ? (
                              <>
                                <span className="text-[11px] font-medium text-[#704915]">
                                  v{st.installed} -&gt; v{st.latest}
                                </span>
                                <Button.Root
                                  size="compact"
                                  disabled={busy || !scan}
                                  onClick={() =>
                                    void runActions(
                                      [{ action: "update_extension", id: ext.id }],
                                      `Updating ${ext.name}`,
                                    )
                                  }
                                >
                                  <Button.Label>Update</Button.Label>
                                </Button.Root>
                              </>
                            ) : st.status === "up_to_date" ? (
                              <>
                                <span className="text-[11px] font-semibold text-[#225129]">
                                  Installed · v{st.installed}
                                </span>
                                <Button.Root
                                  size="compact"
                                  disabled={busy || !scan}
                                  onClick={() =>
                                    void runActions(
                                      [{ action: "remove_extension", id: ext.id }],
                                      `Removing ${ext.name}`,
                                    )
                                  }
                                >
                                  <Button.Label>Remove</Button.Label>
                                </Button.Root>
                              </>
                            ) : (
                              <>
                                <span className="text-[11px] text-muted">Not installed</span>
                                <Button.Root
                                  size="compact"
                                  disabled={busy || !scan}
                                  onClick={() =>
                                    void runActions(
                                      [{ action: "install_extension", id: ext.id }],
                                      `Installing ${ext.name}`,
                                    )
                                  }
                                >
                                  <Button.Label>Install</Button.Label>
                                </Button.Root>
                              </>
                            )}
                          </div>
                        </article>
                      );
                    })
                  )}
                </div>
              </div>
            )}

            {(tab === "games" || tab === "apps") && (
              <div className="space-y-3">
                {tab === "games" && scan && scan.apple_games_count > 0 && (
                  <p className="rounded-lg border border-[#dedbd5] bg-white/80 px-3.5 py-2 text-[12px] text-body">
                    {scan.apple_games_count} official Apple Click Wheel archival game(s) in{" "}
                    <code className="text-ink">/iPod_Control/Games_RO/</code> are managed via iTunes
                    and kept untouched.
                  </p>
                )}
                <div className="grid gap-3 sm:grid-cols-2">
                  {(tab === "games" ? catalog.games : catalog.apps).map((item) => {
                    const kind = tab === "games" ? "game" : "app";
                    const st = getBundleStatus(item, scan, kind);
                    return (
                      <article
                        key={item.id}
                        className="flex flex-col justify-between gap-3 rounded-xl border border-[#d6d2cb] bg-white p-4 shadow-xs"
                      >
                        <div>
                          <div className="flex items-start justify-between gap-2">
                            <div>
                              <h3 className="text-[14px] font-bold text-ink">{item.name}</h3>
                              <p className="text-[11px] text-muted">
                                v{item.version} · {item.author} · {formatBytes(item.total_bytes)}
                              </p>
                            </div>
                          </div>
                          <p className="mt-2 text-[12px] leading-5 text-body">{item.description}</p>
                        </div>

                        <div className="flex items-center justify-between gap-2 border-t border-[#efece6] pt-2.5">
                          {st.status === "update_available" ? (
                            <>
                              <span className="text-[11px] font-medium text-[#704915]">
                                v{st.installed} -&gt; v{st.latest}
                              </span>
                              <Button.Root
                                size="compact"
                                disabled={busy || !scan}
                                onClick={() =>
                                  void runActions(
                                    [
                                      kind === "game"
                                        ? { action: "update_game", id: item.id }
                                        : { action: "update_app", id: item.id },
                                    ],
                                    `Updating ${item.name}`,
                                  )
                                }
                              >
                                <Button.Label>Update</Button.Label>
                              </Button.Root>
                            </>
                          ) : st.status === "up_to_date" ? (
                            <>
                              <span className="text-[11px] font-semibold text-[#225129]">
                                Installed · v{st.installed}
                              </span>
                              <Button.Root
                                size="compact"
                                disabled={busy || !scan}
                                onClick={() =>
                                  void runActions(
                                    [
                                      kind === "game"
                                        ? { action: "remove_game", id: item.id }
                                        : { action: "remove_app", id: item.id },
                                    ],
                                    `Removing ${item.name}`,
                                  )
                                }
                              >
                                <Button.Label>Remove</Button.Label>
                              </Button.Root>
                            </>
                          ) : (
                            <>
                              <span className="text-[11px] text-muted">Not installed</span>
                              <Button.Root
                                size="compact"
                                disabled={busy || !scan}
                                onClick={() =>
                                  void runActions(
                                    [
                                      kind === "game"
                                        ? { action: "install_game", id: item.id }
                                        : { action: "install_app", id: item.id },
                                    ],
                                    `Installing ${item.name}`,
                                  )
                                }
                              >
                                <Button.Label>Install</Button.Label>
                              </Button.Root>
                            </>
                          )}
                        </div>
                      </article>
                    );
                  })}
                </div>
              </div>
            )}
          </>
        ) : null}
      </div>

      <InstallerScreen.Actions className="mt-6">
        <Button.Root disabled={busy} onClick={onBack}>
          <Button.Label>Back</Button.Label>
        </Button.Root>
      </InstallerScreen.Actions>
    </InstallerScreen.Root>
  );
};

export { StoreScreen };
