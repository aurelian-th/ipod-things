use revisionos_core::{
    catalog::{fetch_catalog, Catalog, DEFAULT_CATALOG_URL},
    ipod_drive::{
        detect_ipod_drives as core_detect_drives, scan_ipod_root, sha256_hex, IpodDriveSummary,
        IpodScan,
    },
    recipe::assemble_recipe,
    sync::{
        apply_store_actions_with_progress, disk_mode_uninstall as core_uninstall,
        disk_mode_upgrade_firmware as core_upgrade_firmware, migrate_to_revisionos_with_progress,
        ApplyReport, MigrationOptions, MigrationReport, ProgressEvent, StoreAction, SyncError,
        UninstallReport,
    },
};
use serde::Serialize;
use std::{
    collections::BTreeMap,
    fs,
    path::{Path, PathBuf},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    time::Duration,
};
use tauri::{ipc::Channel, AppHandle, Manager, State};
use tauri_plugin_dialog::DialogExt;
use tauri_plugin_opener::OpenerExt;

#[derive(Default)]
pub struct StoreTaskState {
    pub cancel: Arc<AtomicBool>,
}

#[derive(Serialize)]
pub struct CachedFirmwareStatus {
    pub has_ipsw: bool,
    pub has_decrypted_osos: bool,
}

#[derive(Serialize)]
pub struct NorBackupEntry {
    pub device_dir: String,
    pub sha256: String,
    pub path: String,
    pub bytes: u64,
}

#[derive(Clone)]
pub struct DownloadItem {
    pub destination: String,
    pub bytes: Vec<u8>,
}

pub fn http_download_verified(
    client: &reqwest::blocking::Client,
    url: &str,
    expected_sha256: Option<&str>,
    name: &str,
) -> Result<Vec<u8>, SyncError> {
    let bytes = client
        .get(url)
        .send()
        .and_then(reqwest::blocking::Response::error_for_status)
        .map_err(|e| SyncError::Download(e.to_string()))?
        .bytes()
        .map_err(|e| SyncError::Download(e.to_string()))?
        .to_vec();
    if let Some(expected) = expected_sha256 {
        let actual = sha256_hex(&bytes);
        if actual != expected {
            return Err(SyncError::ChecksumMismatch {
                name: name.into(),
                expected: expected.into(),
                actual,
            });
        }
    }
    Ok(bytes)
}

pub fn build_http_client() -> Result<reqwest::blocking::Client, String> {
    reqwest::blocking::Client::builder()
        .user_agent(concat!("revisionos-installer/", env!("CARGO_PKG_VERSION")))
        .timeout(Duration::from_secs(45))
        .build()
        .map_err(|e| e.to_string())
}

pub fn collect_selected_downloads(
    catalog: &Catalog,
    extensions: &[String],
    games: &[String],
    apps: &[String],
) -> Result<Vec<DownloadItem>, String> {
    let client = build_http_client()?;
    let mut items = Vec::new();

    for id in extensions {
        let Some(ext) = catalog.extensions.iter().find(|e| &e.id == id) else {
            return Err(format!("Extension '{id}' not found in catalog."));
        };
        let bytes = http_download_verified(&client, &ext.url, Some(&ext.sha256), &ext.id)
            .map_err(|e| e.to_string())?;
        items.push(DownloadItem {
            destination: format!("/Revola/Extensions/{}.rvx", ext.id),
            bytes,
        });
    }

    for id in games {
        let Some(game) = catalog.games.iter().find(|g| &g.id == id) else {
            return Err(format!("Game '{id}' not found in catalog."));
        };
        for file in &game.files {
            let bytes = http_download_verified(&client, &file.url, Some(&file.sha256), &file.path)
                .map_err(|e| e.to_string())?;
            items.push(DownloadItem {
                destination: format!("/Revola/Games/{}/{}", game.folder, file.name),
                bytes,
            });
        }
    }

    for id in apps {
        let Some(app_item) = catalog.apps.iter().find(|a| &a.id == id) else {
            return Err(format!("App '{id}' not found in catalog."));
        };
        for file in &app_item.files {
            let bytes = http_download_verified(&client, &file.url, Some(&file.sha256), &file.path)
                .map_err(|e| e.to_string())?;
            items.push(DownloadItem {
                destination: format!("/Revola/Apps/{}/{}", app_item.folder, file.name),
                bytes,
            });
        }
    }

    Ok(items)
}

