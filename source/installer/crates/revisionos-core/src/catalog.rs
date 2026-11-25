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
    #[error("Invalid repository source: {0}")]
    InvalidRepo(String),
    #[error("Catalog failed integrity validation: {0}")]
    InvalidCatalog(String),
    #[error("Invalid catalog JSON: {0}")]
    Json(#[from] serde_json::Error),
    #[error("Failed to fetch catalog: {0}")]
    Http(#[from] reqwest::Error),
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct RepoSourceStatus {
    pub repo: String,
    pub url: String,
    pub ok: bool,
    pub error: Option<String>,
    pub added_extensions: usize,
    pub added_games: usize,
    pub added_apps: usize,
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
    #[serde(default)]
    pub sources: Vec<RepoSourceStatus>,
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
    #[serde(default)]
    pub source_repo: Option<String>,
    #[serde(default)]
    pub source_url: Option<String>,
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
    #[serde(default)]
    pub source_repo: Option<String>,
    #[serde(default)]
    pub source_url: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct BundleFile {
    pub name: String,
    pub path: String,
    pub bytes: u64,
    pub sha256: String,
    pub url: String,
}

pub fn is_valid_sha256_hex(s: &str) -> bool {
    s.len() == 64 && s.bytes().all(|b| b.is_ascii_hexdigit())
}

pub fn normalize_catalog_url(input: &str) -> Result<String, CatalogError> {
    let trimmed = input.trim();
    if trimmed.is_empty() {
        return Err(CatalogError::InvalidRepo("Empty repository URL".into()));
    }
    if trimmed.starts_with("https://") {
        if trimmed.ends_with(".json") {
            return Ok(trimmed.to_string());
        }
        return Err(CatalogError::InvalidRepo(format!(
            "Custom HTTPS catalog URL must end with .json: {trimmed}"
        )));
    }
    if trimmed.starts_with("http://") {
        return Err(CatalogError::InvalidRepo(
            "Only HTTPS repository URLs are allowed".into(),
        ));
    }
    let clean = trimmed
        .strip_prefix("https://github.com/")
        .unwrap_or(trimmed)
        .trim_end_matches('/');
    let (repo_part, branch) = match clean.split_once('@') {
        Some((r, b)) if !b.trim().is_empty() => (r, b.trim()),
        Some(_) => {
            return Err(CatalogError::InvalidRepo(format!(
                "Missing branch after @ in '{trimmed}'"
            )))
        }
        None => (clean, "main"),
    };
    let mut parts = repo_part.split('/');
    let (Some(owner), Some(repo), None) = (parts.next(), parts.next(), parts.next()) else {
        return Err(CatalogError::InvalidRepo(format!(
            "Expected owner/repo[@branch] or https://.../catalog.json, got '{trimmed}'"
        )));
    };
    let valid_ident = |s: &str| {
        !s.is_empty()
            && s.bytes()
                .all(|b| b.is_ascii_alphanumeric() || matches!(b, b'-' | b'_' | b'.'))
    };
    if !valid_ident(owner) || !valid_ident(repo) || !valid_ident(branch) {
        return Err(CatalogError::InvalidRepo(format!(
            "Invalid characters in repository reference '{trimmed}'"
        )));
    }
    Ok(format!(
        "https://raw.githubusercontent.com/{owner}/{repo}/{branch}/catalog.json"
    ))
}

pub fn parse_api_minor(api: &str) -> Option<(u32, u32)> {
    let mut parts = api.trim().trim_start_matches('v').split('.');
    let maj = parts.next()?.parse::<u32>().ok()?;
    let min = parts.next()?.parse::<u32>().ok()?;
    Some((maj, min))
}

pub fn api_compatible(item_api: &str, core_api: &str) -> bool {
    match (parse_api_minor(item_api), parse_api_minor(core_api)) {
        (Some((imaj, imin)), Some((cmaj, cmin))) => imaj == cmaj && imin <= cmin,
        _ => false,
    }
}

pub fn validate_catalog_integrity(catalog: &Catalog) -> Result<(), CatalogError> {
    for ext in &catalog.extensions {
        if !ext.url.starts_with("https://") {
            return Err(CatalogError::InvalidCatalog(format!(
                "Extension '{}' URL must use HTTPS",
                ext.id
            )));
        }
        if !is_valid_sha256_hex(&ext.sha256) {
            return Err(CatalogError::InvalidCatalog(format!(
                "Extension '{}' has invalid sha256",
                ext.id
            )));
        }
        if !api_compatible(&ext.api, &catalog.firmware.api) && catalog.firmware.api != "0.0" {
            // Allow listing so UI can display compatibility status, but ensure version string parses cleanly
            if parse_api_minor(&ext.api).is_none() {
                return Err(CatalogError::InvalidCatalog(format!(
                    "Extension '{}' has invalid api version '{}'",
                    ext.id, ext.api
                )));
            }
        }
    }
    for bundle in catalog.games.iter().chain(catalog.apps.iter()) {
        for file in &bundle.files {
            if !file.url.starts_with("https://") {
                return Err(CatalogError::InvalidCatalog(format!(
                    "Bundle '{}' file '{}' URL must use HTTPS",
                    bundle.id, file.name
                )));
            }
            if !is_valid_sha256_hex(&file.sha256) {
                return Err(CatalogError::InvalidCatalog(format!(
                    "Bundle '{}' file '{}' has invalid sha256",
                    bundle.id, file.name
                )));
            }
        }
    }
    Ok(())
}

pub fn merge_catalogs(
    mut primary: Catalog,
    extras: Vec<(String, Result<Catalog, String>)>,
) -> Catalog {
    use std::collections::HashSet;

    let core_api = primary.firmware.api.clone();
    let mut ext_ids: HashSet<String> = primary.extensions.iter().map(|e| e.id.clone()).collect();
    let mut game_ids: HashSet<String> = primary.games.iter().map(|g| g.id.clone()).collect();
    let mut app_ids: HashSet<String> = primary.apps.iter().map(|a| a.id.clone()).collect();

    let mut statuses = vec![RepoSourceStatus {
        repo: primary.repo.clone(),
        url: DEFAULT_CATALOG_URL.to_string(),
        ok: true,
        error: None,
        added_extensions: primary.extensions.len(),
        added_games: primary.games.len(),
        added_apps: primary.apps.len(),
    }];

    for (url, res) in extras {
        match res {
            Ok(extra) => {
                if let Err(err) = validate_catalog_integrity(&extra) {
                    statuses.push(RepoSourceStatus {
                        repo: extra.repo,
                        url,
                        ok: false,
                        error: Some(err.to_string()),
                        added_extensions: 0,
                        added_games: 0,
                        added_apps: 0,
                    });
                    continue;
                }
                let mut added_ext = 0usize;
                let mut added_games = 0usize;
                let mut added_apps = 0usize;
                let repo_label = extra.repo.clone();

                for mut ext in extra.extensions {
                    if !api_compatible(&ext.api, &core_api) {
                        continue;
                    }
                    if ext_ids.insert(ext.id.clone()) {
                        ext.source_repo = Some(repo_label.clone());
                        ext.source_url = Some(url.clone());
                        primary.extensions.push(ext);
                        added_ext += 1;
                    }
                }
                for mut game in extra.games {
                    if game_ids.insert(game.id.clone()) {
                        game.source_repo = Some(repo_label.clone());
                        game.source_url = Some(url.clone());
                        primary.games.push(game);
                        added_games += 1;
                    }
                }
                for mut app in extra.apps {
                    if app_ids.insert(app.id.clone()) {
                        app.source_repo = Some(repo_label.clone());
                        app.source_url = Some(url.clone());
                        primary.apps.push(app);
                        added_apps += 1;
                    }
                }
                statuses.push(RepoSourceStatus {
                    repo: repo_label,
                    url,
                    ok: true,
                    error: None,
                    added_extensions: added_ext,
                    added_games,
                    added_apps,
                });
            }
            Err(err) => {
                statuses.push(RepoSourceStatus {
                    repo: url.clone(),
                    url,
                    ok: false,
                    error: Some(err),
                    added_extensions: 0,
                    added_games: 0,
                    added_apps: 0,
                });
            }
        }
    }

    primary.sources = statuses;
    primary
}

pub fn parse_catalog(bytes: &[u8]) -> Result<Catalog, CatalogError> {
    let catalog: Catalog = serde_json::from_slice(bytes)?;
    if catalog.schema != 1 {
        return Err(CatalogError::UnsupportedSchema(catalog.schema));
    }
    Ok(catalog)
}

pub fn fetch_catalog(url: &str) -> Result<Catalog, CatalogError> {
    let normalized = normalize_catalog_url(url)?;
    let client = reqwest::blocking::Client::builder()
        .user_agent(concat!("revisionos-installer/", env!("CARGO_PKG_VERSION")))
        .timeout(Duration::from_secs(30))
        .build()?;
    let bytes = client.get(&normalized).send()?.error_for_status()?.bytes()?;
    parse_catalog(&bytes)
}

pub fn fetch_merged_catalog(
    primary_url: &str,
    extra_urls: &[String],
) -> Result<Catalog, CatalogError> {
    let primary = fetch_catalog(primary_url)?;
    let mut extras = Vec::new();
    let mut seen = std::collections::HashSet::new();
    seen.insert(normalize_catalog_url(primary_url).unwrap_or_else(|_| primary_url.to_string()));

    for raw in extra_urls {
        let trimmed = raw.trim();
        if trimmed.is_empty() {
            continue;
        }
        match normalize_catalog_url(trimmed) {
            Ok(norm) => {
                if !seen.insert(norm.clone()) {
                    continue;
                }
                let res = fetch_catalog(&norm).map_err(|e| e.to_string());
                extras.push((norm, res));
            }
            Err(e) => {
                extras.push((trimmed.to_string(), Err(e.to_string())));
            }
        }
    }
    Ok(merge_catalogs(primary, extras))
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

    #[test]
    fn custom_repo_normalization_integrity_api_and_merge() {
        assert_eq!(
            normalize_catalog_url("alice/my-repo").unwrap(),
            "https://raw.githubusercontent.com/alice/my-repo/main/catalog.json"
        );
        assert_eq!(
            normalize_catalog_url("alice/my-repo@dev").unwrap(),
            "https://raw.githubusercontent.com/alice/my-repo/dev/catalog.json"
        );
        assert!(normalize_catalog_url("http://insecure.example/catalog.json").is_err());

        assert!(api_compatible("1.74", "1.74"));
        assert!(api_compatible("1.70", "1.74"));
        assert!(!api_compatible("1.80", "1.74"));
        assert!(!api_compatible("2.0", "1.74"));

        let sha_good = "a".repeat(64);
        let mk_ext = |id: &str, api: &str| ExtensionItem {
            id: id.into(),
            name: id.into(),
            extension_type: ExtensionType::Utility,
            version: "1.0.0".into(),
            author: "Community".into(),
            stability: "beta".into(),
            api: api.into(),
            default_enabled: false,
            requires_custom_core: false,
            description: "Desc".into(),
            changelog: "Init".into(),
            file: format!("{id}.rvx"),
            bytes: 128,
            sha256: sha_good.clone(),
            url: format!("https://example.com/{id}.rvx"),
            source_repo: None,
            source_url: None,
        };
        let primary: Catalog = serde_json::from_value(serde_json::json!({
            "schema": 1,
            "repo": "aurelian-th/ipod-things",
            "branch": "main",
            "backbone_disclaimer": "",
            "custom_build_notice": "",
            "whats_new": [],
            "categories": [],
            "firmware": {
                "name": "RevisionOS Core",
                "version": "1.74.0",
                "api": "1.74",
                "target": "classic7g",
                "checks": 1,
                "output_bytes": 1,
                "requires_custom_build": true,
                "files": {}
            },
            "extensions": [mk_ext("official-ext", "1.74")],
            "games": [],
            "apps": []
        }))
        .unwrap();

        let mut third_party = primary.clone();
        third_party.repo = "alice/extra-rvx".into();
        third_party.extensions = vec![
            mk_ext("official-ext", "1.74"), // duplicate must not override primary
            mk_ext("too-new-ext", "1.90"),  // incompatible API skipped
            mk_ext("community-ext", "1.73"),
        ];

        let merged = merge_catalogs(
            primary,
            vec![
                (
                    "https://raw.githubusercontent.com/alice/extra-rvx/main/catalog.json".into(),
                    Ok(third_party),
                ),
                (
                    "https://raw.githubusercontent.com/bob/broken/main/catalog.json".into(),
                    Err("404 Not Found".into()),
                ),
            ],
        );

        assert_eq!(merged.extensions.len(), 2);
        assert_eq!(merged.extensions[0].source_repo, None);
        assert_eq!(merged.extensions[1].id, "community-ext");
        assert_eq!(
            merged.extensions[1].source_repo.as_deref(),
            Some("alice/extra-rvx")
        );
        assert_eq!(merged.sources.len(), 3);
        assert!(merged.sources[1].ok);
        assert_eq!(merged.sources[1].added_extensions, 1);
        assert!(!merged.sources[2].ok);
    }
}

