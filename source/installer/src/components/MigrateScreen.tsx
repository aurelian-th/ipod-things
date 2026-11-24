import React from "react";
import { useHeadingFocus } from "../hooks/useHeadingFocus";
import { errorDetail } from "../lib/errors";
import {
  cancelStoreTask,
  detectIpodDrives,
  fetchRevisionCatalog,
  getCachedFirmwareStatus,
  migrateIpodDrive,
  pickIpodDrive,
  scanIpodDrive,
} from "../lib/store";
import {
  type CachedFirmwareStatus,
  type Catalog,
  type ContentSelection,
  type IpodDriveSummary,
  type IpodScan,
  type MigrationOptions,
  type MigrationReport,
  type ProgressEvent,
  describeDetectedLayout,
  importSelectionFromScan,
} from "../lib/storeLogic";
import { Button } from "./ui/Button";
import { InstallerScreen } from "./ui/InstallerScreen";

interface IMigrateScreenProps {
  onBack: () => void;
  onContinueToInstall: (imported: ContentSelection, catalog: Catalog | null) => void;
}

const MigrateScreen: React.FC<IMigrateScreenProps> = ({ onBack, onContinueToInstall }) => {
  const { headingRef } = useHeadingFocus();
  const [catalog, setCatalog] = React.useState<Catalog | null>(null);
  const [drives, setDrives] = React.useState<IpodDriveSummary[]>([]);
  const [selectedRoot, setSelectedRoot] = React.useState("");
  const [scan, setScan] = React.useState<IpodScan | null>(null);
  const [fwCache, setFwCache] = React.useState<CachedFirmwareStatus>({
    has_ipsw: false,
    has_decrypted_osos: false,
  });
  const [options, setOptions] = React.useState<MigrationOptions>({
    migrate_games_ro: true,
    cleanup_legacy_bins: true,
    upgrade_firmware: false,
    update_extensions: true,
  });
  const [loading, setLoading] = React.useState(true);
  const [busy, setBusy] = React.useState(false);
  const [progress, setProgress] = React.useState<ProgressEvent | null>(null);
  const [logLines, setLogLines] = React.useState<string[]>([]);
  const [report, setReport] = React.useState<MigrationReport | null>(null);
  const [error, setError] = React.useState("");

  const appendLog = React.useCallback((line: string) => {
    const time = new Date().toLocaleTimeString();
    setLogLines((prev) => [...prev.slice(-80), `[${time}] ${line}`]);
  }, []);

  const refresh = React.useCallback(
    async (preferredRoot?: string) => {
      setLoading(true);
      setError("");
      try {
        const [cat, found, cache] = await Promise.all([
          fetchRevisionCatalog(),
          detectIpodDrives(),
          getCachedFirmwareStatus(),
        ]);
        setCatalog(cat);
        setDrives(found);
        setFwCache(cache);
        setOptions((prev) => ({
          ...prev,
          upgrade_firmware: cache.has_decrypted_osos ? prev.upgrade_firmware : false,
        }));
        const nextRoot = preferredRoot || selectedRoot || found[0]?.root || "";
        setSelectedRoot(nextRoot);
        if (nextRoot) {
          setScan(await scanIpodDrive(nextRoot));
        } else {
          setScan(null);
        }
      } catch (err) {
        setError(errorDetail(err));
      } finally {
        setLoading(false);
      }
    },
    [selectedRoot],
  );

  React.useEffect(() => {
    void refresh();
  }, [refresh]);

  const handlePickDrive = async () => {
    try {
      const picked = await pickIpodDrive();
      if (picked) {
        setReport(null);
        await refresh(picked.root);
      }
    } catch (err) {
      setError(errorDetail(err));
    }
  };

  const runMigration = async () => {
    if (!selectedRoot) {
      setError("Connect your iPod in Disk Mode or select its storage folder first.");
      return;
    }
    setBusy(true);
    setError("");
    setReport(null);
    setLogLines([]);
    setProgress({ step: "start", detail: "Starting migration", completed: 0, total: 5 });
    appendLog(`Starting Disk Mode migration on ${selectedRoot}`);
    try {
      const res = await migrateIpodDrive(selectedRoot, options, (ev) => {
        setProgress(ev);
        appendLog(ev.detail);
      });
      setReport(res);
      appendLog("Migration complete.");
      setScan(await scanIpodDrive(selectedRoot));
    } catch (err) {
      const msg = errorDetail(err);
      setError(msg);
      appendLog(`Error: ${msg}`);
    } finally {
      setBusy(false);
    }
  };

  const importAndInstall = () => {
    if (!scan || !catalog) return;
    const imported = importSelectionFromScan(catalog, scan);
    onContinueToInstall(imported, catalog);
  };

  const layoutInfo = scan ? describeDetectedLayout(scan.layout) : null;
  const pct =
    progress && progress.total > 0
      ? Math.min(100, Math.round((progress.completed * 100) / progress.total))
      : 0;

  return (
    <InstallerScreen.Root aria-labelledby="migrate-title">
      <InstallerScreen.Title id="migrate-title" ref={headingRef} tabIndex={-1}>
        Migrate or check iPod layout
      </InstallerScreen.Title>
      <InstallerScreen.Description className="mt-2 max-w-165 text-[14px] leading-6">
        Inspect your iPod's firmware layout, copy unsigned RepriseOS homebrew into{" "}
        <code className="text-ink">/Revola/Games/</code> (leaving Apple archival Click Wheel games
        untouched), and import selections.
      </InstallerScreen.Description>

      <div className="mt-6 space-y-4 text-left">
        <div className="installer-well flex flex-wrap items-center justify-between gap-3 rounded-xl p-4">
          <div className="min-w-0 flex-1 text-[13px]">
            <p className="font-semibold text-ink">
              {selectedRoot ? `Target iPod: ${selectedRoot}` : "No Disk Mode iPod detected"}
            </p>
            <p className="mt-0.5 text-[12px] text-body">
              {layoutInfo
                ? layoutInfo.title
                : "Connect your iPod in Disk Mode or browse for its mounted storage folder."}
            </p>
          </div>
          <div className="flex flex-wrap gap-2">
            {drives.length > 1 && (
              <select
                value={selectedRoot}
                onChange={(event) => {
                  setReport(null);
                  void refresh(event.target.value);
                }}
                className="rounded-lg border border-[#c2bfba] bg-white px-2.5 py-1.5 text-[12px]"
              >
                {drives.map((d) => (
                  <option key={d.root} value={d.root}>
                    {d.label} ({d.root})
                  </option>
                ))}
              </select>
            )}
            <Button.Root size="compact" disabled={busy || loading} onClick={() => void refresh()}>
              <Button.Label>Refresh</Button.Label>
            </Button.Root>
            <Button.Root
              size="compact"
              disabled={busy || loading}
              onClick={() => void handlePickDrive()}
            >
              <Button.Label>Choose iPod folder…</Button.Label>
            </Button.Root>
          </div>
        </div>

        {error && (
          <p
            role="alert"
            className="rounded-lg border border-[#aa3e36]/40 bg-[#fbeeed] px-4 py-2.5 text-[13px] text-[#aa3e36]"
          >
            {error}
          </p>
        )}

        {scan && layoutInfo && (
          <div className="grid gap-4 lg:grid-cols-2">
            <section className="installer-well space-y-3 rounded-xl p-4 text-[12px] leading-5 text-body">
              <h2 className="text-[14px] font-semibold text-ink">{layoutInfo.title}</h2>
              <p>{layoutInfo.summary}</p>
              <div className="grid gap-2.5 sm:grid-cols-2">
                <div className="rounded-lg border border-[#dedbd5] bg-white/85 p-3">
                  <p className="font-semibold text-ink">Firmware files</p>
                  <ul className="mt-1 space-y-1">
                    <li>/Revola/Firmware/revola.bin: {scan.has_revola_bin ? "yes" : "no"}</li>
                    <li>/Revola/Firmware/loader.bin: {scan.has_loader_bin ? "yes" : "no"}</li>
                    <li>Legacy /revola.bin: {scan.has_root_revola_bin ? "yes" : "no"}</li>
                    <li>Root /osos-cfw.bin: {scan.has_reprise_osos ? "yes" : "no"}</li>
                  </ul>
                </div>
                <div className="rounded-lg border border-[#dedbd5] bg-white/85 p-3">
                  <p className="font-semibold text-ink">Content detected</p>
                  <ul className="mt-1 space-y-1">
                    <li>/Revola/Extensions/: {scan.extensions.length}</li>
                    <li>/Revola/Games/: {scan.games.length}</li>
                    <li>Unsigned Games_RO homebrew: {scan.reprise_games.length}</li>
                    <li>Apple Click Wheel archival games: {scan.apple_games_count}</li>
                    <li>/Revola/Apps/: {scan.apps.length}</li>
                  </ul>
                </div>
              </div>
              {scan.apple_games_count > 0 && (
                <p className="rounded-lg border border-[#dedbd5] bg-white/80 px-3 py-2 text-[11px] text-body">
                  {scan.apple_games_count} official Apple Click Wheel game(s) in{" "}
                  <code className="text-ink">/iPod_Control/Games_RO/</code> are never copied or
                  touched.
                </p>
              )}
            </section>

            <section className="installer-well space-y-3 rounded-xl p-4 text-[12px] text-body">
              <h2 className="text-[14px] font-semibold text-ink">Migration options</h2>
              <label className="flex cursor-pointer items-center gap-2.5">
                <input
                  type="checkbox"
                  checked={options.migrate_games_ro}
                  onChange={(e) => setOptions({ ...options, migrate_games_ro: e.target.checked })}
                  className="size-4 accent-[#aa7435]"
                />
                Copy unsigned homebrew games ({scan.reprise_games.length}) from{" "}
                <code>/iPod_Control/Games_RO/</code>
              </label>
              <label className="flex cursor-pointer items-center gap-2.5">
                <input
                  type="checkbox"
                  checked={options.update_extensions}
                  onChange={(e) =>
                    setOptions({ ...options, update_extensions: e.target.checked })
                  }
                  className="size-4 accent-[#aa7435]"
                />
                Update outdated extensions and ensure <code>homebrew.rvx</code> is installed
              </label>
              <label
                className={`flex items-start gap-2.5 ${fwCache.has_decrypted_osos ? "cursor-pointer" : "opacity-60"}`}
              >
                <input
                  type="checkbox"
                  disabled={!fwCache.has_decrypted_osos}
                  checked={fwCache.has_decrypted_osos && options.upgrade_firmware}
                  onChange={(e) => setOptions({ ...options, upgrade_firmware: e.target.checked })}
                  className="mt-0.5 size-4 accent-[#aa7435]"
                />
                <span>
                  Update RevisionOS <code>revola.bin</code> (and matching{" "}
                  <code>/osos-cfw.bin</code>)
                  {!fwCache.has_decrypted_osos && (
                    <span className="mt-0.5 block text-[11px] text-[#704915]">
                      Requires cached Apple 2.0.5 firmware (run Install or reapply RevisionOS in DFU
                      mode once to decrypt and cache it).
                    </span>
                  )}
                </span>
              </label>
              <label className="flex cursor-pointer items-center gap-2.5">
                <input
                  type="checkbox"
                  checked={options.cleanup_legacy_bins}
                  onChange={(e) =>
                    setOptions({ ...options, cleanup_legacy_bins: e.target.checked })
                  }
                  className="size-4 accent-[#aa7435]"
                />
                Clean up legacy root <code>/revola.bin</code> &amp; <code>/loader.bin</code>{" "}
                (keeps <code>/osos-cfw.bin</code> intact)
              </label>
              <div className="flex flex-wrap gap-2 pt-2">
                <Button.Root size="compact" disabled={busy} onClick={() => void runMigration()}>
                  <Button.Label>
                    {busy ? "Migrating…" : "Run Disk Mode migration"}
                  </Button.Label>
                </Button.Root>
                {busy && (
                  <Button.Root
                    size="compact"
                    intent="danger"
                    onClick={() => void cancelStoreTask()}
                  >
                    <Button.Label>Cancel</Button.Label>
                  </Button.Root>
                )}
                <Button.Root
                  size="compact"
                  disabled={busy || !catalog}
                  onClick={importAndInstall}
                >
                  <Button.Label>Import selection and run DFU install</Button.Label>
                </Button.Root>
              </div>
            </section>
          </div>
        )}

        {(busy || logLines.length > 0) && (
          <section className="installer-well space-y-2 rounded-xl p-4 text-[12px]">
            <div className="flex items-center justify-between gap-2">
              <span className="font-semibold text-ink">
                {progress ? progress.detail : "Migration log"}
              </span>
              {progress && <span className="tabular-nums text-muted">{pct}%</span>}
            </div>
            {progress && (
              <div className="h-2 overflow-hidden rounded-full bg-[#dad6cf]">
                <div
                  className="h-full bg-[#aa7435] transition-all duration-200"
                  style={{ width: `${pct}%` }}
                />
              </div>
            )}
            <pre className="max-h-36 overflow-y-auto rounded-lg border border-[#dedbd5] bg-[#1e1d1b] p-3 font-mono text-[11px] leading-5 text-[#eae6df]">
              {logLines.join("\n")}
            </pre>
          </section>
        )}

        {report && (
          <section className="rounded-xl border border-[#3e7d47]/40 bg-[#eef7ef] p-4 text-[12px] leading-5 text-[#225129]">
            <h2 className="text-[13px] font-bold">Migration complete</h2>
            <ul className="mt-1.5 space-y-1">
              <li>
                Copied homebrew games:{" "}
                {report.migrated_games.length ? report.migrated_games.join(", ") : "none"}
              </li>
              <li>
                Updated extensions:{" "}
                {report.updated_extensions.length
                  ? report.updated_extensions.join(", ")
                  : "none"}
              </li>
              <li>
                Installed Homebrew loader:{" "}
                {report.installed_homebrew ? "yes" : "already present or not needed"}
              </li>
              <li>Updated firmware: {report.firmware_updated ? "yes" : "skipped"}</li>
              <li>
                Cleaned legacy root files:{" "}
                {report.cleaned_legacy_files.length
                  ? report.cleaned_legacy_files.join(", ")
                  : "none"}
              </li>
            </ul>
            {report.notes.length > 0 && (
              <ul className="mt-2 list-disc pl-5 text-[#704915]">
                {report.notes.map((n) => (
                  <li key={n}>{n}</li>
                ))}
              </ul>
            )}
          </section>
        )}
      </div>

      <InstallerScreen.Actions className="mt-6">
        <Button.Root disabled={busy} onClick={onBack}>
          <Button.Label>Back</Button.Label>
        </Button.Root>
      </InstallerScreen.Actions>
    </InstallerScreen.Root>
  );
};

export { MigrateScreen };
