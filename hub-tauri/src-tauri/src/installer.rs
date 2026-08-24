use super::settings::{ensure_dir, HubSettings};
use std::collections::BTreeMap;
use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

#[derive(Clone, Debug, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct InstalledEditor {
    pub version: String,
    pub root_dir: String,
    pub engine_exe: Option<String>,
}

pub fn canonical_editor_version(version: &str) -> Option<String> {
    let trimmed = version.trim();
    let numeric = trimmed
        .strip_prefix('v')
        .or_else(|| trimmed.strip_prefix('V'))
        .unwrap_or(trimmed);
    let mut parts = numeric.split('.');
    let major = parts.next()?;
    let minor = parts.next()?;
    let patch = parts.next()?;
    if parts.next().is_some()
        || major.is_empty()
        || minor.is_empty()
        || patch.is_empty()
        || !major.chars().all(|c| c.is_ascii_digit())
        || !minor.chars().all(|c| c.is_ascii_digit())
        || !patch.chars().all(|c| c.is_ascii_digit())
    {
        return None;
    }
    Some(format!("v{major}.{minor}.{patch}"))
}

pub fn versions_equal(a: &str, b: &str) -> bool {
    match (canonical_editor_version(a), canonical_editor_version(b)) {
        (Some(a), Some(b)) => a == b,
        _ => false,
    }
}

pub fn list_installed(settings: &HubSettings) -> Result<Vec<InstalledEditor>, String> {
    let root = settings.installs_root_dir();
    if !root.is_dir() {
        return Ok(Vec::new());
    }
    let mut by_version = BTreeMap::<String, InstalledEditor>::new();
    for entry in fs::read_dir(&root).map_err(|e| e.to_string())? {
        let entry = entry.map_err(|e| e.to_string())?;
        if !entry.file_type().map_err(|e| e.to_string())?.is_dir() {
            continue;
        }
        let folder_name = entry.file_name().to_string_lossy().into_owned();
        // Legacy scaffolding builds are intentionally hidden from the Hub UI;
        // non-version folders (including `toolchains`) are not editor installs.
        let Some(version) = canonical_editor_version(&folder_name) else {
            continue;
        };
        if version.starts_with("v0.") {
            continue;
        }
        let root_dir = entry.path();
        let exe = find_engine_exe_in(&root_dir);
        let candidate = InstalledEditor {
            version,
            root_dir: root_dir.to_string_lossy().into_owned(),
            engine_exe: exe.map(|p| p.to_string_lossy().into_owned()),
        };

        // A legacy no-prefix folder and a current `v`-prefixed folder may both
        // exist. Expose one logical install and prefer the current folder.
        let replace = by_version
            .get(&candidate.version)
            .map(|current| {
                let current_name = Path::new(&current.root_dir)
                    .file_name()
                    .and_then(|n| n.to_str())
                    .unwrap_or_default();
                (!current_name.starts_with('v') && folder_name.starts_with('v'))
                    || (current.engine_exe.is_none() && candidate.engine_exe.is_some())
            })
            .unwrap_or(true);
        if replace {
            by_version.insert(candidate.version.clone(), candidate);
        }
    }
    let mut result: Vec<_> = by_version.into_values().collect();
    result.sort_by(|a, b| a.version.cmp(&b.version));
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::{canonical_editor_version, versions_equal};

    #[test]
    fn canonicalizes_editor_versions() {
        assert_eq!(
            canonical_editor_version("2026.7.9").as_deref(),
            Some("v2026.7.9")
        );
        assert_eq!(
            canonical_editor_version("v2026.7.9").as_deref(),
            Some("v2026.7.9")
        );
        assert_eq!(
            canonical_editor_version("V2026.7.9").as_deref(),
            Some("v2026.7.9")
        );
    }

    #[test]
    fn rejects_non_editor_install_directories() {
        assert_eq!(canonical_editor_version("toolchains"), None);
        assert_eq!(canonical_editor_version("vtoolchains"), None);
        assert_eq!(canonical_editor_version("2026.7"), None);
    }

    #[test]
    fn compares_prefixed_and_unprefixed_versions() {
        assert!(versions_equal("v2026.7.9", "2026.7.9"));
        assert!(!versions_equal("v2026.7.9", "v2026.7.8"));
    }
}

