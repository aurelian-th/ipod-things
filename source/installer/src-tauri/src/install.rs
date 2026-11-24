pub(super) mod diagnostics;
mod firmware;
mod job;
mod transfer;

use crate::{device::UsbState, firmware::FirmwareState, package::PackageState};
use job::Job;
pub use job::{InstallEvent, InstallResult};
use reprise_bundle::assembly;
use reprise_device::UploadHelper;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Mutex,
};
use tauri::{ipc::Channel, AppHandle, Manager};

type Result<T> = std::result::Result<T, Box<dyn std::error::Error + Send + Sync>>;

#[derive(Default)]
pub struct InstallState {
    pub running: AtomicBool,
    cancellation: Mutex<Cancellation>,
    failed_directory: Mutex<Option<std::path::PathBuf>>,
}

#[derive(Default)]
struct Cancellation {
    requested: bool,
    allowed: bool,
}

struct Running<'a>(&'a InstallState);

impl Drop for Running<'_> {
    fn drop(&mut self) {
        self.0.running.store(false, Ordering::Release);
    }
}

#[tauri::command]
pub fn cancel_install(app: AppHandle) {
    let state = app.state::<InstallState>();
    if let Ok(mut cancellation) = state.cancellation.lock() {
        if cancellation.allowed {
            cancellation.requested = true;
        }
    };
}

