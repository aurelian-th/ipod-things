use serde::{Deserialize, Serialize};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum PlistError {
    #[error("Missing root <dict> in Manifest.plist")]
    MissingRootDict,
    #[error("Malformed XML token near byte {0}")]
    Malformed(usize),
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct BundleManifestFile {
    pub path: String,
    pub size: Option<u64>,
    pub drm: bool,
    pub verify: bool,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct BundleManifest {
    pub name: String,
    pub build_identifier: String,
    pub version: String,
    pub guid: String,
    pub heap_size: Option<u64>,
    pub files: Vec<BundleManifestFile>,
}

impl BundleManifest {
    pub fn has_eapp_executable(&self) -> bool {
        self.files
            .iter()
            .any(|f| f.path.to_ascii_lowercase().ends_with(".eapp"))
    }

    pub fn is_apple_signed(&self) -> bool {
        self.files.iter().any(|f| f.drm || f.verify) || !self.has_eapp_executable()
    }
}

fn unescape_xml(raw: &str) -> String {
    raw.replace("&amp;", "&")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&apos;", "'")
}

fn read_tag<'a>(src: &'a str, pos: &mut usize) -> Result<Option<(bool, &'a str, &'a str)>, PlistError> {
    let bytes = src.as_bytes();
    while *pos < bytes.len() {
        let Some(lt_rel) = src[*pos..].find('<') else {
            *pos = bytes.len();
            return Ok(None);
        };
        let lt = *pos + lt_rel;
        if src[lt..].starts_with("<!--") {
            let Some(end_rel) = src[lt + 4..].find("-->") else {
                return Err(PlistError::Malformed(lt));
            };
            *pos = lt + 4 + end_rel + 3;
            continue;
        }
        if src[lt..].starts_with("<?") || src[lt..].starts_with("<!") {
            let Some(gt_rel) = src[lt..].find('>') else {
                return Err(PlistError::Malformed(lt));
            };
            *pos = lt + gt_rel + 1;
            continue;
        }
        let Some(gt_rel) = src[lt..].find('>') else {
            return Err(PlistError::Malformed(lt));
        };
        let tag_raw = src[lt + 1..lt + gt_rel].trim();
        *pos = lt + gt_rel + 1;
        if let Some(closing) = tag_raw.strip_prefix('/') {
            return Ok(Some((true, closing.trim(), "")));
        }
        let self_closing = tag_raw.ends_with('/');
        let name = tag_raw.trim_end_matches('/').trim();
        if self_closing {
            return Ok(Some((false, name, "")));
        }
        return Ok(Some((false, name, "")));
    }
    Ok(None)
}

fn read_element_text(src: &str, pos: &mut usize, tag: &str) -> Result<String, PlistError> {
    let end_tag = format!("</{tag}>");
    let start = *pos;
    let Some(rel) = src[start..].find(&end_tag) else {
        return Err(PlistError::Malformed(start));
    };
    *pos = start + rel + end_tag.len();
    Ok(unescape_xml(src[start..start + rel].trim()))
}

fn skip_compound(src: &str, pos: &mut usize, tag: &str) -> Result<(), PlistError> {
    let mut depth = 1usize;
    while depth > 0 {
        let Some((closing, name, _)) = read_tag(src, pos)? else {
            return Err(PlistError::Malformed(*pos));
        };
        if name == tag {
            if closing {
                depth -= 1;
            } else {
                depth += 1;
            }
        }
    }
    Ok(())
}

fn skip_value(src: &str, pos: &mut usize) -> Result<(), PlistError> {
    while let Some((closing, name, _)) = read_tag(src, pos)? {
        if closing {
            continue;
        }
        match name {
            "true" | "false" => return Ok(()),
            "dict" | "array" => return skip_compound(src, pos, name),
            _ => {
                let _ = read_element_text(src, pos, name)?;
                return Ok(());
            }
        }
    }
    Ok(())
}

fn read_scalar(src: &str, pos: &mut usize) -> Result<Option<(String, String)>, PlistError> {
    while let Some((closing, name, _)) = read_tag(src, pos)? {
        if closing {
            return Ok(None);
        }
        match name {
            "true" => return Ok(Some(("bool".into(), "true".into()))),
            "false" => return Ok(Some(("bool".into(), "false".into()))),
            "dict" | "array" => {
                skip_compound(src, pos, name)?;
                return Ok(Some((name.into(), String::new())));
            }
            _ => {
                let text = read_element_text(src, pos, name)?;
                return Ok(Some((name.into(), text)));
            }
        }
    }
    Ok(None)
}

fn parse_files_array(src: &str, pos: &mut usize) -> Result<Vec<BundleManifestFile>, PlistError> {
    let mut files = Vec::new();
    while let Some((closing, name, _)) = read_tag(src, pos)? {
        if closing && name == "array" {
            break;
        }
        if !closing && name == "dict" {
            let mut path = None;
            let mut size = None;
            let mut drm = false;
            let mut verify = false;
            while let Some((c2, n2, _)) = read_tag(src, pos)? {
                if c2 && n2 == "dict" {
                    break;
                }
                if !c2 && n2 == "key" {
                    let key = read_element_text(src, pos, "key")?;
                    if let Some((kind, val)) = read_scalar(src, pos)? {
                        if key == "Path" && kind == "string" {
                            path = Some(val);
                        } else if key == "Size" && kind == "integer" {
                            size = val.parse::<u64>().ok();
                        } else if key == "DRM" && kind == "bool" {
                            drm = val == "true";
                        } else if key == "Verify" && kind == "bool" {
                            verify = val == "true";
                        }
                    }
                }
            }
            if let Some(path) = path {
                files.push(BundleManifestFile {
                    path,
                    size,
                    drm,
                    verify,
                });
            }
        } else if !closing {
            skip_value(src, pos)?;
        }
    }
    Ok(files)
}

pub fn parse_manifest_plist(xml: &str) -> Result<BundleManifest, PlistError> {
    let mut pos = 0usize;
    let mut in_root = false;
    while let Some((closing, name, _)) = read_tag(xml, &mut pos)? {
        if !closing && name == "dict" {
            in_root = true;
            break;
        }
    }
    if !in_root {
        return Err(PlistError::MissingRootDict);
    }

    let mut manifest = BundleManifest {
        name: String::new(),
        build_identifier: String::new(),
        version: String::new(),
        guid: String::new(),
        heap_size: None,
        files: Vec::new(),
    };

    while let Some((closing, tag, _)) = read_tag(xml, &mut pos)? {
        if closing && tag == "dict" {
            break;
        }
        if !closing && tag == "key" {
            let key = read_element_text(xml, &mut pos, "key")?;
            let Some((vclosing, vtag, _)) = read_tag(xml, &mut pos)? else {
                return Err(PlistError::Malformed(pos));
            };
            if vclosing {
                return Err(PlistError::Malformed(pos));
            }
            if key == "Files" && vtag == "array" {
                manifest.files = parse_files_array(xml, &mut pos)?;
                continue;
            }
            match vtag {
                "dict" | "array" => skip_compound(xml, &mut pos, vtag)?,
                "true" | "false" => {}
                _ => {
                    let val = read_element_text(xml, &mut pos, vtag)?;
                    match key.as_str() {
                        "Name" => manifest.name = val,
                        "BuildIdentifier" => manifest.build_identifier = val,
                        "Version" => manifest.version = val,
                        "GUID" => manifest.guid = val,
                        "HeapSize" => manifest.heap_size = val.parse::<u64>().ok(),
                        _ => {}
                    }
                }
            }
        }
    }
    Ok(manifest)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn parses_synthetic_and_real_game_plists() {
        let xml = r#"<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
<dict>
  <key>Name</key><string>Doodle Jump</string>
  <key>BuildIdentifier</key><string>doodlejump</string>
  <key>GUID</key><string>doodlejump</string>
  <key>Version</key><string>0.2.0</string>
  <key>HeapSize</key><integer>5120</integer>
  <key>Files</key>
  <array>
    <dict><key>Path</key><string>doodlejump.eapp</string><key>Size</key><integer>48240</integer></dict>
  </array>
</dict>
</plist>"#;
        let parsed = parse_manifest_plist(xml).unwrap();
        assert_eq!(parsed.name, "Doodle Jump");
        assert_eq!(parsed.build_identifier, "doodlejump");
        assert_eq!(parsed.heap_size, Some(5120));
        assert_eq!(parsed.files.len(), 1);
        assert!(parsed.has_eapp_executable());
        assert!(!parsed.is_apple_signed());

        let apple_xml = r#"<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
  <key>BuildIdentifier</key><string>1462222666-fc4e9559d11</string>
  <key>Files</key><array>
    <dict><key>DRM</key><true/><key>Path</key><string>Executables/Phase.bin</string><key>Size</key><integer>891960</integer><key>Verify</key><true/></dict>
  </array>
</dict></plist>"#;
        let apple = parse_manifest_plist(apple_xml).unwrap();
        assert!(!apple.has_eapp_executable());
        assert!(apple.is_apple_signed());

        let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../ipod-things");
        if root.join("Games/doodlejump/Manifest.plist").is_file() {
            let doodle_xml =
                std::fs::read_to_string(root.join("Games/doodlejump/Manifest.plist")).unwrap();
            let doodle = parse_manifest_plist(&doodle_xml).unwrap();
            assert_eq!(doodle.name, "Doodle Jump");
            assert!(doodle.files.iter().any(|f| f.path == "doodlejump.eapp"));
        }
    }
}

