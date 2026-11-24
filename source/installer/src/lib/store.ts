import { backendAvailable, createChannel, invoke } from "./backend.ts";
import type {
  CachedFirmwareStatus,
  Catalog,
  IpodDriveSummary,
  IpodScan,
  MigrationOptions,
  MigrationReport,
  ProgressEvent,
  StoreAction,
} from "./storeLogic.ts";

export interface ApplyReport {
  installed_extensions: string[];
  removed_extensions: string[];
  installed_games: string[];
  removed_games: string[];
  installed_apps: string[];
  removed_apps: string[];
}

export interface UninstallReport {
  removed_paths: string[];
  kept_user_data: boolean;
}

export interface NorBackupEntry {
  device_dir: string;
  sha256: string;
  path: string;
  bytes: number;
}

export async function fetchRevisionCatalog(catalogUrl?: string): Promise<Catalog> {
  if (!backendAvailable()) {
    throw new Error("Open the desktop installer to fetch the catalog.");
  }
  return invoke<Catalog>("fetch_revision_catalog", {
    catalogUrl: catalogUrl ?? null,
  });
}

export async function detectIpodDrives(): Promise<IpodDriveSummary[]> {
  if (!backendAvailable()) return [];
  return invoke<IpodDriveSummary[]>("detect_ipod_drives");
}

export async function pickIpodDrive(): Promise<IpodDriveSummary | null> {
  if (!backendAvailable()) {
    throw new Error("Open the desktop installer to select an iPod drive.");
  }
  return invoke<IpodDriveSummary | null>("pick_ipod_drive");
}

export async function scanIpodDrive(root: string): Promise<IpodScan> {
  if (!backendAvailable()) {
    throw new Error("Open the desktop installer to inspect your iPod.");
  }
  return invoke<IpodScan>("scan_ipod_drive", { root });
}

export async function getCachedFirmwareStatus(): Promise<CachedFirmwareStatus> {
  if (!backendAvailable()) return { has_ipsw: false, has_decrypted_osos: false };
  return invoke<CachedFirmwareStatus>("cached_firmware_status");
}

export async function cancelStoreTask(): Promise<void> {
  if (!backendAvailable()) return;
  return invoke<void>("cancel_store_task");
}

export async function applyStoreChanges(
  root: string,
  actions: StoreAction[],
  onProgress?: (event: ProgressEvent) => void,
  catalogUrl?: string,
): Promise<ApplyReport> {
  if (!backendAvailable()) {
    throw new Error("Open the desktop installer to modify iPod files.");
  }
  const onEvent = createChannel<ProgressEvent>();
  onEvent.onmessage = (event) => onProgress?.(event);
  return invoke<ApplyReport>("apply_store_changes", {
    root,
    actions,
    catalogUrl: catalogUrl ?? null,
    onEvent,
  });
}

export async function diskModeUpgradeFirmware(
  root: string,
  catalogUrl?: string,
): Promise<void> {
  if (!backendAvailable()) {
    throw new Error("Open the desktop installer to upgrade core firmware.");
  }
  return invoke<void>("disk_mode_upgrade_firmware", {
    root,
    catalogUrl: catalogUrl ?? null,
  });
}

export async function diskModeUninstall(
  root: string,
  keepUserData: boolean,
): Promise<UninstallReport> {
  if (!backendAvailable()) {
    throw new Error("Open the desktop installer to remove RevisionOS files.");
  }
  return invoke<UninstallReport>("disk_mode_uninstall", {
    root,
    keepUserData,
  });
}

export async function listNorBackups(): Promise<NorBackupEntry[]> {
  if (!backendAvailable()) return [];
  return invoke<NorBackupEntry[]>("list_nor_backups");
}

export async function openBackupsFolder(): Promise<void> {
  if (!backendAvailable()) {
    throw new Error("Open the desktop installer to open backups.");
  }
  return invoke<void>("open_backups_folder");
}

export async function migrateIpodDrive(
  root: string,
  options: MigrationOptions,
  onProgress?: (event: ProgressEvent) => void,
  catalogUrl?: string,
): Promise<MigrationReport> {
  if (!backendAvailable()) {
    throw new Error("Open the desktop installer to migrate your iPod.");
  }
  const onEvent = createChannel<ProgressEvent>();
  onEvent.onmessage = (event) => onProgress?.(event);
  return invoke<MigrationReport>("migrate_ipod_drive", {
    root,
    options,
    catalogUrl: catalogUrl ?? null,
    onEvent,
  });
}