#[tauri::command]
pub async fn install(
    app: AppHandle,
    firmware_sha256: String,
    package_digest: String,
    selected_extensions: Option<Vec<String>>,
    selected_games: Option<Vec<String>>,
    selected_apps: Option<Vec<String>>,
    catalog_url: Option<String>,
    on_event: Channel<InstallEvent>,
) -> std::result::Result<InstallResult, String> {
    let operation = app
        .state::<UsbState>()
        .acquire()
        .map_err(|e| e.to_string())?;
    let state = app.state::<InstallState>();
    *state.failed_directory.lock().map_err(|e| e.to_string())? = None;
    *state.cancellation.lock().map_err(|e| e.to_string())? = Cancellation {
        requested: false,
        allowed: true,
    };
    state.running.store(true, Ordering::Release);
    tauri::async_runtime::spawn_blocking(move || {
        let _operation = operation;
        let state = app.state::<InstallState>();
        let _running = Running(&state);
        run(
            &app,
            &state,
            firmware_sha256,
            package_digest,
            selected_extensions.unwrap_or_default(),
            selected_games.unwrap_or_default(),
            selected_apps.unwrap_or_default(),
            catalog_url,
            on_event,
        )
        .map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| e.to_string())?
}

fn run(
    app: &AppHandle,
    state: &InstallState,
    firmware_sha256: String,
    package_digest: String,
    selected_extensions: Vec<String>,
    selected_games: Vec<String>,
    selected_apps: Vec<String>,
    catalog_url: Option<String>,
    channel: Channel<InstallEvent>,
) -> Result<InstallResult> {
    let ipsw = app.state::<FirmwareState>().selected(&firmware_sha256)?;
    let bundle = app
        .state::<PackageState>()
        .0
        .lock()
        .map_err(|e| e.to_string())?
        .as_ref()
        .filter(|b| b.digest() == package_digest)
        .cloned()
        .ok_or("Choose the package again.")?;
    crate::package::require_target(&bundle.manifest().compatibility, ipsw.metadata.target()?)?;
    let helper = UploadHelper::from_bytes(
        bundle.file("usb_helper", "image")?,
        bundle.file("usb_helper", "descriptor")?,
    )?;
    if !helper.supports_storage_inspection() {
        return Err("Choose a newer package with storage inspection support.".into());
    }
    let expected_bootloader = assembly::bootloader_fingerprint(&bundle)?;
    let checked = app
        .state::<UsbState>()
        .checked
        .lock()
        .map_err(|e| e.to_string())?
        .take()
        .ok_or("Return to the DFU guide and check your iPod again.")?;
    let identity = checked
        .report
        .identity
        .as_ref()
        .ok_or("Missing checked device identity.")?;
    if !checked.report.compatible
        || checked.report.cleanup != reprise_device::Cleanup::Idle
        || checked.package_digest != package_digest
    {
        return Err("Device or package changed. Run device checks again.".into());
    }
    let hardware_policy = checked.hardware_policy;
    crate::package::require_device_with_policy(
        &bundle.manifest().compatibility,
        &checked.report,
        hardware_policy,
    )?;
    let data = app.path().app_data_dir()?;
    let directory = job::backup_directory(&data, &identity.hardware_id)?;
    job::save_json(&directory.join("device.json"), &checked.report)?;
    job::save_json(
        &directory.join("inputs.json"),
        &serde_json::json!({"ipsw": ipsw.sha256, "bundle": bundle.digest()}),
    )?;
    let mut job = Job::new(state, channel, directory)?;
    job.log(format!(
        "Installer {}; package {} ({}); IPSW {}",
        env!("CARGO_PKG_VERSION"),
        bundle.manifest().version,
        bundle.digest(),
        ipsw.sha256
    ));
    job.log(format!(
        "Device: {} serial={} hardware_id={:02x?}; logs={}",
        identity.model,
        identity.serial,
        identity.hardware_id,
        job.directory.display()
    ));
    if hardware_policy == reprise_device::firmware::HardwarePolicy::AllowUnknown {
        job.log(format!(
            "EXPERIMENTAL: user accepted unknown HwVr {:#010x}; original device identity preserved",
            identity.hardware_version
        ));
        job.save_json(
            "hardware-override.json",
            &serde_json::json!({
                "accepted": true, "hardware_version": identity.hardware_version,
                "package_digest": package_digest,
            }),
        )?;
    }
    let outcome = (|| {
        let catalog = revisionos_core::catalog::fetch_catalog(
            catalog_url
                .as_deref()
                .unwrap_or(revisionos_core::catalog::DEFAULT_CATALOG_URL),
        )?;
        let extras = crate::store::collect_selected_downloads(
            &catalog,
            &selected_extensions,
            &selected_games,
            &selected_apps,
        )
        .map_err(|e| -> Box<dyn std::error::Error + Send + Sync> { e.into() })?;
        let extra_bytes: u32 = extras
            .iter()
            .map(|item| item.bytes.len() as u32)
            .sum();

        let mut session = checked.session;
        job.stage("checkBootloader", "Checking installed bootloader")?;
        let bootloader = session.inspect_bootloader(|p| {
            job.progress("Verifying installed bootloader", p.completed, p.total)
        })?;
        job.save_json("bootloader.json", &bootloader)?;
        job.log(format!("Detected bootloader: apple={}; code_sha256={}; code_bytes={}; entrypoint={:#x}; NOR bytes inspected={}",
            bootloader.apple, bootloader.fingerprint.sha256, bootloader.fingerprint.bytes,
            bootloader.fingerprint.entrypoint, bootloader.read_bytes));
        let action = assembly::BootloaderAction::for_installed(&bootloader, &expected_bootloader);
        job.plan(action)?;
        session = transfer::inspect_storage(session, &helper, &bundle, extra_bytes, &mut job)?;
        let nor = firmware::backup(&mut session, identity, action, &mut job)?;
        let (mut artifacts, themes) = firmware::assemble(
            &mut session,
            &ipsw,
            &nor,
            &bundle,
            &data,
            hardware_policy,
            &mut job,
        )?;
        let osos_path = data.join("firmware/osos-2.0.5.bin");
        if osos_path.is_file() {
            let osos_bytes = std::fs::read(&osos_path)?;
            artifacts.osos = crate::store::assemble_revisionos_firmware(&catalog, &osos_bytes)
                .map_err(|e| -> Box<dyn std::error::Error + Send + Sync> { e.into() })?;
        }
        transfer::install(
            session,
            &helper,
            &artifacts,
            themes.as_ref(),
            &extras,
            action,
            &mut job,
        )
    })();
    job.finish(outcome)
}
