use super::{
    job::{InstallOutcome, Job},
    Result,
};
use crate::store::DownloadItem;
use reprise_bundle::{
    assembly::{self, Artifacts},
    VerifiedBundle,
};
use reprise_device::{Session, UploadHelper, UploadOptions};
use std::io::Cursor;

pub(super) fn inspect_storage(
    session: Session,
    helper: &UploadHelper,
    bundle: &VerifiedBundle,
    extra_bytes: u32,
    job: &mut Job<'_>,
) -> Result<Session> {
    job.stage("storage", "Checking storage")?;
    let required = assembly::disk_bytes(bundle)?
        .checked_mul(2)
        .and_then(|total| total.checked_add(crate::theme::disk_bytes(bundle)?))
        .and_then(|total| total.checked_add(extra_bytes))
        .ok_or("Package disk size overflow")?;
    let directory = job.directory.clone();
    let (returned, storage) =
        session.inspect_storage_with_diagnostics(helper, required, Some(&directory), |detail| {
            job.log(detail)
        })?;

    job.save_json("storage.json", &storage)?;
    job.emit(
        format!(
            "{} bytes free; companion present: {}; OSOS present: {}",
            storage.free_bytes, storage.companion_exists, storage.osos_exists
        ),
        None,
    );

    Ok(returned)
}

pub(super) fn install(
    mut session: Session,
    helper: &UploadHelper,
    artifacts: &Artifacts,
    themes: Option<&crate::theme::Themes>,
    extras: &[DownloadItem],
    action: assembly::BootloaderAction,
    job: &mut Job<'_>,
) -> Result<InstallOutcome> {
    let mut files: Vec<(&'static str, String, String, String, &[u8])> = Vec::new();
    if let Some(themes) = themes {
        files.push((
            "uploadDefaultTheme",
            "uploadDefaultTheme.json".into(),
            "stock artwork".into(),
            "/themes/default/theme.bin".into(),
            &themes.default,
        ));
        files.push((
            "uploadDarkTheme",
            "uploadDarkTheme.json".into(),
            "Dark Mode artwork".into(),
            "/themes/dark/theme.bin".into(),
            &themes.dark,
        ));
    }
    files.push((
        "uploadLoader",
        "uploadLoader.json".into(),
        "companion loader".into(),
        "/Revola/Firmware/loader.bin".into(),
        &artifacts.companion,
    ));
    files.push((
        "uploadLoader",
        "uploadLoader-root.json".into(),
        "boot companion loader".into(),
        "/cfw-loader.bin".into(),
        &artifacts.companion,
    ));
    files.push((
        "uploadOsos",
        "uploadOsos.json".into(),
        "RevisionOS core".into(),
        "/Revola/Firmware/revola.bin".into(),
        &artifacts.osos,
    ));
    files.push((
        "uploadOsos",
        "uploadOsos-root.json".into(),
        "boot RevisionOS core".into(),
        "/osos-cfw.bin".into(),
        &artifacts.osos,
    ));
    for (extra_idx, extra) in extras.iter().enumerate() {
        let name = extra
            .destination
            .rsplit('/')
            .next()
            .unwrap_or("extension")
            .to_string();
        files.push((
            "uploadContent",
            format!("uploadContent-{extra_idx}.json"),
            name,
            extra.destination.clone(),
            &extra.bytes,
        ));
    }
    for (stage, report_name, name, destination, bytes) in files {
        job.stage(stage, &format!("Preparing {name}"))?;
        let log_job = std::cell::RefCell::new(&mut *job);
        let uploaded = session.upload_with_log(
            helper,
            &mut Cursor::new(bytes),
            UploadOptions {
                destination,
                overwrite: true,
            },
            |p| {
                let detail = match p.stage {
                    "hash" => format!("Checking {name}"),
                    "launch" => "Starting transfer helper".into(),
                    "upload" => format!("Uploading {name}"),
                    "write" => format!("Writing {name}"),
                    "verify" => format!("Verifying {name}"),
                    "dfu-return" => "Reconnecting to iPod".into(),
                    "complete" => format!("Verified {name}"),
                    _ => format!("Preparing {name}"),
                };
                let progress = match p.stage {
                    "hash" | "upload" | "write" | "verify" | "complete" => {
                        Some((p.completed, p.total))
                    }
                    _ => None,
                };
                log_job.borrow_mut().emit(detail, progress);
            },
            |detail| log_job.borrow_mut().log(detail),
        )?;
        session = uploaded.session;
        job.save_json(&report_name, &uploaded.report)?;
    }
    if action == assembly::BootloaderAction::Keep {
        job.begin_restart()?;
        session.restart()?;
        job.emit(
            "UPDATE: disk files verified; restart requested; NOR unchanged",
            None,
        );
        return Ok(InstallOutcome::FilesUpdated);
    }
    job.begin_bootloader()?;
    session.install_bootloader(&artifacts.nor_installer, |done, total| {
        job.emit(
            "Sending bootloader installer",
            Some((done as u64, total as u64)),
        )
    })?;
    job.emit("Bootloader installer sent", None);
    Ok(InstallOutcome::InstallerSent)
}
