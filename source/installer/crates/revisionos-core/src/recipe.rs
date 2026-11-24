use crate::ipod_drive::sha256_hex;
use serde::Deserialize;
use std::collections::BTreeMap;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum RecipeError {
    #[error("Invalid recipe JSON: {0}")]
    Json(#[from] serde_json::Error),
    #[error("Unsupported recipe schema {0}")]
    UnsupportedSchema(u32),
    #[error("Missing recipe input {0}")]
    MissingInput(String),
    #[error("Recipe data hash or length mismatch")]
    DataMismatch,
    #[error("Apple input size or SHA-256 mismatch for {0}")]
    InputMismatch(String),
    #[error("Apple image verification check failed at offset {offset}")]
    CheckFailed { offset: usize },
    #[error("Recipe segment out of bounds")]
    SegmentBounds,
    #[error("Assembled size {actual} does not match expected {expected}")]
    OutputSizeMismatch { expected: usize, actual: usize },
}

#[derive(Deserialize)]
struct Check {
    name: String,
    offset: usize,
    hex: String,
}

#[derive(Deserialize)]
struct BlobFingerprint {
    bytes: usize,
    sha256: String,
}

#[derive(Deserialize)]
struct OutputSpec {
    bytes: usize,
}

#[derive(Deserialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
enum Segment {
    Input {
        name: String,
        offset: usize,
        bytes: usize,
    },
    Data {
        offset: usize,
        bytes: usize,
    },
    Zero {
        bytes: usize,
    },
}

#[derive(Deserialize)]
struct Recipe {
    schema: u32,
    inputs: BTreeMap<String, BlobFingerprint>,
    data: BlobFingerprint,
    output: OutputSpec,
    checks: Vec<Check>,
    segments: Vec<Segment>,
}

pub fn assemble_recipe(
    recipe_json: &[u8],
    recipe_data: &[u8],
    inputs: &BTreeMap<String, &[u8]>,
) -> Result<Vec<u8>, RecipeError> {
    let recipe: Recipe = serde_json::from_slice(recipe_json)?;
    if recipe.schema != 2 {
        return Err(RecipeError::UnsupportedSchema(recipe.schema));
    }
    if recipe_data.len() != recipe.data.bytes || sha256_hex(recipe_data) != recipe.data.sha256 {
        return Err(RecipeError::DataMismatch);
    }
    for (name, expected) in &recipe.inputs {
        let Some(&bytes) = inputs.get(name) else {
            return Err(RecipeError::MissingInput(name.clone()));
        };
        if bytes.len() != expected.bytes || sha256_hex(bytes) != expected.sha256 {
            return Err(RecipeError::InputMismatch(name.clone()));
        }
    }
    for check in &recipe.checks {
        let Some(&bytes) = inputs.get(&check.name) else {
            return Err(RecipeError::MissingInput(check.name.clone()));
        };
        let expected = hex::decode(&check.hex).map_err(|_| RecipeError::CheckFailed {
            offset: check.offset,
        })?;
        let end = check
            .offset
            .checked_add(expected.len())
            .ok_or(RecipeError::CheckFailed {
                offset: check.offset,
            })?;
        let slice = bytes
            .get(check.offset..end)
            .ok_or(RecipeError::CheckFailed {
                offset: check.offset,
            })?;
        if slice != expected.as_slice() {
            return Err(RecipeError::CheckFailed {
                offset: check.offset,
            });
        }
    }

    let mut out = Vec::with_capacity(recipe.output.bytes);
    for segment in &recipe.segments {
        match segment {
            Segment::Input {
                name,
                offset,
                bytes,
            } => {
                let src = inputs
                    .get(name)
                    .ok_or_else(|| RecipeError::MissingInput(name.clone()))?;
                let end = offset.checked_add(*bytes).ok_or(RecipeError::SegmentBounds)?;
                let slice = src.get(*offset..end).ok_or(RecipeError::SegmentBounds)?;
                out.extend_from_slice(slice);
            }
            Segment::Data { offset, bytes } => {
                let end = offset.checked_add(*bytes).ok_or(RecipeError::SegmentBounds)?;
                let slice = recipe_data
                    .get(*offset..end)
                    .ok_or(RecipeError::SegmentBounds)?;
                out.extend_from_slice(slice);
            }
            Segment::Zero { bytes } => {
                out.resize(out.len() + *bytes, 0);
            }
        }
    }
    if out.len() != recipe.output.bytes {
        return Err(RecipeError::OutputSizeMismatch {
            expected: recipe.output.bytes,
            actual: out.len(),
        });
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn assembles_synthetic_recipe_and_rejects_bad_check() {
        let input_bytes = b"ABCDEFGH".to_vec();
        let data_bytes = b"XY".to_vec();
        let recipe = serde_json::json!({
            "schema": 2,
            "interface": "classic7g-file-v1",
            "inputs": {
                "osos": {
                    "bytes": input_bytes.len(),
                    "sha256": sha256_hex(&input_bytes),
                }
            },
            "data": {
                "bytes": data_bytes.len(),
                "sha256": sha256_hex(&data_bytes),
            },
            "output": { "bytes": 8 },
            "checks": [
                { "name": "osos", "offset": 2, "hex": hex::encode(b"CD") }
            ],
            "segments": [
                { "kind": "input", "name": "osos", "offset": 0, "bytes": 4 },
                { "kind": "data", "offset": 0, "bytes": 2 },
                { "kind": "zero", "bytes": 2 }
            ]
        });
        let recipe_bytes = serde_json::to_vec(&recipe).unwrap();
        let mut map = BTreeMap::new();
        map.insert("osos".to_string(), input_bytes.as_slice());

        let assembled = assemble_recipe(&recipe_bytes, &data_bytes, &map).unwrap();
        assert_eq!(&assembled, b"ABCDXY\0\0");

        let bad_input = b"ABXXEFGH";
        assert!(matches!(
            assemble_recipe(&recipe_bytes, &data_bytes, &BTreeMap::from([("osos".into(), bad_input.as_slice())])),
            Err(RecipeError::InputMismatch(_))
        ));
    }
}

