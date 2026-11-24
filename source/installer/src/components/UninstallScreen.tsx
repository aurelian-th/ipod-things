import React from "react";
import { useHeadingFocus } from "../hooks/useHeadingFocus";
import { errorDetail } from "../lib/errors";
import {
  type NorBackupEntry,
  detectIpodDrives,
  diskModeUninstall,
  listNorBackups,
  openBackupsFolder,
  pickIpodDrive,
} from "../lib/store";
import type { IpodDriveSummary } from "../lib/storeLogic";
import { Button } from "./ui/Button";
import { InstallerScreen } from "./ui/InstallerScreen";

interface IUninstallScreenProps {
  onBack: () => void;
}

const UninstallScreen: React.FC<IUninstallScreenProps> = ({ onBack }) => {
  const { headingRef } = useHeadingFocus();
  const [drives, setDrives] = React.useState<IpodDriveSummary[]>([]);
  const [selectedRoot, setSelectedRoot] = React.useState("");
  const [keepUserData, setKeepUserData] = React.useState(true);
  const [backups, setBackups] = React.useState<NorBackupEntry[]>([]);
  const [busy, setBusy] = React.useState(false);
  const [result, setResult] = React.useState("");
  const [error, setError] = React.useState("");

  const load = React.useCallback(async () => {
    try {
      const [foundDrives, foundBackups] = await Promise.all([
        detectIpodDrives(),
        listNorBackups(),
      ]);
      setDrives(foundDrives);
      setBackups(foundBackups);
      if (!selectedRoot && foundDrives[0]) setSelectedRoot(foundDrives[0].root);
    } catch (err) {
      setError(errorDetail(err));
    }
  }, [selectedRoot]);

  React.useEffect(() => {
    void load();
  }, [load]);

  const handleUninstall = async () => {
    if (!selectedRoot) {
      setError("Connect your iPod in Disk Mode or select its storage folder first.");
      return;
    }
    setBusy(true);
    setError("");
    setResult("");
    try {
      const report = await diskModeUninstall(selectedRoot, keepUserData);
      const removed = report.removed_paths.length
        ? report.removed_paths.join(", ")
        : "no RevisionOS folders found";
      setResult(
        `Removed ${removed}. Apple music database (/iPod_Control/) was left untouched.`,
      );
      await load();
    } catch (err) {
      setError(errorDetail(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <InstallerScreen.Root aria-labelledby="uninstall-title">
      <InstallerScreen.Title id="uninstall-title" ref={headingRef} tabIndex={-1}>
        Uninstall or restore
      </InstallerScreen.Title>
      <InstallerScreen.Description className="mt-3 max-w-140 text-[14px] leading-6">
        Remove RevisionOS disk files safely in Disk Mode, inspect saved NOR backups, or restore
        Apple stock firmware.
      </InstallerScreen.Description>

      <div className="mx-auto mt-6 max-w-145 space-y-4 text-left">
        <section className="installer-well space-y-3 rounded-lg p-4">
          <h2 className="text-[14px] font-semibold text-ink">
            1. Remove RevisionOS files in Disk Mode
          </h2>
          <p className="text-[12px] text-body">
            Target drive: <strong className="text-ink">{selectedRoot || "None selected"}</strong>
          </p>
          <div className="flex flex-wrap gap-2">
            {drives.length > 1 && (
              <select
                value={selectedRoot}
                onChange={(event) => setSelectedRoot(event.target.value)}
                className="rounded-lg border border-[#c2bfba] bg-white px-2.5 py-1.5 text-[12px]"
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
              disabled={busy}
              onClick={async () => {
                const picked = await pickIpodDrive();
                if (picked) setSelectedRoot(picked.root);
              }}
            >
              <Button.Label>Choose iPod folder…</Button.Label>
            </Button.Root>
          </div>
          <label className="flex cursor-pointer items-center gap-2 text-[12px] text-body">
            <input
              type="checkbox"
              checked={keepUserData}
              onChange={(event) => setKeepUserData(event.target.checked)}
              className="size-4 accent-[#aa7435]"
            />
            Keep extension settings and game saves (/Revola/Data/)
          </label>
          <Button.Root
            intent="danger"
            size="compact"
            disabled={busy || !selectedRoot}
            onClick={() => void handleUninstall()}
          >
            <Button.Label>{busy ? "Removing…" : "Remove RevisionOS files"}</Button.Label>
          </Button.Root>
          {result && <p className="text-[12px] text-[#225129]">{result}</p>}
          {error && <p className="text-[12px] text-[#aa3e36]">{error}</p>}
        </section>

        <section className="installer-well space-y-2 rounded-lg p-4 text-[12px] leading-5 text-body">
          <div className="flex flex-wrap items-center justify-between gap-2">
            <h2 className="text-[14px] font-semibold text-ink">2. Saved NOR backups</h2>
            <Button.Root size="compact" onClick={() => void openBackupsFolder()}>
              <Button.Label>Open backups folder</Button.Label>
            </Button.Root>
          </div>
          {backups.length === 0 ? (
            <p>No local NOR backups found yet on this computer.</p>
          ) : (
            <ul className="space-y-1">
              {backups.map((b) => (
                <li key={b.path} className="break-all rounded bg-white/80 px-2.5 py-1.5">
                  {b.device_dir} · {b.sha256.slice(0, 12)}… ({b.bytes} bytes)
                </li>
              ))}
            </ul>
          )}
        </section>

        <section className="installer-well space-y-1.5 rounded-lg p-4 text-[12px] leading-5 text-body">
          <h2 className="text-[14px] font-semibold text-ink">
            3. Restore stock Apple bootloader and RetailOS
          </h2>
          <p>
            To remove the dual bootloader itself, hold Menu + Center until your iPod enters Disk
            Mode or DFU mode, connect it to iTunes (Windows) or Finder (macOS), and click{" "}
            <strong className="text-ink">Restore iPod</strong> with Apple 2.0.5 firmware.
          </p>
        </section>
      </div>

      <InstallerScreen.Actions className="mt-6">
        <Button.Root onClick={onBack}>
          <Button.Label>Back</Button.Label>
        </Button.Root>
      </InstallerScreen.Actions>
    </InstallerScreen.Root>
  );
};

export { UninstallScreen };

