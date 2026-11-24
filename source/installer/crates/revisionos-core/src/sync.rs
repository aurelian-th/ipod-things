use crate::{
    catalog::{BundleItem, Catalog, ExtensionItem},
    ipod_drive::{
        files_identical, scan_ipod_root, sha256_hex, DriveError, FirmwareLayout, IpodScan,
    },
};
use semver::Version;
use serde::{Deserialize, Serialize};
use std::{cmp::Ordering, fs, io::Write, path::Path};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum SyncError {
    #[error("Operation cancelled by user")]
    Cancelled,
    #[error("Unknown item id: {0}")]
    UnknownItem(String),
    #[error("Download failed: {0}")]
    Download(String),
    #[error("Checksum mismatch for {name}: expected {expected}, got {actual}")]
    ChecksumMismatch {
        name: String,
        expected: String,
        actual: String,
    },
    #[error("Size mismatch for {name}: expected {expected} bytes, got {actual}")]
    SizeMismatch {
        name: String,
        expected: u64,
        actual: u64,
    },
    #[error("{0}")]
    Drive(#[from] DriveError),
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ProgressEvent {
    pub step: String,
    pub detail: String,
    pub completed: u64,
    pub total: u64,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "status", rename_all = "snake_case")]
pub enum StoreStatus {
    UpToDate { installed: String },
    UpdateAvailable { installed: String, latest: String },
    NotInstalled,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "action", rename_all = "snake_case")]
