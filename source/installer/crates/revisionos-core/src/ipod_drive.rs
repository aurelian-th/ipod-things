use crate::{
    plist::parse_manifest_plist,
    rvx::{parse_rvx, ExtensionType, Stability},
};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    fs,
    path::{Path, PathBuf},
};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum DriveError {
    #[error("Folder is not a valid iPod storage root (missing iPod_Control, Revola, or firmware files)")]
    NotAnIpod,
    #[error("I/O error reading iPod drive: {0}")]
    Io(#[from] std::io::Error),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FirmwareLayout {
    RevolaBackbone,
    RepriseBootloaderWithRevisionOs,
    RevolaRootLegacy,
    RepriseOs,
    Mixed,
    Fresh,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct InstalledExtension {
    pub id: String,
    pub name: String,
    pub version: String,
    pub author: String,
    pub extension_type: Option<ExtensionType>,
    pub stability: Option<Stability>,
    pub file_name: String,
    pub bytes: u64,
    pub sha256: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct InstalledBundle {
    pub id: String,
    pub folder: String,
    pub kind: String,
    pub name: String,
    pub version: String,
    pub total_bytes: u64,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct IpodDriveSummary {
    pub root: String,
    pub label: String,
    pub layout: FirmwareLayout,
    pub has_revola_bin: bool,
    pub has_loader_bin: bool,
    pub has_reprise_osos: bool,
    pub has_root_revola_bin: bool,
    pub reprise_osos_matches_revola: bool,
    pub migration_recommended: bool,
    pub extensions_count: usize,
    pub games_count: usize,
    pub reprise_games_count: usize,
    pub apple_games_count: usize,
    pub apps_count: usize,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct IpodScan {
    pub root: String,
    pub layout: FirmwareLayout,
    pub has_revola_bin: bool,
    pub revola_bin_sha256: Option<String>,
    pub has_loader_bin: bool,
    pub has_reprise_osos: bool,
    pub has_reprise_loader: bool,
    pub has_root_revola_bin: bool,
    pub has_root_loader_bin: bool,
    pub reprise_osos_matches_revola: bool,
    pub migration_recommended: bool,
    pub has_ipod_control: bool,
    pub extensions: Vec<InstalledExtension>,
    pub games: Vec<InstalledBundle>,
    pub reprise_games: Vec<InstalledBundle>,
    pub apple_games_count: usize,
    pub apps: Vec<InstalledBundle>,
}

pub fn sha256_hex(bytes: &[u8]) -> String {
    hex::encode(Sha256::digest(bytes))
}

pub fn files_identical(a: &Path, b: &Path) -> bool {
    let Ok(ma) = fs::metadata(a) else {
        return false;
    };
    let Ok(mb) = fs::metadata(b) else {
        return false;
    };
    if !ma.is_file() || !mb.is_file() || ma.len() != mb.len() {
        return false;
    }
    let (Ok(ba), Ok(bb)) = (fs::read(a), fs::read(b)) else {
        return false;
    };
    sha256_hex(&ba) == sha256_hex(&bb)
}

pub fn looks_like_ipod_root(path: &Path) -> bool {
    path.is_dir()
        && (path.join("iPod_Control").is_dir()
            || path.join("Revola").is_dir()
            || path.join("osos-cfw.bin").is_file()
            || path.join("revola.bin").is_file())
}

pub fn detect_in_roots(mount_roots: &[PathBuf]) -> Vec<IpodDriveSummary> {
    let mut found = Vec::new();
    for root in mount_roots {
        if looks_like_ipod_root(root) {
            if let Ok(scan) = scan_ipod_root(root) {
                found.push(scan.summary());
            }
            continue;
        }
        let Ok(entries) = fs::read_dir(root) else {
            continue;
        };
        for entry in entries.flatten() {
            let candidate = entry.path();
            if looks_like_ipod_root(&candidate) {
                if let Ok(scan) = scan_ipod_root(&candidate) {
                    found.push(scan.summary());
                }
            }
        }
    }
    found.sort_by(|a, b| a.root.cmp(&b.root));
    found.dedup_by(|a, b| a.root == b.root);
    found
}

pub fn platform_mount_roots() -> Vec<PathBuf> {
    #[cfg(target_os = "linux")]
    {
        let mut roots = vec![
            PathBuf::from("/mnt"),
            PathBuf::from("/media"),
            PathBuf::from("/run/media"),
        ];
        if let Ok(user) = std::env::var("USER") {
            roots.push(PathBuf::from("/media").join(&user));
            roots.push(PathBuf::from("/run/media").join(&user));
        }
        roots
    }
    #[cfg(target_os = "macos")]
    {
        vec![PathBuf::from("/Volumes")]
    }
    #[cfg(target_os = "windows")]
    {
        ('D'..='Z')
            .map(|letter| PathBuf::from(format!("{letter}:\\")))
            .collect()
    }
}

pub fn detect_ipod_drives() -> Vec<IpodDriveSummary> {
    detect_in_roots(&platform_mount_roots())
}

impl IpodScan {
    pub fn summary(&self) -> IpodDriveSummary {
        let label = Path::new(&self.root)
            .file_name()
            .and_then(|s| s.to_str())
            .filter(|s| !s.is_empty())
            .unwrap_or(&self.root)
            .to_owned();
        IpodDriveSummary {
            root: self.root.clone(),
            label,
            layout: self.layout,
            has_revola_bin: self.has_revola_bin,
            has_loader_bin: self.has_loader_bin,
            has_reprise_osos: self.has_reprise_osos,
            has_root_revola_bin: self.has_root_revola_bin,
            reprise_osos_matches_revola: self.reprise_osos_matches_revola,
            migration_recommended: self.migration_recommended,
            extensions_count: self.extensions.len(),
            games_count: self.games.len(),
            reprise_games_count: self.reprise_games.len(),
            apple_games_count: self.apple_games_count,
            apps_count: self.apps.len(),
        }
    }
}

fn dir_bytes(dir: &Path) -> u64 {
    fs::read_dir(dir)
        .ok()
        .into_iter()
        .flatten()
        .flatten()
        .filter_map(|e| e.metadata().ok())
        .filter(|m| m.is_file())
        .map(|m| m.len())
        .sum()
}

fn scan_bundles(dir: &Path, kind: &str) -> Vec<InstalledBundle> {
    let mut items = Vec::new();
    let Ok(entries) = fs::read_dir(dir) else {
        return items;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        let plist_path = path.join("Manifest.plist");
        let Ok(xml) = fs::read_to_string(&plist_path) else {
            continue;
        };
        if let Ok(manifest) = parse_manifest_plist(&xml) {
            let folder = entry.file_name().to_string_lossy().into_owned();
            let id = if manifest.build_identifier.is_empty() {
                folder.clone()
            } else {
                manifest.build_identifier
            };
            items.push(InstalledBundle {
                id,
                folder: folder.clone(),
                kind: kind.into(),
                name: if manifest.name.is_empty() {
                    folder
                } else {
                    manifest.name
                },
                version: manifest.version,
                total_bytes: dir_bytes(&path),
            });
        }
    }
    items.sort_by(|a, b| a.id.cmp(&b.id));
    items
}

fn scan_games_ro(dir: &Path) -> (Vec<InstalledBundle>, usize) {
    let mut homebrew = Vec::new();
    let mut apple_count = 0usize;
    let Ok(entries) = fs::read_dir(dir) else {
        return (homebrew, apple_count);
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        let plist_path = path.join("Manifest.plist");
        let Ok(xml) = fs::read_to_string(&plist_path) else {
            apple_count += 1;
            continue;
        };
        match parse_manifest_plist(&xml) {
            Ok(manifest) if !manifest.is_apple_signed() => {
                let folder = entry.file_name().to_string_lossy().into_owned();
                let id = if manifest.build_identifier.is_empty() {
                    folder.clone()
                } else {
                    manifest.build_identifier
                };
                homebrew.push(InstalledBundle {
                    id,
                    folder: folder.clone(),
                    kind: "game".into(),
                    name: if manifest.name.is_empty() {
                        folder
                    } else {
                        manifest.name
                    },
                    version: manifest.version,
                    total_bytes: dir_bytes(&path),
                });
            }
            _ => apple_count += 1,
        }
    }
    homebrew.sort_by(|a, b| a.id.cmp(&b.id));
    (homebrew, apple_count)
}

pub fn scan_ipod_root(root: &Path) -> Result<IpodScan, DriveError> {
    if !looks_like_ipod_root(root) {
        return Err(DriveError::NotAnIpod);
    }

    let revola_dir = root.join("Revola");
    let firmware_bin = revola_dir.join("Firmware/revola.bin");
    let loader_bin = revola_dir.join("Firmware/loader.bin");
    let legacy_bin = root.join("revola.bin");
    let legacy_loader = root.join("loader.bin");
    let reprise_osos = root.join("osos-cfw.bin");
    let reprise_loader = root.join("cfw-loader.bin");

    let has_firmware_bin = firmware_bin.is_file();
    let has_firmware_loader = loader_bin.is_file();
    let has_root_revola_bin = legacy_bin.is_file();
    let has_root_loader_bin = legacy_loader.is_file();
    let has_reprise_osos = reprise_osos.is_file();
    let has_reprise_loader = reprise_loader.is_file();
    let reprise_osos_matches_revola = files_identical(&reprise_osos, &firmware_bin);

    let mut extensions = Vec::new();
    if let Ok(entries) = fs::read_dir(revola_dir.join("Extensions")) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().and_then(|s| s.to_str()) != Some("rvx") {
                continue;
            }
            let Ok(bytes) = fs::read(&path) else {
                continue;
            };
            if let Ok(meta) = parse_rvx(&bytes) {
                extensions.push(InstalledExtension {
                    id: meta.id,
                    name: meta.name,
                    version: meta.version,
                    author: meta.author,
                    extension_type: meta.extension_type,
                    stability: meta.stability,
                    file_name: entry.file_name().to_string_lossy().into_owned(),
                    bytes: bytes.len() as u64,
                    sha256: sha256_hex(&bytes),
                });
            }
        }
    }
    extensions.sort_by(|a, b| a.id.cmp(&b.id));

    let games = scan_bundles(&revola_dir.join("Games"), "game");
    let (reprise_games, apple_games_count) = scan_games_ro(&root.join("iPod_Control/Games_RO"));
    let apps = scan_bundles(&revola_dir.join("Apps"), "app");

    let has_revola_backbone_files = has_firmware_bin
        || has_firmware_loader
        || !extensions.is_empty()
        || !games.is_empty()
        || !apps.is_empty();
    let has_revola_root_legacy = has_root_revola_bin || has_root_loader_bin;
    let has_reprise = has_reprise_osos || has_reprise_loader || !reprise_games.is_empty();

    let layout = if reprise_osos_matches_revola
        && !has_revola_root_legacy
        && reprise_games.is_empty()
    {
        FirmwareLayout::RepriseBootloaderWithRevisionOs
    } else if (has_reprise && (has_revola_backbone_files || has_revola_root_legacy))
        || (has_revola_backbone_files && has_revola_root_legacy)
    {
        FirmwareLayout::Mixed
    } else if has_reprise {
        FirmwareLayout::RepriseOs
    } else if has_revola_root_legacy {
        FirmwareLayout::RevolaRootLegacy
    } else if revola_dir.is_dir() {
        FirmwareLayout::RevolaBackbone
    } else {
        FirmwareLayout::Fresh
    };

    let bin_path = if has_firmware_bin {
        Some(firmware_bin)
    } else if has_root_revola_bin {
        Some(legacy_bin)
    } else {
        None
    };

    let revola_bin_sha256 = bin_path
        .as_ref()
        .and_then(|p| fs::read(p).ok())
        .map(|bytes| sha256_hex(&bytes));

    let migration_recommended = has_root_revola_bin
        || has_root_loader_bin
        || !reprise_games.is_empty()
        || ((has_reprise_osos || has_reprise_loader) && !reprise_osos_matches_revola);

    Ok(IpodScan {
        root: root.to_string_lossy().into_owned(),
        layout,
        has_revola_bin: has_firmware_bin || has_root_revola_bin,
        revola_bin_sha256,
        has_loader_bin: has_firmware_loader || has_root_loader_bin,
        has_reprise_osos,
        has_reprise_loader,
        has_root_revola_bin,
        has_root_loader_bin,
        reprise_osos_matches_revola,
        migration_recommended,
        has_ipod_control: root.join("iPod_Control").is_dir(),
        extensions,
        games,
        reprise_games,
        apple_games_count,
        apps,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rvx::test_helpers::build_test_rvx;
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

    #[test]
    fn scans_mock_disk_mode_ipod() {
        let dir = tempdir().unwrap();
        let ipod = dir.path().join("IPOD");
        fs::create_dir_all(ipod.join("iPod_Control")).unwrap();
        fs::create_dir_all(ipod.join("Revola/Firmware")).unwrap();
        fs::create_dir_all(ipod.join("Revola/Extensions")).unwrap();
        fs::create_dir_all(ipod.join("Revola/Games/2048")).unwrap();
        fs::write(ipod.join("Revola/Firmware/revola.bin"), b"firmware").unwrap();

        fs::write(
            ipod.join("Revola/Extensions/hold-standby-clock.rvx"),
            build_test_rvx(
                "hold-standby-clock",
                "Hold Standby Clock",
                "1.0.0",
                "0xAu",
                2 | (2 << 2),
            ),
        )
        .unwrap();
        fs::write(ipod.join("Revola/Games/2048/Manifest.plist"), SAMPLE_EAPP_PLIST).unwrap();

        let detected = detect_in_roots(&[dir.path().to_path_buf()]);
        assert_eq!(detected.len(), 1);
        assert_eq!(detected[0].extensions_count, 1);
        assert_eq!(detected[0].games_count, 1);
        assert!(!detected[0].migration_recommended);

        let scan = scan_ipod_root(&ipod).unwrap();
        assert_eq!(scan.layout, FirmwareLayout::RevolaBackbone);
        assert!(scan.has_revola_bin);
        assert_eq!(scan.extensions[0].id, "hold-standby-clock");
        assert_eq!(scan.games[0].id, "2048");
    }

    #[test]
    fn filters_apple_games_and_detects_renamed_revision_layout() {
        let dir = tempdir().unwrap();

        let ipod = dir.path().join("RENAMED");
        fs::create_dir_all(ipod.join("iPod_Control/Games_RO/1D000")).unwrap();
        fs::create_dir_all(ipod.join("iPod_Control/Games_RO/2048")).unwrap();
        fs::create_dir_all(ipod.join("Revola/Firmware")).unwrap();
        fs::write(ipod.join("Revola/Firmware/revola.bin"), b"same-firmware").unwrap();
        fs::write(ipod.join("Revola/Firmware/loader.bin"), b"loader").unwrap();
        fs::write(ipod.join("osos-cfw.bin"), b"same-firmware").unwrap();
        fs::write(ipod.join("cfw-loader.bin"), b"loader").unwrap();
        fs::write(
            ipod.join("iPod_Control/Games_RO/1D000/Manifest.plist"),
            SAMPLE_APPLE_DRM_PLIST,
        )
        .unwrap();
        fs::write(
            ipod.join("iPod_Control/Games_RO/2048/Manifest.plist"),
            SAMPLE_EAPP_PLIST,
        )
        .unwrap();

        let scan = scan_ipod_root(&ipod).unwrap();
        assert_eq!(scan.apple_games_count, 1);
        assert_eq!(scan.reprise_games.len(), 1);
        assert_eq!(scan.reprise_games[0].id, "2048");
        assert!(scan.reprise_osos_matches_revola);

        fs::remove_dir_all(ipod.join("iPod_Control/Games_RO/2048")).unwrap();
        let clean_scan = scan_ipod_root(&ipod).unwrap();
        assert_eq!(
            clean_scan.layout,
            FirmwareLayout::RepriseBootloaderWithRevisionOs
        );
        assert!(!clean_scan.migration_recommended);
    }

    #[test]
    fn detects_legacy_revola_layout() {
        let dir = tempdir().unwrap();
        let legacy_ipod = dir.path().join("LEGACY");
        fs::create_dir_all(legacy_ipod.join("iPod_Control")).unwrap();
        fs::write(legacy_ipod.join("revola.bin"), b"legacy-revola").unwrap();
        fs::write(legacy_ipod.join("loader.bin"), b"legacy-loader").unwrap();

        let legacy_scan = scan_ipod_root(&legacy_ipod).unwrap();
        assert_eq!(legacy_scan.layout, FirmwareLayout::RevolaRootLegacy);
        assert!(legacy_scan.has_root_revola_bin);
        assert!(legacy_scan.has_root_loader_bin);
        assert!(legacy_scan.migration_recommended);
    }
}