pub fn install_from_url(
    settings: &HubSettings,
    version: &str,
    url: &str,
) -> Result<InstalledEditor, String> {
    let installs_root = settings.installs_root_dir();
    ensure_dir(&installs_root)?;

    let dest_dir = installs_root.join(version);
    if dest_dir.exists() {
        fs::remove_dir_all(&dest_dir).map_err(|e| e.to_string())?;
    }
    ensure_dir(&dest_dir)?;

    let tmp_zip = dest_dir.join("_download.zip");
    download_to_file(url, &tmp_zip)?;

    extract_zip(&tmp_zip, &dest_dir)?;
    let _ = fs::remove_file(&tmp_zip);

    let exe = find_engine_exe_in(&dest_dir);
    Ok(InstalledEditor {
        version: version.to_string(),
        root_dir: dest_dir.to_string_lossy().into_owned(),
        engine_exe: exe.map(|p| p.to_string_lossy().into_owned()),
    })
}

fn download_to_file(url: &str, out_path: &Path) -> Result<(), String> {
    let client = reqwest::blocking::Client::builder()
        .user_agent("mipsync-hub")
        .build()
        .map_err(|e| e.to_string())?;
    let mut resp = client.get(url).send().map_err(|e| e.to_string())?;
    if !resp.status().is_success() {
        return Err(format!("download failed: {}", resp.status()));
    }
    let mut out = fs::File::create(out_path).map_err(|e| e.to_string())?;
    let mut buf = Vec::new();
    resp.read_to_end(&mut buf).map_err(|e| e.to_string())?;
    out.write_all(&buf).map_err(|e| e.to_string())
}

fn extract_zip(zip_path: &Path, dest_dir: &Path) -> Result<(), String> {
    let file = fs::File::open(zip_path).map_err(|e| e.to_string())?;
    let mut archive = zip::ZipArchive::new(file).map_err(|e| e.to_string())?;
    for i in 0..archive.len() {
        let mut f = archive.by_index(i).map_err(|e| e.to_string())?;
        let outpath = match f.enclosed_name() {
            Some(p) => dest_dir.join(p),
            None => continue,
        };
        let is_dir = f.name().ends_with('/') || f.name().ends_with('\\');
        if is_dir {
            ensure_dir(&outpath)?;
        } else {
            if let Some(parent) = outpath.parent() {
                ensure_dir(parent)?;
            }
            let mut outfile = fs::File::create(&outpath).map_err(|e| e.to_string())?;
            std::io::copy(&mut f, &mut outfile).map_err(|e| e.to_string())?;
        }
    }
    Ok(())
}

fn find_engine_exe_in(root: &Path) -> Option<PathBuf> {
    // Fast path: editor installs place the engine next to the version folder.
    for name in ["MipsyncEngine.exe", "MipsyncEngine"] {
        let p = root.join(name);
        if p.is_file() {
            return Some(p);
        }
    }

    // Slow path: legacy/odd layouts. Keep depth small to avoid UI stalls.
    let names = ["MipsyncEngine.exe", "MipsyncEngine"];
    for entry in walkdir::WalkDir::new(root)
        .max_depth(2)
        .follow_links(false)
        .into_iter()
        .filter_map(Result::ok)
    {
        if !entry.file_type().is_file() {
            continue;
        }
        let file_name = entry.file_name().to_string_lossy();
        if names.iter().any(|n| file_name.eq_ignore_ascii_case(n)) {
            return Some(entry.path().to_path_buf());
        }
    }
    None
}
