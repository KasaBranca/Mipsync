use crate::settings::{ensure_dir, HubSettings};
use serde::Deserialize;
use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

const API_BASE: &str = "https://api.github.com";
const REPO: &str = "Lameguy64/PSn00bSDK";

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
struct Release {
    tag_name: String,
    draft: bool,
    prerelease: bool,
    #[serde(default)]
    assets: Vec<Asset>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
struct Asset {
    name: String,
    browser_download_url: String,
}

#[derive(Clone, Debug, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolchainState {
    pub installed: bool,
    pub version: Option<String>,
    pub root_dir: Option<String>,
    pub message: Option<String>,
}

fn fetch_releases() -> Result<Vec<Release>, String> {
    let url = format!("{API_BASE}/repos/{REPO}/releases?per_page=20");
    let client = reqwest::blocking::Client::builder()
        .user_agent("mipsync-hub")
        .build()
        .map_err(|e| e.to_string())?;
    let resp = client.get(url).send().map_err(|e| e.to_string())?;
    if !resp.status().is_success() {
        return Err(format!("GitHub API error: {}", resp.status()));
    }
    resp.json::<Vec<Release>>().map_err(|e| e.to_string())
}

fn pick_windows_zip(release: &Release) -> Option<Asset> {
    let mut zips: Vec<&Asset> = release
        .assets
        .iter()
        .filter(|a| a.name.to_lowercase().ends_with(".zip"))
        .collect();
    zips.sort_by_key(|a| a.name.len());
    zips.into_iter()
        .find(|a| {
            let n = a.name.to_lowercase();
            (n.contains("windows") || n.contains("win")) && !n.contains("source")
        })
        .cloned()
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
        if f.name().ends_with('/') {
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

fn find_sdk_root(dir: &Path) -> Option<PathBuf> {
    // Release zips usually contain bin/, lib/libpsn00b/, share/psn00bsdk/
    let expected = [
        dir.join("bin"),
        dir.join("lib").join("libpsn00b"),
        dir.join("share").join("psn00bsdk"),
    ];
    if expected.iter().all(|p| p.exists()) {
        return Some(dir.to_path_buf());
    }
    // Sometimes zip has a single top-level directory.
    let mut candidates = Vec::new();
    if let Ok(rd) = fs::read_dir(dir) {
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                candidates.push(p);
            }
        }
    }
    for c in candidates {
        let expected = [
            c.join("bin"),
            c.join("lib").join("libpsn00b"),
            c.join("share").join("psn00bsdk"),
        ];
        if expected.iter().all(|p| p.exists()) {
            return Some(c);
        }
    }
    None
}

pub fn get_state(settings: &HubSettings) -> ToolchainState {
    if let Some(root) = settings.psn00bsdk_dir() {
        let ok = root.is_dir() && find_sdk_root(&root).is_some();
        return ToolchainState {
            installed: ok,
            version: None,
            root_dir: Some(root.to_string_lossy().into_owned()),
            message: None,
        };
    }
    ToolchainState {
        installed: false,
        version: None,
        root_dir: None,
        message: None,
    }
}

pub fn install(settings: &mut HubSettings) -> Result<ToolchainState, String> {
    let releases = fetch_releases()?;
    let release = releases
        .into_iter()
        .find(|r| !r.draft && !r.prerelease)
        .ok_or_else(|| "No stable PSn00bSDK release found".to_string())?;
    let asset = pick_windows_zip(&release).ok_or_else(|| "No Windows ZIP asset found".to_string())?;

    let installs_root = settings.installs_root_dir();
    let toolchains_root = installs_root.join("toolchains").join("psn00bsdk");
    ensure_dir(&toolchains_root)?;

    let dest_dir = toolchains_root.join(&release.tag_name);
    if dest_dir.exists() {
        fs::remove_dir_all(&dest_dir).map_err(|e| e.to_string())?;
    }
    ensure_dir(&dest_dir)?;

    let tmp_zip = dest_dir.join("_download.zip");
    download_to_file(&asset.browser_download_url, &tmp_zip)?;
    extract_zip(&tmp_zip, &dest_dir)?;
    let _ = fs::remove_file(&tmp_zip);

    let sdk_root = find_sdk_root(&dest_dir).ok_or_else(|| {
        "Installed archive did not contain expected PSn00bSDK layout (bin/, lib/libpsn00b/)".to_string()
    })?;

    settings.psn00bsdk_dir = Some(sdk_root.to_string_lossy().into_owned());
    settings.save()?;

    Ok(ToolchainState {
        installed: true,
        version: Some(release.tag_name),
        root_dir: Some(settings.psn00bsdk_dir.clone().unwrap_or_default()),
        message: Some("PSn00bSDK installed".into()),
    })
}
