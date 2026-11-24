use serde::Serialize;
use std::sync::atomic::AtomicBool;
use tauri::{ipc::Channel, AppHandle};
use tauri_plugin_opener::OpenerExt;

const DOWNLOAD_PAGE: &str = "https://github.com/aurelian-th/ipod-things";

#[derive(Default)]
pub struct UpdateState {
    pub installing: AtomicBool,
}

#[derive(Clone, Copy, Serialize, PartialEq)]
#[serde(rename_all = "snake_case")]
pub enum UpdateMode {
    Automatic,
    Manual,
    Disabled,
}

#[derive(Serialize)]
pub struct UpdateInfo {
    current_version: String,
    version: Option<String>,
    mode: UpdateMode,
}

#[derive(Serialize)]
pub struct UpdateProgress {
    stage: &'static str,
    downloaded: u64,
    total: Option<u64>,
}

#[tauri::command]
pub async fn check_installer_update(app: AppHandle) -> Result<UpdateInfo, String> {
    Ok(UpdateInfo {
        current_version: app.package_info().version.to_string(),
        version: None,
        mode: UpdateMode::Disabled,
    })
}

#[tauri::command]
pub async fn install_installer_update(
    _app: AppHandle,
    _on_event: Channel<UpdateProgress>,
) -> Result<(), String> {
    Err("Automatic self-updates are not configured for this build.".into())
}

#[tauri::command]
pub async fn open_installer_download(app: AppHandle) -> Result<(), String> {
    app.opener()
        .open_url(DOWNLOAD_PAGE, None::<&str>)
        .map_err(|e| e.to_string())
}
