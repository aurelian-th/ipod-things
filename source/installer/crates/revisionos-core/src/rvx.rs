use serde::{Deserialize, Serialize};
use thiserror::Error;

pub const RVX_MAGIC: u32 = 0x30585652; // RVX0
pub const RVX_FORMAT: u32 = 1;
pub const RVMF_MAGIC: u32 = 0x464d5652; // RVMF
pub const RVMF_VERSION: u32 = 3;

const RVX_HEADER_LEN: usize = 28;
const RVMF_HEADER_LEN: usize = 48;

#[derive(Debug, Error)]
pub enum RvxError {
    #[error("RVX file too short ({0} bytes)")]
    TooShort(usize),
    #[error("Invalid RVX header magic or format")]
    InvalidRvxHeader,
    #[error("Invalid RVMF manifest header")]
    InvalidManifestHeader,
    #[error("Invalid manifest offset or string table")]
    InvalidStringTable,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Stability {
    Alpha,
    Beta,
    Stable,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ExtensionType {
    Audio,
    Customization,
    Media,
    Utility,
    Connectivity,
    Developer,
}

impl ExtensionType {
    pub fn from_code(code: u32) -> Option<Self> {
        match code {
            1 => Some(Self::Audio),
            2 => Some(Self::Customization),
            3 => Some(Self::Media),
            4 => Some(Self::Utility),
            5 => Some(Self::Connectivity),
            6 => Some(Self::Developer),
            _ => None,
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::Audio => "Audio",
            Self::Customization => "Customization",
            Self::Media => "Media",
            Self::Utility => "Utility",
            Self::Connectivity => "Connectivity",
            Self::Developer => "Developer",
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct RvxMetadata {
    pub id: String,
    pub name: String,
    pub description: String,
    pub version: String,
    pub author: String,
    pub license: String,
    pub note: String,
    pub stability: Option<Stability>,
    pub extension_type: Option<ExtensionType>,
    pub api_major: u16,
    pub api_minor: u16,
}

fn read_u16_le(bytes: &[u8], off: usize) -> u16 {
    u16::from_le_bytes([bytes[off], bytes[off + 1]])
}

fn read_u32_le(bytes: &[u8], off: usize) -> u32 {
    u32::from_le_bytes([bytes[off], bytes[off + 1], bytes[off + 2], bytes[off + 3]])
}

fn read_cstr(table: &[u8], offset: usize) -> Result<String, RvxError> {
    if offset >= table.len() {
        return Err(RvxError::InvalidStringTable);
    }
    let slice = &table[offset..];
    let end = slice
        .iter()
        .position(|&b| b == 0)
        .ok_or(RvxError::InvalidStringTable)?;
    String::from_utf8(slice[..end].to_vec()).map_err(|_| RvxError::InvalidStringTable)
}

pub fn parse_rvx(bytes: &[u8]) -> Result<RvxMetadata, RvxError> {
    if bytes.len() < RVX_HEADER_LEN + RVMF_HEADER_LEN {
        return Err(RvxError::TooShort(bytes.len()));
    }
    if read_u32_le(bytes, 0) != RVX_MAGIC || read_u32_le(bytes, 4) != RVX_FORMAT {
        return Err(RvxError::InvalidRvxHeader);
    }
    let manifest_bytes = read_u32_le(bytes, 16) as usize;
    let manifest_end = RVX_HEADER_LEN
        .checked_add(manifest_bytes)
        .filter(|&end| end <= bytes.len())
        .ok_or(RvxError::InvalidManifestHeader)?;
    let manifest = &bytes[RVX_HEADER_LEN..manifest_end];
    if manifest.len() < RVMF_HEADER_LEN {
        return Err(RvxError::InvalidManifestHeader);
    }
    if read_u32_le(manifest, 0) != RVMF_MAGIC || read_u32_le(manifest, 4) != RVMF_VERSION {
        return Err(RvxError::InvalidManifestHeader);
    }
    let api_word = read_u32_le(manifest, 8);
    let flags = read_u32_le(manifest, 12);
    let module_count = read_u32_le(manifest, 16) as usize;
    let site_count = read_u32_le(manifest, 20) as usize;
    let strings_bytes = read_u32_le(manifest, 24) as usize;

    let mut text_offsets = [0usize; 7];
    for (idx, slot) in text_offsets.iter_mut().enumerate() {
        *slot = read_u16_le(manifest, 28 + idx * 2) as usize;
    }

    let table_offset = RVMF_HEADER_LEN + site_count * 12 + module_count * 8;
    let table_end = table_offset
        .checked_add(strings_bytes)
        .filter(|&end| end <= manifest.len())
        .ok_or(RvxError::InvalidStringTable)?;
    let table = &manifest[table_offset..table_end];

    let stability = match flags & 3 {
        1 => Some(Stability::Alpha),
        2 => Some(Stability::Beta),
        3 => Some(Stability::Stable),
        _ => None,
    };
    let extension_type = ExtensionType::from_code((flags >> 2) & 0xf);

    Ok(RvxMetadata {
        id: read_cstr(table, text_offsets[0])?,
        name: read_cstr(table, text_offsets[1])?,
        description: read_cstr(table, text_offsets[2])?,
        version: read_cstr(table, text_offsets[3])?,
        author: read_cstr(table, text_offsets[4])?,
        license: read_cstr(table, text_offsets[5])?,
        note: read_cstr(table, text_offsets[6])?,
        stability,
        extension_type,
        api_major: (api_word >> 16) as u16,
        api_minor: (api_word & 0xffff) as u16,
    })
}

#[cfg(test)]
pub(crate) mod test_helpers {
    pub fn build_test_rvx(id: &str, name: &str, version: &str, author: &str, flags: u32) -> Vec<u8> {
        let strings = [
            id,
            name,
            "Test description",
            version,
            author,
            "GPL-3.0-only",
            "",
        ];
        let mut table = Vec::new();
        let mut offsets = [0u16; 7];
        for (i, s) in strings.iter().enumerate() {
            offsets[i] = table.len() as u16;
            table.extend_from_slice(s.as_bytes());
            table.push(0);
        }

        let mut manifest = Vec::new();
        manifest.extend_from_slice(&super::RVMF_MAGIC.to_le_bytes());
        manifest.extend_from_slice(&super::RVMF_VERSION.to_le_bytes());
        manifest.extend_from_slice(&((1u32 << 16) | 74u32).to_le_bytes());
        manifest.extend_from_slice(&flags.to_le_bytes());
        manifest.extend_from_slice(&0u32.to_le_bytes()); // module_count
        manifest.extend_from_slice(&0u32.to_le_bytes()); // site_count
        manifest.extend_from_slice(&(table.len() as u32).to_le_bytes());
        for off in offsets {
            manifest.extend_from_slice(&off.to_le_bytes());
        }
        manifest.extend_from_slice(&0u16.to_le_bytes()); // content
        manifest.extend_from_slice(&0u32.to_le_bytes()); // content_count
        manifest.extend_from_slice(&table);

        let mut rvx = Vec::new();
        rvx.extend_from_slice(&super::RVX_MAGIC.to_le_bytes());
        rvx.extend_from_slice(&super::RVX_FORMAT.to_le_bytes());
        rvx.extend_from_slice(&((1u32 << 16) | 74u32).to_le_bytes());
        rvx.extend_from_slice(&0u32.to_le_bytes());
        rvx.extend_from_slice(&(manifest.len() as u32).to_le_bytes());
        rvx.extend_from_slice(&0u32.to_le_bytes());
        rvx.extend_from_slice(&0u32.to_le_bytes());
        rvx.extend_from_slice(&manifest);
        rvx
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn parses_synthetic_and_real_compiled_extensions() {
        let synth = test_helpers::build_test_rvx(
            "hold-standby-clock",
            "Hold Standby Clock",
            "1.0.0",
            "0xAu",
            2 | (2 << 2),
        );
        let synth_meta = parse_rvx(&synth).unwrap();
        assert_eq!(synth_meta.id, "hold-standby-clock");
        assert_eq!(synth_meta.stability, Some(Stability::Beta));
        assert_eq!(synth_meta.extension_type, Some(ExtensionType::Customization));
        assert_eq!((synth_meta.api_major, synth_meta.api_minor), (1, 74));

        let ext_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../../ipod-things/Extensions");
        if ext_dir.join("hold-standby-clock.rvx").is_file() {
            let hsc = std::fs::read(ext_dir.join("hold-standby-clock.rvx")).unwrap();
            let meta = parse_rvx(&hsc).unwrap();
            assert_eq!(meta.id, "hold-standby-clock");
            assert_eq!(meta.name, "Hold Standby Clock");
            assert_eq!(meta.version, "1.0.0");
            assert_eq!(meta.author, "0xAu");
            assert_eq!(meta.stability, Some(Stability::Beta));
            assert_eq!(meta.extension_type, Some(ExtensionType::Customization));
            assert_eq!((meta.api_major, meta.api_minor), (1, 74));

            let srv = std::fs::read(ext_dir.join("silent-remote-volume.rvx")).unwrap();
            let srv_meta = parse_rvx(&srv).unwrap();
            assert_eq!(srv_meta.id, "silent-remote-volume");
            assert_eq!(srv_meta.extension_type, Some(ExtensionType::Utility));

            let iap2 = std::fs::read(ext_dir.join("iap2.rvx")).unwrap();
            let iap2_meta = parse_rvx(&iap2).unwrap();
            assert_eq!(iap2_meta.id, "iap2");
            assert_eq!(iap2_meta.stability, Some(Stability::Stable));
            assert_eq!(iap2_meta.extension_type, Some(ExtensionType::Connectivity));
        }
    }

    #[test]
    fn rejects_invalid_header() {
        assert!(matches!(parse_rvx(b"short"), Err(RvxError::TooShort(_))));
        let mut bogus = vec![0u8; 128];
        bogus[0..4].copy_from_slice(b"NOPE");
        assert!(matches!(parse_rvx(&bogus), Err(RvxError::InvalidRvxHeader)));
    }
}

