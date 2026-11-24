use crate::rvx::ExtensionType;
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, time::Duration};
use thiserror::Error;

pub const DEFAULT_CATALOG_URL: &str =
    "https://raw.githubusercontent.com/aurelian-th/ipod-things/main/catalog.json";

#[derive(Debug, Error)]
pub enum CatalogError {
    #[error("Unsupported catalog schema {0}")]
    UnsupportedSchema(u32),
    #[error("Invalid catalog JSON: {0}")]
    Json(#[from] serde_json::Error),
    #[error("Failed to fetch catalog: {0}")]
    Http(#[from] reqwest::Error),
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Catalog {
    pub schema: u32,
    pub repo: String,
    pub branch: String,
    pub backbone_disclaimer: String,
    pub custom_build_notice: String,
    pub whats_new: Vec<WhatsNewItem>,
    pub categories: Vec<Category>,
    pub firmware: FirmwareCatalog,
    pub extensions: Vec<ExtensionItem>,
    pub games: Vec<BundleItem>,
    pub apps: Vec<BundleItem>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Category {
    pub id: ExtensionType,
    pub label: String,
    pub description: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct WhatsNewItem {
    pub id: String,
    pub title: String,
    pub summary: String,
    pub version: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct FirmwareCatalog {
    pub name: String,
    pub version: String,
    pub api: String,
    pub target: String,
    pub checks: usize,
    pub output_bytes: usize,
    pub requires_custom_build: bool,
    pub files: BTreeMap<String, FirmwareFile>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct FirmwareFile {
    pub path: String,
    pub bytes: u64,
    pub sha256: String,
    pub url: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ExtensionItem {
    pub id: String,
    pub name: String,
    #[serde(rename = "type")]
    pub extension_type: ExtensionType,
    pub version: String,
    pub author: String,
    pub stability: String,
    pub api: String,
    pub default_enabled: bool,
    pub requires_custom_core: bool,
    pub description: String,
    pub changelog: String,
    pub file: String,
    pub bytes: u64,
    pub sha256: String,
    pub url: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct BundleItem {
    pub id: String,
    pub folder: String,
    pub kind: String,
    pub name: String,
    pub version: String,
    pub author: String,
    pub description: String,
    pub changelog: String,
    pub total_bytes: u64,
    pub files: Vec<BundleFile>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct BundleFile {
    pub name: String,
    pub path: String,
    pub bytes: u64,
    pub sha256: String,
    pub url: String,
}

pub fn parse_catalog(bytes: &[u8]) -> Result<Catalog, CatalogError> {
    let catalog: Catalog = serde_json::from_slice(bytes)?;
    if catalog.schema != 1 {
        return Err(CatalogError::UnsupportedSchema(catalog.schema));
    }
    Ok(catalog)
}

pub fn fetch_catalog(url: &str) -> Result<Catalog, CatalogError> {
    let client = reqwest::blocking::Client::builder()
        .user_agent(concat!("revisionos-installer/", env!("CARGO_PKG_VERSION")))
        .timeout(Duration::from_secs(30))
        .build()?;
    let bytes = client.get(url).send()?.error_for_status()?.bytes()?;
    parse_catalog(&bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn parses_synthetic_and_optional_real_catalog() {
        let sample = serde_json::json!({
            "schema": 1,
            "repo": "aurelian-th/ipod-things",
            "branch": "main",
            "backbone_disclaimer": "Temporary RevolaOS backbone",
            "custom_build_notice": "Use RevisionOS revola.bin",
            "whats_new": [],
            "categories": [
                { "id": "customization", "label": "Customization", "description": "Look and feel" }
            ],
            "firmware": {
                "name": "RevisionOS Core",
                "version": "1.74.0",
                "api": "1.74",
                "target": "classic7g",
                "checks": 172,
                "output_bytes": 10728704,
                "requires_custom_build": true,
                "files": {}
            },
            "extensions": [],
            "games": [],
            "apps": []
        });
        let parsed = parse_catalog(&serde_json::to_vec(&sample).unwrap()).unwrap();
        assert_eq!(parsed.schema, 1);

        let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../../ipod-things/catalog.json");
        if path.is_file() {
            let raw = std::fs::read(&path).unwrap();
            let catalog = parse_catalog(&raw).unwrap();
            assert_eq!(catalog.extensions.len(), 22);
            assert_eq!(catalog.games.len(), 2);
            assert_eq!(catalog.apps.len(), 1);
            assert_eq!(catalog.categories.len(), 6);
            assert!(catalog.firmware.files.contains_key("firmware.json"));
        }
    }
}

