mod device;
mod firmware;
mod install;
mod package;
mod store;
mod theme;
mod updates;
mod usb_permissions;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .manage(updates::UpdateState::default())
        .manage(device::UsbState::default())
        .manage(package::PackageState::default())
        .manage(firmware::FirmwareState::default())
        .manage(install::InstallState::default())
        .manage(store::StoreTaskState::default())
        .on_window_event(|window, event| {
            use tauri::Manager;
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                if window
                    .state::<install::InstallState>()
                    .running
                    .load(std::sync::atomic::Ordering::Acquire)
                    || window
                        .state::<updates::UpdateState>()
                        .installing
                        .load(std::sync::atomic::Ordering::Acquire)
                {
                    api.prevent_close();
                }
            }
        })
        .invoke_handler(tauri::generate_handler![
            usb_permissions::usb_permissions_ready,
            usb_permissions::enable_usb_access,
            device::discover_devices,
            device::check_device,
            package::choose_local_package,
            package::prepare_package,
            firmware::choose_firmware,
            firmware::cached_firmware,
            install::install,
            install::diagnostics::save_debug_zip,
            updates::check_installer_update,
            updates::install_installer_update,
            updates::open_installer_download,
            install::cancel_install,
            store::fetch_revision_catalog,
            store::detect_ipod_drives,
            store::pick_ipod_drive,
            store::scan_ipod_drive,
            store::apply_store_changes,
            store::disk_mode_upgrade_firmware,
            store::disk_mode_uninstall,
            store::migrate_ipod_drive,
            store::cancel_store_task,
            store::cached_firmware_status,
            store::list_nor_backups,
            store::open_backups_folder
        ])
        .build(tauri::generate_context!())
        .expect("error while building tauri application")
        .run(|app, event| {
            use tauri::Manager;
            if let tauri::RunEvent::ExitRequested { api, .. } = event {
                if app
                    .state::<install::InstallState>()
                    .running
                    .load(std::sync::atomic::Ordering::Acquire)
                    || app
                        .state::<updates::UpdateState>()
                        .installing
                        .load(std::sync::atomic::Ordering::Acquire)
                {
                    api.prevent_exit();
                }
            }
        });
}
