import React from "react";
import { useHeadingFocus } from "../hooks/useHeadingFocus";
import { errorDetail } from "../lib/errors";
import { detectIpodDrives, fetchRevisionCatalog, pickIpodDrive, scanIpodDrive } from "../lib/store";
import {
  type Catalog,
  type ContentSelection,
  type ExtensionType,
  computePresetSelection,
  ensureHomebrewRequirement,
  filterExtensions,
  formatBytes,
  importSelectionFromScan,
} from "../lib/storeLogic";
import { Button } from "./ui/Button";
import { InstallerScreen } from "./ui/InstallerScreen";

interface IContentSelectionScreenProps {
  catalog: Catalog | null;
  onCatalogLoaded: (catalog: Catalog) => void;
  selection: ContentSelection;
  onChange: (next: ContentSelection) => void;
  onContinue: () => void;
  onBack: () => void;
}

const categoryFilters: { id: "all" | ExtensionType; label: string }[] = [
  { id: "all", label: "All" },
  { id: "audio", label: "Audio" },
  { id: "customization", label: "Customization" },
  { id: "media", label: "Media" },
  { id: "utility", label: "Utility" },
  { id: "connectivity", label: "Connectivity" },
  { id: "developer", label: "Developer" },
];

const ContentSelectionScreen: React.FC<IContentSelectionScreenProps> = ({
  catalog,
  onCatalogLoaded,
  selection,
  onChange,
  onContinue,
  onBack,
}) => {
  const { headingRef } = useHeadingFocus();
  const [loading, setLoading] = React.useState(!catalog);
  const [error, setError] = React.useState("");
  const [category, setCategory] = React.useState<"all" | ExtensionType>("all");
  const [query, setQuery] = React.useState("");
  const [homebrewNotice, setHomebrewNotice] = React.useState(false);
  const [importMessage, setImportMessage] = React.useState("");

  const handleImportFromIpod = async () => {
    if (!catalog) return;
    setImportMessage("");
    try {
      const drives = await detectIpodDrives();
      const target = drives[0] ?? (await pickIpodDrive());
      if (!target) return;
      const scan = await scanIpodDrive(target.root);
      const imported = importSelectionFromScan(catalog, scan);
      onChange(imported);
      setImportMessage(
        `Imported from ${target.root}: ${imported.extensions.length} extensions, ${imported.games.length} games, ${imported.apps.length} apps.`,
      );
    } catch (err) {
      setError(errorDetail(err));
    }
  };

  const load = React.useCallback(async () => {
    setLoading(true);
    setError("");
    try {
      const next = await fetchRevisionCatalog();
      onCatalogLoaded(next);
      if (
        selection.extensions.length === 0 &&
        selection.games.length === 0 &&
        selection.apps.length === 0
      ) {
        onChange(computePresetSelection(next, "recommended"));
      }
    } catch (err) {
      setError(errorDetail(err));
    } finally {
      setLoading(false);
    }
  }, [onCatalogLoaded, onChange, selection.apps.length, selection.extensions.length, selection.games.length]);

  React.useEffect(() => {
    if (!catalog) void load();
  }, [catalog, load]);

  const applySelection = (raw: ContentSelection) => {
    const { selection: normalized, autoAddedHomebrew } = ensureHomebrewRequirement(raw);
    if (autoAddedHomebrew) setHomebrewNotice(true);
    onChange(normalized);
  };

  const toggleExtension = (id: string) => {
    const has = selection.extensions.includes(id);
    if (has && id === "homebrew" && (selection.games.length > 0 || selection.apps.length > 0)) {
      setHomebrewNotice(true);
      return;
    }
    const extensions = has
      ? selection.extensions.filter((item) => item !== id)
      : [...selection.extensions, id];
    setHomebrewNotice(false);
    applySelection({ ...selection, extensions });
  };

  const toggleGame = (id: string) => {
    const has = selection.games.includes(id);
    const games = has ? selection.games.filter((g) => g !== id) : [...selection.games, id];
    applySelection({ ...selection, games });
  };

  const toggleApp = (id: string) => {
    const has = selection.apps.includes(id);
    const apps = has ? selection.apps.filter((a) => a !== id) : [...selection.apps, id];
    applySelection({ ...selection, apps });
  };

  const visibleExtensions = catalog ? filterExtensions(catalog.extensions, category, query) : [];

  return (
    <InstallerScreen.Root aria-labelledby="content-title">
      <InstallerScreen.Title id="content-title" ref={headingRef} tabIndex={-1}>
        Choose extensions and homebrew.
      </InstallerScreen.Title>
      <InstallerScreen.Description className="mt-3 max-w-145 text-[14px] leading-6">
        Pick which extensions, games, and apps to install onto your iPod. You can change or update
        them later in Store and updates.
      </InstallerScreen.Description>

      {loading ? (
        <div className="installer-well mx-auto mt-6 max-w-145 rounded-lg p-6 text-[13px] text-body">
          Loading catalog from GitHub…
        </div>
      ) : error ? (
        <div className="installer-well mx-auto mt-6 max-w-145 space-y-3 rounded-lg p-5 text-left text-[13px]">
          <p className="text-[#aa3e36]">{error}</p>
          <p className="text-body">
            You can retry fetching the GitHub catalog or continue with core firmware only.
          </p>
          <div className="flex flex-wrap gap-2">
            <Button.Root size="compact" onClick={() => void load()}>
              <Button.Label>Retry catalog</Button.Label>
            </Button.Root>
          </div>
        </div>
      ) : catalog ? (
        <div className="mx-auto mt-6 max-w-150 space-y-5 text-left">
          <div className="flex flex-wrap items-center justify-between gap-3">
            <div className="flex flex-wrap gap-2">
              {(["recommended", "minimal", "everything"] as const).map((preset) => (
                <Button.Root
                  key={preset}
                  size="compact"
                  onClick={() => {
                    setHomebrewNotice(false);
                    onChange(computePresetSelection(catalog, preset));
                  }}
                >
                  <Button.Label>
                    {preset === "recommended"
                      ? "Recommended"
                      : preset === "minimal"
                        ? "Minimal (core only)"
                        : "Select all"}
                  </Button.Label>
                </Button.Root>
              ))}
              <Button.Root size="compact" onClick={() => void handleImportFromIpod()}>
                <Button.Label>Import from iPod</Button.Label>
              </Button.Root>
            </div>
            <input
              type="search"
              value={query}
              onChange={(event) => setQuery(event.target.value)}
              placeholder="Filter extensions..."
              aria-label="Filter extensions"
              className="w-56 rounded-lg border border-[#c2bfba] bg-white px-3 py-1.5 text-[13px] text-ink outline-none focus:border-focus"
            />
          </div>

          <div className="flex flex-wrap gap-1.5" role="group" aria-label="Extension categories">
            {categoryFilters.map((filter) => {
              const active = category === filter.id;
              return (
                <button
                  key={filter.id}
                  type="button"
                  onClick={() => setCategory(filter.id)}
                  className={`cursor-pointer rounded-full border px-3 py-1 text-[12px] transition ${
                    active
                      ? "border-[#aa7435] bg-[#aa7435] font-medium text-white"
                      : "border-[#cbc8c2] bg-white/80 text-body hover:text-ink"
                  }`}
                >
                  {filter.label}
                </button>
              );
            })}
          </div>

          {homebrewNotice && (
            <p className="rounded-lg border border-[#bd8b3d]/50 bg-[#fdf7ec] px-3 py-2 text-[12px] text-ink">
              Homebrew (`homebrew.rvx`) stays enabled whenever games or apps are selected so Extras
              &gt; Games and Apps can launch them.
            </p>
          )}
          {importMessage && (
            <p className="rounded-lg border border-[#3e7d47]/40 bg-[#eef7ef] px-3 py-2 text-[12px] text-[#225129]">
              {importMessage}
            </p>
          )}

          <div className="installer-well max-h-72 space-y-2 overflow-y-auto rounded-lg p-3">
            {visibleExtensions.length === 0 ? (
              <p className="py-4 text-center text-[13px] text-muted">
                No extensions match the current filter.
              </p>
            ) : (
              visibleExtensions.map((ext) => {
                const checked = selection.extensions.includes(ext.id);
                return (
                  <label
                    key={ext.id}
                    className="flex cursor-pointer items-start gap-3 rounded-md border border-transparent bg-white/75 p-3 hover:border-[#cbc8c2]"
                  >
                    <input
                      type="checkbox"
                      checked={checked}
                      onChange={() => toggleExtension(ext.id)}
                      className="mt-1 size-4 accent-[#aa7435]"
                    />
                    <div className="min-w-0 flex-1">
                      <div className="flex flex-wrap items-center gap-2">
                        <span className="text-[13px] font-semibold text-ink">{ext.name}</span>
                        <span className="rounded bg-[#eeedea] px-1.5 py-0.5 text-[11px] capitalize text-body">
                          {ext.type}
                        </span>
                        <span className="text-[11px] text-muted">
                          v{ext.version} by {ext.author}
                        </span>
                        {ext.requires_custom_core && (
                          <span className="rounded bg-[#f6ebd8] px-1.5 py-0.5 text-[11px] text-[#704915]">
                            Needs RevisionOS revola.bin
                          </span>
                        )}
                      </div>
                      <p className="mt-1 text-[12px] leading-5 text-body">{ext.description}</p>
                    </div>
                  </label>
                );
              })
            )}
          </div>

          <div className="grid gap-3 sm:grid-cols-2">
            <div className="installer-well rounded-lg p-3">
              <h2 className="mb-2 text-[13px] font-semibold text-ink">
                Games (/Revola/Games/)
              </h2>
              <div className="space-y-2">
                {catalog.games.map((game) => (
                  <label
                    key={game.id}
                    className="flex cursor-pointer items-start gap-2.5 rounded-md bg-white/75 p-2.5"
                  >
                    <input
                      type="checkbox"
                      checked={selection.games.includes(game.id)}
                      onChange={() => toggleGame(game.id)}
                      className="mt-1 size-4 accent-[#aa7435]"
                    />
                    <div className="min-w-0 flex-1">
                      <div className="flex items-center justify-between gap-2">
                        <span className="text-[13px] font-medium text-ink">{game.name}</span>
                        <span className="text-[11px] text-muted">
                          v{game.version} · {formatBytes(game.total_bytes)}
                        </span>
                      </div>
                      <p className="mt-0.5 text-[12px] text-body">{game.description}</p>
                    </div>
                  </label>
                ))}
              </div>
            </div>

            <div className="installer-well rounded-lg p-3">
              <h2 className="mb-2 text-[13px] font-semibold text-ink">Apps (/Revola/Apps/)</h2>
              <div className="space-y-2">
                {catalog.apps.map((app) => (
                  <label
                    key={app.id}
                    className="flex cursor-pointer items-start gap-2.5 rounded-md bg-white/75 p-2.5"
                  >
                    <input
                      type="checkbox"
                      checked={selection.apps.includes(app.id)}
                      onChange={() => toggleApp(app.id)}
                      className="mt-1 size-4 accent-[#aa7435]"
                    />
                    <div className="min-w-0 flex-1">
                      <div className="flex items-center justify-between gap-2">
                        <span className="text-[13px] font-medium text-ink">{app.name}</span>
                        <span className="text-[11px] text-muted">
                          v{app.version} · {formatBytes(app.total_bytes)}
                        </span>
                      </div>
                      <p className="mt-0.5 text-[12px] text-body">{app.description}</p>
                    </div>
                  </label>
                ))}
              </div>
            </div>
          </div>

          <p className="text-center text-[12px] text-muted">
            Selected: {selection.extensions.length} extensions, {selection.games.length} games,{" "}
            {selection.apps.length} apps.
          </p>
        </div>
      ) : null}

      <InstallerScreen.Actions className="mt-6">
        <Button.Root onClick={onBack}>
          <Button.Label>Back</Button.Label>
        </Button.Root>
        <Button.Root disabled={loading} onClick={onContinue}>
          <Button.Label>Continue</Button.Label>
        </Button.Root>
      </InstallerScreen.Actions>
    </InstallerScreen.Root>
  );
};

export { ContentSelectionScreen };

