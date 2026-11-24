import type { Backend, EventChannel } from "../lib/backend.ts";
import type { IpodScan, StoreAction } from "../lib/storeLogic.ts";
import { simulateChecks, type Wait } from "./checks.ts";
import {
  mockCatalog,
  mockDevice,
  mockDriveSummary,
  mockFirmware,
  mockIpodScan,
  mockPackage,
  mockRepriseScan,
} from "./fixtures.ts";
import { simulateInstallation, simulateUpdate } from "./progress.ts";
import type { Scenario } from "./scenarios.ts";

function channel<T>(args: Record<string, unknown>, name = "onEvent"): EventChannel<T> {
  const value = args[name] as EventChannel<T> | undefined;
  if (typeof value?.onmessage !== "function") throw new Error(`Missing mock channel: ${name}`);
  return value;
}

export function createMockBackend(
  scenario: Scenario,
  wait: Wait = () => new Promise((resolve) => setTimeout(resolve, 250)),
  saveDebug?: (sessionLog: string) => Promise<string | null>,
): Backend {
  let cancelled = false;
  let checkId = 0;
  let overrideAvailable = false;
  let usbReady = !scenario.usbSetup;
  let usbAttempts = 0;
  let simScan: IpodScan = structuredClone(
    scenario.id === "migrate" ? mockRepriseScan : mockIpodScan,
  );

  async function handle(command: string, args: Record<string, unknown>) {
    switch (command) {
      case "usb_permissions_ready":
        if (scenario.usbSetup === "checking") return new Promise<never>(() => {});
        if (scenario.usbSetup === "check-error" && !usbReady)
          throw new Error("Could not check USB access setup.");
        return usbReady;
      case "enable_usb_access":
        usbAttempts += 1;
        if (scenario.usbSetup === "waiting") return new Promise<never>(() => {});
        await wait();
        if (scenario.usbSetup === "unavailable") {
          throw new Error(
            "Administrator authentication is unavailable or was denied. Make sure polkit and a desktop authentication agent are installed and running, then try again.",
          );
        }
        if (scenario.usbSetup === "denied" && usbAttempts === 1)
          throw new Error(
            "Administrator authentication was cancelled. Try again to enable iPod access.",
          );
        usbReady = true;
        return;
      case "discover_devices":
        if (scenario.discovery === "denied")
          throw new Error("USB discovery failed: access denied.");
        if (scenario.discovery === "absent") return [];
        if (scenario.discovery === "normal") return [{ ...mockDevice, dfu_candidate: false }];
        if (scenario.discovery === "multiple")
          return [mockDevice, { ...mockDevice, selector: { bus: 1, address: 2 } }];
        return [mockDevice];
      case "check_device": {
        const approved = args.allowUnknownHardware !== undefined;
        const eligible = overrideAvailable && args.allowUnknownHardware === checkId;
        overrideAvailable = false;
        checkId += 1;
        if (approved && !eligible)
          throw new Error("Device or package changed. Run device checks again.");
        const report = await simulateChecks(
          approved ? { ...scenario, checkFailure: undefined, issue: undefined } : scenario,
          channel(args),
          wait,
        );
        if (approved && report.identity) {
          report.identity.model = scenario.checkFailure?.model ?? report.identity.model;
          const model = report.checks.find((check) => check.id === "model");
          if (model) model.detail = "Experimental hardware override accepted";
        }
        overrideAvailable =
          report.issue?.kind === "unknown_hardware" && report.cleanup.state === "idle";
        return { ...report, check_id: checkId, hardware_override: approved };
      }
      case "cached_firmware":
        return null;
      case "choose_firmware":
        channel<string>(args, "onSelected").onmessage(mockFirmware.filename);
        await wait();
        if (scenario.firmwareError) throw new Error(scenario.firmwareError);
        return mockFirmware;
      case "choose_local_package":
      case "prepare_package":
        overrideAvailable = false;
        await wait();
        if (scenario.packageError) throw new Error(scenario.packageError);
        return {
          ...mockPackage,
          filename: command === "choose_local_package" ? "repriseos.zip" : null,
        };
      case "install":
        cancelled = false;
        return simulateInstallation(scenario, channel(args), wait, () => cancelled);
      case "cancel_install":
        cancelled = true;
        return;
      case "save_debug_zip":
        if (scenario.debugExport === "cancel") return null;
        if (scenario.debugExport === "error") throw new Error("Destination is not writable.");
        if (saveDebug) return saveDebug(String(args.sessionLog ?? ""));
        throw new Error("Native debug export is unavailable in this preview.");
      case "check_installer_update":
        await wait();
        if (scenario.update === "check-error")
          throw new Error(
            "Could not check for updates. Check your internet connection and try again.",
          );
        return {
          current_version: "0.1.1",
          version: scenario.update && scenario.update !== "current" ? "0.1.2" : null,
          mode: scenario.update === "manual" ? "manual" : "automatic",
        };
      case "install_installer_update":
        return simulateUpdate(scenario, channel(args), wait);
      case "plugin:opener|open_url":
      case "open_installer_download":
      case "open_backups_folder":
        return;
      case "fetch_revision_catalog":
        await wait();
        return mockCatalog;
      case "cached_firmware_status":
        return { has_ipsw: false, has_decrypted_osos: false };
      case "cancel_store_task":
        cancelled = true;
        return;
      case "detect_ipod_drives":
        return [{
          ...mockDriveSummary,
          layout: simScan.layout,
          has_revola_bin: simScan.has_revola_bin,
          has_loader_bin: simScan.has_loader_bin,
          has_reprise_osos: simScan.has_reprise_osos,
          has_root_revola_bin: simScan.has_root_revola_bin,
          reprise_osos_matches_revola: simScan.reprise_osos_matches_revola,
          migration_recommended: simScan.migration_recommended,
          extensions_count: simScan.extensions.length,
          games_count: simScan.games.length,
          reprise_games_count: simScan.reprise_games.length,
          apple_games_count: simScan.apple_games_count,
          apps_count: simScan.apps.length,
        }];
      case "migrate_ipod_drive": {
        channel<{ step: string; detail: string; completed: number; total: number }>(args).onmessage({
          step: "scan",
          detail: "Scanning iPod storage and filtering Apple Click Wheel archival games",
          completed: 1,
          total: 5,
        });
        await wait();
        const prevLayout = simScan.layout;
        const copiedGames = simScan.reprise_games.map((g) => g.folder);
        for (const g of simScan.reprise_games) {
          if (!simScan.games.some((x) => x.id === g.id)) simScan.games.push(g);
        }
        simScan.layout = "reprise_bootloader_with_revision_os";
        simScan.has_revola_bin = true;
        simScan.reprise_osos_matches_revola = true;
        simScan.migration_recommended = false;
        return {
          source_layout: prevLayout,
          migrated_games: copiedGames,
          updated_extensions: [],
          installed_homebrew: copiedGames.length > 0,
          firmware_updated: false,
          cleaned_legacy_files: [],
          notes: [
            "Kept /osos-cfw.bin and /cfw-loader.bin for the RepriseOS bootloader.",
            `Kept ${simScan.apple_games_count} Apple Click Wheel archival game(s) untouched in /iPod_Control/Games_RO/.`,
          ],
        };
      }
      case "pick_ipod_drive":
        return mockDriveSummary;
      case "scan_ipod_drive":
        return simScan;
      case "apply_store_changes": {
        cancelled = false;
        const actions = (args.actions as StoreAction[] | undefined) ?? [];
        const ch = channel<{ step: string; detail: string; completed: number; total: number }>(args);
        for (let i = 0; i < actions.length; i += 1) {
          if (cancelled) throw new Error("Operation cancelled by user");
          const a = actions[i];
          ch.onmessage({ step: a.action, detail: `${a.action} (${a.id})`, completed: i, total: actions.length });
          await wait();
          if (a.action === "install_extension" || a.action === "update_extension") {
            const ext = mockCatalog.extensions.find((e) => e.id === a.id);
            if (ext) {
              simScan.extensions = simScan.extensions.filter((e) => e.id !== a.id);
              simScan.extensions.push({
                id: ext.id,
                name: ext.name,
                version: ext.version,
                author: ext.author,
                extension_type: ext.type,
                stability: ext.stability,
                file_name: `${ext.id}.rvx`,
                bytes: ext.bytes,
                sha256: ext.sha256,
              });
            }
          } else if (a.action === "remove_extension") {
            simScan.extensions = simScan.extensions.filter((e) => e.id !== a.id);
          } else if (a.action === "install_game" || a.action === "update_game") {
            const game = mockCatalog.games.find((g) => g.id === a.id);
            if (game) {
              simScan.games = simScan.games.filter((g) => g.id !== a.id);
              simScan.games.push({
                id: game.id,
                folder: game.folder,
                kind: "game",
                name: game.name,
                version: game.version,
                total_bytes: game.total_bytes,
              });
            }
          } else if (a.action === "remove_game") {
            simScan.games = simScan.games.filter((g) => g.id !== a.id);
          } else if (a.action === "install_app" || a.action === "update_app") {
            const app = mockCatalog.apps.find((x) => x.id === a.id);
            if (app) {
              simScan.apps = simScan.apps.filter((x) => x.id !== a.id);
              simScan.apps.push({
                id: app.id,
                folder: app.folder,
                kind: "app",
                name: app.name,
                version: app.version,
                total_bytes: app.total_bytes,
              });
            }
          } else if (a.action === "remove_app") {
            simScan.apps = simScan.apps.filter((x) => x.id !== a.id);
          }
        }
        return {
          installed_extensions: [],
          removed_extensions: [],
          installed_games: [],
          removed_games: [],
          installed_apps: [],
          removed_apps: [],
        };
      }
      case "disk_mode_upgrade_firmware":
        await wait();
        simScan.has_revola_bin = true;
        return;
      case "disk_mode_uninstall":
        await wait();
        simScan.extensions = [];
        simScan.games = [];
        simScan.apps = [];
        simScan.has_revola_bin = false;
        return {
          removed_paths: ["/Revola/Firmware", "/Revola/Extensions", "/Revola/Games"],
          kept_user_data: Boolean(args.keepUserData),
        };
      case "list_nor_backups":
        return [
          {
            device_dir: "MC297-mock",
            sha256: "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            path: "/mock/nor/0123456789abcdef/nor.bin",
            bytes: 1048576,
          },
        ];
      default:
        throw new Error(`Unimplemented simulated command: ${command}`);
    }
  }

  return {
    invoke: async <T>(command: string, args: Record<string, unknown> = {}) =>
      (await handle(command, args)) as T,
    createChannel: <T>() => ({ onmessage: (_message: T) => undefined }),
  };
}