pub enum StoreAction {
    InstallExtension { id: String },
    UpdateExtension { id: String },
    RemoveExtension { id: String },
    InstallGame { id: String },
    UpdateGame { id: String },
    RemoveGame { id: String },
    InstallApp { id: String },
    UpdateApp { id: String },
    RemoveApp { id: String },
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct ApplyReport {
    pub installed_extensions: Vec<String>,
    pub removed_extensions: Vec<String>,
    pub installed_games: Vec<String>,
    pub removed_games: Vec<String>,
    pub installed_apps: Vec<String>,
    pub removed_apps: Vec<String>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct UninstallReport {
    pub removed_paths: Vec<String>,
    pub kept_user_data: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct MigrationOptions {
    pub migrate_games_ro: bool,
    pub cleanup_legacy_bins: bool,
    pub upgrade_firmware: bool,
    pub update_extensions: bool,
}

impl Default for MigrationOptions {
    fn default() -> Self {
        Self {
            migrate_games_ro: true,
            cleanup_legacy_bins: true,
            upgrade_firmware: true,
            update_extensions: true,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct MigrationReport {
    pub source_layout: FirmwareLayout,
    pub migrated_games: Vec<String>,
    pub updated_extensions: Vec<String>,
    pub installed_homebrew: bool,
    pub firmware_updated: bool,
    pub cleaned_legacy_files: Vec<String>,
    pub notes: Vec<String>,
}

fn normalize_semver(raw: &str) -> Option<Version> {
    let trimmed = raw.trim().trim_start_matches('v');
    if let Ok(v) = Version::parse(trimmed) {
        return Some(v);
    }
    let parts: Vec<&str> = trimmed.split('.').collect();
    if parts.len() == 2 {
        return Version::parse(&format!("{}.{}.0", parts[0], parts[1])).ok();
    }
    None
}

pub fn compare_versions(installed: &str, latest: &str) -> Ordering {
    if let (Some(a), Some(b)) = (normalize_semver(installed), normalize_semver(latest)) {
        return a.cmp(&b);
    }
    let a_nums: Vec<u64> = installed
        .split('.')
        .filter_map(|p| p.parse::<u64>().ok())
        .collect();
    let b_nums: Vec<u64> = latest
        .split('.')
        .filter_map(|p| p.parse::<u64>().ok())
        .collect();
    if !a_nums.is_empty() && !b_nums.is_empty() {
        return a_nums.cmp(&b_nums);
    }
    installed.cmp(latest)
}

pub fn extension_status(item: &ExtensionItem, scan: &IpodScan) -> StoreStatus {
    match scan.extensions.iter().find(|e| e.id == item.id) {
        None => StoreStatus::NotInstalled,
        Some(installed) => {
            if compare_versions(&installed.version, &item.version) == Ordering::Less
                || (installed.version == item.version && installed.sha256 != item.sha256)
            {
                StoreStatus::UpdateAvailable {
                    installed: installed.version.clone(),
                    latest: item.version.clone(),
                }
            } else {
                StoreStatus::UpToDate {
                    installed: installed.version.clone(),
                }
            }
        }
    }
}

pub fn bundle_status(item: &BundleItem, scan: &IpodScan) -> StoreStatus {
    let existing = match item.kind.as_str() {
        "game" => scan.games.iter().find(|g| g.id == item.id),
        _ => scan.apps.iter().find(|a| a.id == item.id),
    };
    match existing {
        None => StoreStatus::NotInstalled,
        Some(installed) => {
            if compare_versions(&installed.version, &item.version) == Ordering::Less {
                StoreStatus::UpdateAvailable {
                    installed: installed.version.clone(),
                    latest: item.version.clone(),
                }
            } else {
                StoreStatus::UpToDate {
                    installed: installed.version.clone(),
                }
            }
        }
    }
}

pub fn atomic_write_verified(
    dest: &Path,
    bytes: &[u8],
    expected_sha256: Option<&str>,
    name: &str,
) -> Result<(), SyncError> {
    if let Some(expected) = expected_sha256 {
        let actual = sha256_hex(bytes);
        if actual != expected {
            return Err(SyncError::ChecksumMismatch {
                name: name.into(),
                expected: expected.into(),
                actual,
            });
        }
    }
    if let Some(parent) = dest.parent() {
        fs::create_dir_all(parent)?;
    }
    let tmp = dest.with_extension("tmp");
    {
        let mut f = fs::File::create(&tmp)?;
        f.write_all(bytes)?;
        f.sync_all()?;
    }
    fs::rename(&tmp, dest)?;
    Ok(())
}

pub fn apply_store_actions<F>(
    root: &Path,
    catalog: &Catalog,
    actions: &[StoreAction],
    fetch: F,
) -> Result<ApplyReport, SyncError>
where
    F: Fn(&str) -> Result<Vec<u8>, SyncError>,
{
    apply_store_actions_with_progress(root, catalog, actions, fetch, |_| {}, || false)
}

pub fn apply_store_actions_with_progress<F, P, C>(
    root: &Path,
    catalog: &Catalog,
    actions: &[StoreAction],
    fetch: F,
    mut on_progress: P,
    is_cancelled: C,
) -> Result<ApplyReport, SyncError>
where
    F: Fn(&str) -> Result<Vec<u8>, SyncError>,
    P: FnMut(ProgressEvent),
    C: Fn() -> bool,
{
    let mut report = ApplyReport::default();
    let revola = root.join("Revola");
    let total = actions.len() as u64;

    for (idx, action) in actions.iter().enumerate() {
        if is_cancelled() {
            return Err(SyncError::Cancelled);
        }
        let step_num = idx as u64;
        match action {
            StoreAction::InstallExtension { id } | StoreAction::UpdateExtension { id } => {
                let item = catalog
                    .extensions
                    .iter()
                    .find(|e| &e.id == id)
                    .ok_or_else(|| SyncError::UnknownItem(id.clone()))?;
                on_progress(ProgressEvent {
                    step: "extension".into(),
                    detail: format!("Downloading and writing {}.rvx", item.id),
                    completed: step_num,
                    total,
                });
                let bytes = fetch(&item.url)?;
                if bytes.len() as u64 != item.bytes {
                    return Err(SyncError::SizeMismatch {
                        name: item.id.clone(),
                        expected: item.bytes,
                        actual: bytes.len() as u64,
                    });
                }
                let dest = revola.join("Extensions").join(format!("{}.rvx", item.id));
                atomic_write_verified(&dest, &bytes, Some(&item.sha256), &item.id)?;
                report.installed_extensions.push(item.id.clone());
            }
            StoreAction::RemoveExtension { id } => {
                on_progress(ProgressEvent {
                    step: "remove".into(),
                    detail: format!("Removing {id}.rvx"),
                    completed: step_num,
                    total,
                });
                let target = revola.join("Extensions").join(format!("{id}.rvx"));
                if target.exists() {
                    fs::remove_file(&target)?;
                    report.removed_extensions.push(id.clone());
                }
            }
            StoreAction::InstallGame { id } | StoreAction::UpdateGame { id } => {
                let item = catalog
                    .games
                    .iter()
                    .find(|g| &g.id == id)
                    .ok_or_else(|| SyncError::UnknownItem(id.clone()))?;
                on_progress(ProgressEvent {
                    step: "game".into(),
                    detail: format!("Installing game {}", item.name),
                    completed: step_num,
                    total,
                });
                write_bundle(&revola.join("Games"), item, &fetch, &is_cancelled)?;
                report.installed_games.push(item.id.clone());
            }
            StoreAction::RemoveGame { id } => {
                let folder = catalog
                    .games
                    .iter()
                    .find(|g| &g.id == id)
                    .map(|g| g.folder.as_str())
                    .unwrap_or(id.as_str());
                on_progress(ProgressEvent {
                    step: "remove".into(),
                    detail: format!("Removing game {folder}"),
                    completed: step_num,
                    total,
                });
                let target = revola.join("Games").join(folder);
                if target.exists() {
                    fs::remove_dir_all(&target)?;
                    report.removed_games.push(id.clone());
                }
            }
            StoreAction::InstallApp { id } | StoreAction::UpdateApp { id } => {
                let item = catalog
                    .apps
                    .iter()
                    .find(|a| &a.id == id)
                    .ok_or_else(|| SyncError::UnknownItem(id.clone()))?;
                on_progress(ProgressEvent {
                    step: "app".into(),
                    detail: format!("Installing app {}", item.name),
                    completed: step_num,
                    total,
                });
                write_bundle(&revola.join("Apps"), item, &fetch, &is_cancelled)?;
                report.installed_apps.push(item.id.clone());
            }
            StoreAction::RemoveApp { id } => {
                let folder = catalog
                    .apps
                    .iter()
                    .find(|a| &a.id == id)
                    .map(|a| a.folder.as_str())
                    .unwrap_or(id.as_str());
                on_progress(ProgressEvent {
                    step: "remove".into(),
                    detail: format!("Removing app {folder}"),
                    completed: step_num,
                    total,
                });
                let target = revola.join("Apps").join(folder);
                if target.exists() {
                    fs::remove_dir_all(&target)?;
                    report.removed_apps.push(id.clone());
                }
            }
        }
        on_progress(ProgressEvent {
            step: "done_item".into(),
            detail: format!("Finished step {} of {total}", step_num + 1),
            completed: step_num + 1,
            total,
        });
    }
    Ok(report)
}

fn write_bundle<F, C>(
    base_dir: &Path,
    item: &BundleItem,
    fetch: &F,
    is_cancelled: &C,
) -> Result<(), SyncError>
where
    F: Fn(&str) -> Result<Vec<u8>, SyncError>,
    C: Fn() -> bool,
{
    let dest_dir = base_dir.join(&item.folder);
    fs::create_dir_all(&dest_dir)?;
    for file in &item.files {
        if is_cancelled() {
            return Err(SyncError::Cancelled);
        }
        let bytes = fetch(&file.url)?;
        if bytes.len() as u64 != file.bytes {
            return Err(SyncError::SizeMismatch {
                name: file.path.clone(),
                expected: file.bytes,
                actual: bytes.len() as u64,
            });
        }
        atomic_write_verified(
            &dest_dir.join(&file.name),
            &bytes,
            Some(&file.sha256),
            &file.path,
        )?;
    }
    Ok(())
}

fn copy_dir_all<C>(src: &Path, dst: &Path, is_cancelled: &C) -> Result<(), SyncError>
where
    C: Fn() -> bool,
{
    fs::create_dir_all(dst)?;
    for entry in fs::read_dir(src)? {
        if is_cancelled() {
            return Err(SyncError::Cancelled);
        }
        let entry = entry?;
        let path = entry.path();
        let target = dst.join(entry.file_name());
        if path.is_dir() {
            copy_dir_all(&path, &target, is_cancelled)?;
        } else if path.is_file() {
            fs::copy(&path, &target)?;
        }
    }
    Ok(())
}

pub fn migrate_to_revisionos<F>(
    root: &Path,
    catalog: &Catalog,
    revola_bin: Option<&[u8]>,
    options: &MigrationOptions,
    fetch: F,
) -> Result<MigrationReport, SyncError>
where
    F: Fn(&str) -> Result<Vec<u8>, SyncError>,
{
    migrate_to_revisionos_with_progress(
        root,
        catalog,
        revola_bin,
        options,
        fetch,
        |_| {},
        || false,
    )
}

pub fn migrate_to_revisionos_with_progress<F, P, C>(
    root: &Path,
    catalog: &Catalog,
    revola_bin: Option<&[u8]>,
    options: &MigrationOptions,
    fetch: F,
    mut on_progress: P,
    is_cancelled: C,
) -> Result<MigrationReport, SyncError>
where
    F: Fn(&str) -> Result<Vec<u8>, SyncError>,
    P: FnMut(ProgressEvent),
    C: Fn() -> bool,
{
    on_progress(ProgressEvent {
        step: "scan".into(),
        detail: format!("Scanning iPod storage at {}", root.display()),
        completed: 0,
        total: 5,
    });
    if is_cancelled() {
        return Err(SyncError::Cancelled);
    }

    let scan = scan_ipod_root(root)?;
    let revola = root.join("Revola");
    for sub in ["Firmware", "Extensions", "Games", "Apps"] {
        fs::create_dir_all(revola.join(sub))?;
    }

    let mut report = MigrationReport {
        source_layout: scan.layout,
        migrated_games: Vec::new(),
        updated_extensions: Vec::new(),
        installed_homebrew: false,
        firmware_updated: false,
        cleaned_legacy_files: Vec::new(),
        notes: Vec::new(),
    };

    let fw_revola = revola.join("Firmware/revola.bin");
    let fw_loader = revola.join("Firmware/loader.bin");

    if options.upgrade_firmware {
        if let Some(bin) = revola_bin {
            on_progress(ProgressEvent {
                step: "firmware".into(),
                detail: "Writing /Revola/Firmware/revola.bin".into(),
                completed: 1,
                total: 5,
            });
            disk_mode_upgrade_firmware(root, bin, None)?;
            report.firmware_updated = true;
        } else {
            report.notes.push(
                "Skipped firmware update because Apple 2.0.5 is not cached yet; run DFU Install once to cache it."
                    .into(),
            );
        }
    }

    if is_cancelled() {
        return Err(SyncError::Cancelled);
    }

    if !fw_loader.is_file() && root.join("loader.bin").is_file() {
        on_progress(ProgressEvent {
            step: "loader".into(),
            detail: "Copying /loader.bin to /Revola/Firmware/loader.bin".into(),
            completed: 2,
            total: 5,
        });
        let legacy_loader = fs::read(root.join("loader.bin"))?;
        atomic_write_verified(&fw_loader, &legacy_loader, None, "loader.bin")?;
    }

    if !fw_loader.is_file() {
        report.notes.push(
            "/Revola/Firmware/loader.bin is missing. Run Install or reapply RevisionOS in DFU mode to install the companion loader and bootloader."
                .into(),
        );
    }

    if options.migrate_games_ro {
        let total_ro = scan.reprise_games.len() as u64;
        for (i, game) in scan.reprise_games.iter().enumerate() {
            if is_cancelled() {
                return Err(SyncError::Cancelled);
            }
            let src_dir = root.join("iPod_Control/Games_RO").join(&game.folder);
            let dst_dir = revola.join("Games").join(&game.folder);
            if src_dir.is_dir() && !dst_dir.exists() {
                on_progress(ProgressEvent {
                    step: "games".into(),
                    detail: format!(
                        "Copying homebrew game {} ({}/{})",
                        game.name,
                        i + 1,
                        total_ro
                    ),
                    completed: 2,
                    total: 5,
                });
                copy_dir_all(&src_dir, &dst_dir, &is_cancelled)?;
                report.migrated_games.push(game.folder.clone());
            }
        }
        if scan.apple_games_count > 0 {
            report.notes.push(format!(
                "Kept {} Apple Click Wheel archival game(s) untouched in /iPod_Control/Games_RO/.",
                scan.apple_games_count
            ));
        }
    }

    if options.update_extensions {
        if is_cancelled() {
            return Err(SyncError::Cancelled);
        }
        let mut actions = Vec::new();
        for ext in &catalog.extensions {
            if matches!(extension_status(ext, &scan), StoreStatus::UpdateAvailable { .. }) {
                actions.push(StoreAction::UpdateExtension { id: ext.id.clone() });
            }
        }
        let has_homebrew = scan.extensions.iter().any(|e| e.id == "homebrew");
        let has_any_bundles =
            !scan.games.is_empty() || !scan.apps.is_empty() || !report.migrated_games.is_empty();
        if has_any_bundles && !has_homebrew && catalog.extensions.iter().any(|e| e.id == "homebrew")
        {
            actions.push(StoreAction::InstallExtension {
                id: "homebrew".into(),
            });
            report.installed_homebrew = true;
        }
        if !actions.is_empty() {
            on_progress(ProgressEvent {
                step: "extensions".into(),
                detail: format!("Updating {} extension(s)", actions.len()),
                completed: 3,
                total: 5,
            });
            let apply = apply_store_actions_with_progress(
                root,
                catalog,
                &actions,
                &fetch,
                |ev| on_progress(ev),
                &is_cancelled,
            )?;
            report.updated_extensions = apply.installed_extensions;
        }
    }

    if options.cleanup_legacy_bins {
        on_progress(ProgressEvent {
            step: "cleanup".into(),
            detail: "Checking legacy root firmware files".into(),
            completed: 4,
            total: 5,
        });
        if fw_revola.is_file() {
            for legacy in ["revola.bin", "loader.bin"] {
                let target = root.join(legacy);
                if target.is_file() {
                    fs::remove_file(&target)?;
                    report.cleaned_legacy_files.push(format!("/{legacy}"));
                }
            }
        }
        if scan.has_reprise_osos || scan.has_reprise_loader {
            report.notes.push(
                "Kept /osos-cfw.bin and /cfw-loader.bin because Disk Mode cannot verify whether NOR uses the RepriseOS bootloader. If you flash the Revola/RevisionOS bootloader via DFU, those two files are no longer needed."
                    .into(),
            );
        }
    }

    on_progress(ProgressEvent {
        step: "complete".into(),
        detail: "Migration finished".into(),
        completed: 5,
        total: 5,
    });

    Ok(report)
}

pub fn disk_mode_upgrade_firmware(
    root: &Path,
    revola_bin: &[u8],
    loader_bin: Option<&[u8]>,
) -> Result<(), SyncError> {
    let fw_revola = root.join("Revola/Firmware/revola.bin");
    let root_revola = root.join("revola.bin");
    let osos_cfw = root.join("osos-cfw.bin");

    let osos_mirrored_revola =
        osos_cfw.is_file() && (!fw_revola.is_file() || files_identical(&osos_cfw, &fw_revola));

    atomic_write_verified(&fw_revola, revola_bin, None, "revola.bin")?;
    if root_revola.is_file() {
        atomic_write_verified(&root_revola, revola_bin, None, "root revola.bin")?;
    }
    if osos_mirrored_revola {
        atomic_write_verified(&osos_cfw, revola_bin, None, "osos-cfw.bin")?;
    }
    if let Some(loader) = loader_bin {
        let fw_loader = root.join("Revola/Firmware/loader.bin");
        atomic_write_verified(&fw_loader, loader, None, "loader.bin")?;
        if root.join("loader.bin").is_file() {
            atomic_write_verified(&root.join("loader.bin"), loader, None, "root loader.bin")?;
        }
    }
    Ok(())
}

pub fn disk_mode_uninstall(root: &Path, keep_user_data: bool) -> Result<UninstallReport, SyncError> {
    let revola = root.join("Revola");
    let mut removed_paths = Vec::new();
    let mut folders = vec![
        "Firmware",
        "Extensions",
        "Games",
        "Apps",
        "Themes",
        "Crashes",
    ];
    if !keep_user_data {
        folders.push("Data");
    }
    for sub in folders {
        let path = revola.join(sub);
        if path.exists() {
            fs::remove_dir_all(&path)?;
            removed_paths.push(format!("/Revola/{sub}"));
        }
    }
    for legacy in ["revola.bin", "loader.bin"] {
        let path = root.join(legacy);
        if path.is_file() {
            fs::remove_file(&path)?;
            removed_paths.push(format!("/{legacy}"));
        }
    }
    if revola.is_dir()
        && fs::read_dir(&revola)
            .map(|mut d| d.next().is_none())
            .unwrap_or(false)
    {
        fs::remove_dir(&revola)?;
        removed_paths.push("/Revola".into());
    }
    Ok(UninstallReport {
        removed_paths,
        kept_user_data: keep_user_data,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        catalog::{BundleFile, Category, FirmwareCatalog},
        rvx::{test_helpers::build_test_rvx, ExtensionType},
    };
    use std::collections::BTreeMap;
    use tempfile::tempdir;

    const SAMPLE_EAPP_PLIST: &str = r#"<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
  <key>Name</key><string>2048</string>
  <key>BuildIdentifier</key><string>2048</string>
  <key>Version</key><string>0.2.0</string>
  <key>Files</key><array>
    <dict><key>Path</key><string>2048.eapp</string><key>Size</key><integer>100</integer></dict>
  </array>
</dict></plist>"#;

    const SAMPLE_APPLE_DRM_PLIST: &str = r#"<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
  <key>BuildIdentifier</key><string>1462222666-fc4e9559d11</string>
  <key>Files</key><array>
    <dict><key>DRM</key><true/><key>Path</key><string>Executables/Phase.bin</string><key>Verify</key><true/></dict>
  </array>
</dict></plist>"#;

    fn make_synthetic_assets() -> (Catalog, BTreeMap<String, Vec<u8>>) {
        let mut blobs = BTreeMap::new();
        let hsc_bytes = build_test_rvx(
            "hold-standby-clock",
            "Hold Standby Clock",
            "1.0.0",
            "0xAu",
            2 | (2 << 2),
        );
        let hb_bytes = build_test_rvx("homebrew", "Homebrew", "0.1.0", "RevolaOS", 2 | (4 << 2));
        let dice_plist = b"<plist><dict><key>Name</key><string>Dice</string></dict></plist>".to_vec();

        blobs.insert("https://example.invalid/hsc.rvx".to_string(), hsc_bytes.clone());
        blobs.insert("https://example.invalid/homebrew.rvx".to_string(), hb_bytes.clone());
        blobs.insert("https://example.invalid/dice/Manifest.plist".to_string(), dice_plist.clone());

        let catalog = Catalog {
            schema: 1,
            repo: "aurelian-th/ipod-things".into(),
            branch: "main".into(),
            backbone_disclaimer: "Temporary RevolaOS backbone".into(),
            custom_build_notice: "Use RevisionOS revola.bin".into(),
            whats_new: vec![],
            categories: vec![Category {
                id: ExtensionType::Customization,
                label: "Customization".into(),
                description: "Look".into(),
            }],
            firmware: FirmwareCatalog {
                name: "RevisionOS Core".into(),
                version: "1.74.0".into(),
                api: "1.74".into(),
                target: "classic7g".into(),
                checks: 172,
                output_bytes: 1024,
                requires_custom_build: true,
                files: BTreeMap::new(),
            },
            extensions: vec![
                ExtensionItem {
                    id: "hold-standby-clock".into(),
                    name: "Hold Standby Clock".into(),
                    extension_type: ExtensionType::Customization,
                    version: "1.0.0".into(),
                    author: "0xAu".into(),
                    stability: "beta".into(),
                    api: "1.74".into(),
                    default_enabled: false,
                    requires_custom_core: true,
                    description: "Standby clock".into(),
                    changelog: "v1.0.0".into(),
                    file: "Extensions/hold-standby-clock.rvx".into(),
                    bytes: hsc_bytes.len() as u64,
                    sha256: sha256_hex(&hsc_bytes),
                    url: "https://example.invalid/hsc.rvx".into(),
                },
                ExtensionItem {
                    id: "homebrew".into(),
                    name: "Homebrew".into(),
                    extension_type: ExtensionType::Utility,
                    version: "0.1.0".into(),
                    author: "RevolaOS".into(),
                    stability: "beta".into(),
                    api: "1.54".into(),
                    default_enabled: true,
                    requires_custom_core: false,
                    description: "Launcher".into(),
                    changelog: "v0.1.0".into(),
                    file: "Extensions/homebrew.rvx".into(),
                    bytes: hb_bytes.len() as u64,
                    sha256: sha256_hex(&hb_bytes),
                    url: "https://example.invalid/homebrew.rvx".into(),
                },
            ],
            games: vec![],
            apps: vec![BundleItem {
                id: "dice".into(),
                folder: "dice".into(),
                kind: "app".into(),
                name: "Dice".into(),
                version: "0.2.0".into(),
                author: "RevolaOS".into(),
                description: "Dice".into(),
                changelog: "v0.2.0".into(),
                total_bytes: dice_plist.len() as u64,
                files: vec![BundleFile {
                    name: "Manifest.plist".into(),
                    path: "Apps/dice/Manifest.plist".into(),
                    bytes: dice_plist.len() as u64,
                    sha256: sha256_hex(&dice_plist),
                    url: "https://example.invalid/dice/Manifest.plist".into(),
                }],
            }],
        };
        (catalog, blobs)
    }

    #[test]
    fn compares_versions_cleanly() {
        assert_eq!(compare_versions("0.1.0", "1.0.0"), Ordering::Less);
        assert_eq!(compare_versions("1.0", "1.0.0"), Ordering::Equal);
        assert_eq!(compare_versions("1.3.0", "0.2.0"), Ordering::Greater);
    }

    #[test]
    fn installs_updates_emits_progress_and_supports_cancel() {
        let (catalog, blobs) = make_synthetic_assets();

        let ipod_dir = tempdir().unwrap();
        let ipod = ipod_dir.path();
        fs::create_dir_all(ipod.join("iPod_Control")).unwrap();
        fs::create_dir_all(ipod.join("Revola/Data")).unwrap();
        fs::write(ipod.join("Revola/Data/save.bin"), b"keep").unwrap();

        let actions = vec![
            StoreAction::InstallExtension {
                id: "hold-standby-clock".into(),
            },
            StoreAction::InstallApp { id: "dice".into() },
        ];
        let mut events = Vec::new();
        let report = apply_store_actions_with_progress(
            ipod,
            &catalog,
            &actions,
            |url| {
                blobs
                    .get(url)
                    .cloned()
                    .ok_or_else(|| SyncError::Download(url.into()))
            },
            |ev| events.push(ev),
            || false,
        )
        .unwrap();
        assert_eq!(report.installed_extensions, vec!["hold-standby-clock"]);
        assert_eq!(report.installed_apps, vec!["dice"]);
        assert_eq!(events.len(), 4);

        let cancelled = apply_store_actions_with_progress(
            ipod,
            &catalog,
            &actions,
            |_| unreachable!(),
            |_| {},
            || true,
        );
        assert!(matches!(cancelled, Err(SyncError::Cancelled)));

        let uninstall = disk_mode_uninstall(ipod, true).unwrap();
        assert!(uninstall.kept_user_data);
        assert!(ipod.join("Revola/Data/save.bin").is_file());
    }

    #[test]
    fn migrates_homebrew_skips_apple_games_and_preserves_osos_cfw() {
        let (catalog, blobs) = make_synthetic_assets();

        let dir = tempdir().unwrap();
        let ipod = dir.path();
        fs::create_dir_all(ipod.join("iPod_Control/Games_RO/2048")).unwrap();
        fs::create_dir_all(ipod.join("iPod_Control/Games_RO/1D000")).unwrap();
        fs::write(ipod.join("osos-cfw.bin"), b"old-bin").unwrap();
        fs::write(ipod.join("cfw-loader.bin"), b"reprise-loader").unwrap();
        fs::write(
            ipod.join("iPod_Control/Games_RO/2048/Manifest.plist"),
            SAMPLE_EAPP_PLIST,
        )
        .unwrap();
        fs::write(
            ipod.join("iPod_Control/Games_RO/1D000/Manifest.plist"),
            SAMPLE_APPLE_DRM_PLIST,
        )
        .unwrap();

        let report = migrate_to_revisionos(
            ipod,
            &catalog,
            Some(b"revision-bin"),
            &MigrationOptions::default(),
            |url| {
                blobs
                    .get(url)
                    .cloned()
                    .ok_or_else(|| SyncError::Download(url.into()))
            },
        )
        .unwrap();

        assert_eq!(report.source_layout, FirmwareLayout::RepriseOs);
        assert_eq!(report.migrated_games, vec!["2048"]);
        assert!(!ipod.join("Revola/Games/1D000").exists());
        assert!(report.installed_homebrew);
        assert!(report.firmware_updated);
        assert!(ipod.join("Revola/Games/2048/Manifest.plist").is_file());
        assert!(ipod.join("Revola/Extensions/homebrew.rvx").is_file());
        assert_eq!(fs::read(ipod.join("osos-cfw.bin")).unwrap(), b"revision-bin");
        assert!(ipod.join("cfw-loader.bin").is_file());
    }

    #[test]
    fn migrates_legacy_root_revola_and_cleans_up_root_bins() {
        let (catalog, _) = make_synthetic_assets();

        let dir = tempdir().unwrap();
        let ipod = dir.path();
        fs::create_dir_all(ipod.join("iPod_Control")).unwrap();
        fs::write(ipod.join("revola.bin"), b"old-revola").unwrap();
        fs::write(ipod.join("loader.bin"), b"old-loader").unwrap();

        let report = migrate_to_revisionos(
            ipod,
            &catalog,
            Some(b"new-revision-bin"),
            &MigrationOptions::default(),
            |_| unreachable!(),
        )
        .unwrap();

        assert_eq!(report.source_layout, FirmwareLayout::RevolaRootLegacy);
        assert!(report.firmware_updated);
        assert!(ipod.join("Revola/Firmware/revola.bin").is_file());
        assert!(ipod.join("Revola/Firmware/loader.bin").is_file());
        assert!(!ipod.join("revola.bin").exists());
        assert!(!ipod.join("loader.bin").exists());
        assert_eq!(report.cleaned_legacy_files, vec!["/revola.bin", "/loader.bin"]);
    }
}