pub fn assemble_revisionos_firmware(
    catalog: &Catalog,
    osos_bytes: &[u8],
) -> Result<Vec<u8>, String> {
    let fw_json_meta = catalog
        .firmware
        .files
        .get("firmware.json")
        .ok_or("Catalog missing firmware.json")?;
    let fw_data_meta = catalog
        .firmware
        .files
        .get("firmware.data")
        .ok_or("Catalog missing firmware.data")?;
    let client = build_http_client()?;
    let fw_json = http_download_verified(
        &client,
        &fw_json_meta.url,
        Some(&fw_json_meta.sha256),
        "firmware.json",
    )
    .map_err(|e| e.to_string())?;
    let fw_data = http_download_verified(
        &client,
        &fw_data_meta.url,
        Some(&fw_data_meta.sha256),
        "firmware.data",
    )
    .map_err(|e| e.to_string())?;
    let mut inputs = BTreeMap::new();
    inputs.insert("osos".to_string(), osos_bytes);
    assemble_recipe(&fw_json, &fw_data, &inputs).map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn fetch_revision_catalog(catalog_url: Option<String>) -> Result<Catalog, String> {
    let url = catalog_url.unwrap_or_else(|| DEFAULT_CATALOG_URL.to_string());
    tauri::async_runtime::spawn_blocking(move || fetch_catalog(&url).map_err(|e| e.to_string()))
        .await
        .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn detect_ipod_drives() -> Result<Vec<IpodDriveSummary>, String> {
    tauri::async_runtime::spawn_blocking(core_detect_drives)
        .await
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn pick_ipod_drive(app: AppHandle) -> Result<Option<IpodDriveSummary>, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let Some(folder) = app
            .dialog()
            .file()
            .set_title("Choose your connected iPod drive folder")
            .blocking_pick_folder()
        else {
            return Ok(None);
        };
        let path = folder.into_path().map_err(|e| e.to_string())?;
        let scan = scan_ipod_root(&path).map_err(|e| e.to_string())?;
        Ok(Some(scan.summary()))
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn scan_ipod_drive(root: String) -> Result<IpodScan, String> {
    tauri::async_runtime::spawn_blocking(move || {
        scan_ipod_root(Path::new(&root)).map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub fn cancel_store_task(state: State<'_, StoreTaskState>) {
    state.cancel.store(true, Ordering::Release);
}

#[tauri::command]
pub fn cached_firmware_status(app: AppHandle) -> Result<CachedFirmwareStatus, String> {
    let fw_dir = app
        .path()
        .app_data_dir()
        .map_err(|e| e.to_string())?
        .join("firmware");
    Ok(CachedFirmwareStatus {
        has_ipsw: fw_dir.join("iPod_38.2.0.5.ipsw").is_file(),
        has_decrypted_osos: fw_dir.join("osos-2.0.5.bin").is_file(),
    })
}

#[tauri::command]
pub async fn apply_store_changes(
    state: State<'_, StoreTaskState>,
    root: String,
    actions: Vec<StoreAction>,
    on_event: Channel<ProgressEvent>,
    catalog_url: Option<String>,
) -> Result<ApplyReport, String> {
    let cancel = state.cancel.clone();
    cancel.store(false, Ordering::Release);
    let url = catalog_url.unwrap_or_else(|| DEFAULT_CATALOG_URL.to_string());
    tauri::async_runtime::spawn_blocking(move || {
        let _ = on_event.send(ProgressEvent {
            step: "catalog".into(),
            detail: "Fetching catalog from GitHub".into(),
            completed: 0,
            total: actions.len() as u64,
        });
        let catalog = fetch_catalog(&url).map_err(|e| e.to_string())?;
        let client = build_http_client()?;
        apply_store_actions_with_progress(
            Path::new(&root),
            &catalog,
            &actions,
            |asset_url| http_download_verified(&client, asset_url, None, asset_url),
            |ev| {
                let _ = on_event.send(ev);
            },
            || cancel.load(Ordering::Acquire),
        )
        .map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn disk_mode_upgrade_firmware(
    app: AppHandle,
    root: String,
    catalog_url: Option<String>,
) -> Result<(), String> {
    let url = catalog_url.unwrap_or_else(|| DEFAULT_CATALOG_URL.to_string());
    tauri::async_runtime::spawn_blocking(move || {
        let osos_path = app
            .path()
            .app_data_dir()
            .map_err(|e| e.to_string())?
            .join("firmware/osos-2.0.5.bin");
        if !osos_path.is_file() {
            return Err(
                "Decrypted Apple 2.0.5 firmware is not cached yet. Run a full Install/Reapply first."
                    .into(),
            );
        }
        let osos_bytes = fs::read(&osos_path).map_err(|e| e.to_string())?;
        let catalog = fetch_catalog(&url).map_err(|e| e.to_string())?;
        let revola_bin = assemble_revisionos_firmware(&catalog, &osos_bytes)?;
        core_upgrade_firmware(Path::new(&root), &revola_bin, None).map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn disk_mode_uninstall(root: String, keep_user_data: bool) -> Result<UninstallReport, String> {
    tauri::async_runtime::spawn_blocking(move || {
        core_uninstall(Path::new(&root), keep_user_data).map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn migrate_ipod_drive(
    app: AppHandle,
    state: State<'_, StoreTaskState>,
    root: String,
    options: MigrationOptions,
    on_event: Channel<ProgressEvent>,
    catalog_url: Option<String>,
) -> Result<MigrationReport, String> {
    let cancel = state.cancel.clone();
    cancel.store(false, Ordering::Release);
    let url = catalog_url.unwrap_or_else(|| DEFAULT_CATALOG_URL.to_string());
    tauri::async_runtime::spawn_blocking(move || {
        let _ = on_event.send(ProgressEvent {
            step: "catalog".into(),
            detail: "Fetching catalog from GitHub".into(),
            completed: 0,
            total: 5,
        });
        let catalog = fetch_catalog(&url).map_err(|e| e.to_string())?;
        let client = build_http_client()?;
        let revola_bin = if options.upgrade_firmware {
            let osos_path = app
                .path()
                .app_data_dir()
                .map_err(|e| e.to_string())?
                .join("firmware/osos-2.0.5.bin");
            if osos_path.is_file() {
                let _ = on_event.send(ProgressEvent {
                    step: "assemble".into(),
                    detail: "Assembling RevisionOS core from cached Apple 2.0.5 firmware".into(),
                    completed: 1,
                    total: 5,
                });
                let osos_bytes = fs::read(&osos_path).map_err(|e| e.to_string())?;
                Some(assemble_revisionos_firmware(&catalog, &osos_bytes)?)
            } else {
                None
            }
        } else {
            None
        };
        migrate_to_revisionos_with_progress(
            Path::new(&root),
            &catalog,
            revola_bin.as_deref(),
            &options,
            |asset_url| http_download_verified(&client, asset_url, None, asset_url),
            |ev| {
                let _ = on_event.send(ev);
            },
            || cancel.load(Ordering::Acquire),
        )
        .map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn list_nor_backups(app: AppHandle) -> Result<Vec<NorBackupEntry>, String> {
    let data_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    tauri::async_runtime::spawn_blocking(move || collect_nor_backups(&data_dir))
        .await
        .map_err(|e| e.to_string())
}

fn collect_nor_backups(data_dir: &Path) -> Vec<NorBackupEntry> {
    let mut backups = Vec::new();
    let Ok(devices) = fs::read_dir(data_dir) else {
        return backups;
    };
    for dev in devices.flatten() {
        let nor_dir = dev.path().join("nor");
        let Ok(hashes) = fs::read_dir(&nor_dir) else {
            continue;
        };
        for h in hashes.flatten() {
            let nor_file = h.path().join("nor.bin");
            if let Ok(meta) = fs::metadata(&nor_file) {
                if meta.is_file() {
                    backups.push(NorBackupEntry {
                        device_dir: dev.file_name().to_string_lossy().into_owned(),
                        sha256: h.file_name().to_string_lossy().into_owned(),
                        path: nor_file.to_string_lossy().into_owned(),
                        bytes: meta.len(),
                    });
                }
            }
        }
    }
    backups
}

#[tauri::command]
pub async fn open_backups_folder(app: AppHandle) -> Result<(), String> {
    let dir: PathBuf = app.path().app_data_dir().map_err(|e| e.to_string())?;
    fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    app.opener()
        .open_path(dir.to_string_lossy(), None::<&str>)
        .map_err(|e| e.to_string())
}

