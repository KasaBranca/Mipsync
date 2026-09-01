use serde::Deserialize;

const API_BASE: &str = "https://api.github.com";
const REPO: &str = "KasaBranca/Mipsync";

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
pub struct Release {
    pub tag_name: String,
    pub name: Option<String>,
    pub draft: bool,
    pub prerelease: bool,
    pub published_at: Option<String>,
    #[serde(default)]
    pub assets: Vec<Asset>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
pub struct Asset {
    pub name: String,
    pub browser_download_url: String,
    pub size: u64,
    pub content_type: Option<String>,
}

pub fn fetch_releases() -> Result<Vec<Release>, String> {
    let url = format!("{API_BASE}/repos/{REPO}/releases?per_page=100");
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

pub fn pick_windows_asset(release: &Release) -> Option<Asset> {
    // Prefer a Windows zip. If you standardize asset names later, update this matching.
    let mut candidates: Vec<&Asset> = release
        .assets
        .iter()
        .filter(|a| a.name.to_lowercase().ends_with(".zip"))
        .collect();

    candidates.sort_by_key(|a| a.name.len());

    candidates
        .into_iter()
        .find(|a| {
            let n = a.name.to_lowercase();
            n.contains("win") || n.contains("windows")
        })
        .or_else(|| {
            release
                .assets
                .iter()
                .find(|a| a.name.to_lowercase().ends_with(".zip"))
        })
        .cloned()
}

pub fn pick_windows_engine_asset(release: &Release) -> Option<Asset> {
    // Prefer engine/editor zip; avoid Hub zips.
    let mut candidates: Vec<&Asset> = release
        .assets
        .iter()
        .filter(|a| a.name.to_lowercase().ends_with(".zip"))
        .collect();

    candidates.sort_by_key(|a| a.name.len());

    let preferred = candidates.into_iter().find(|a| {
        let n = a.name.to_lowercase();
        (n.contains("engine") || n.contains("editor")) && !n.contains("hub") && (n.contains("win") || n.contains("windows"))
    });

    preferred
        .or_else(|| {
            release.assets.iter().find(|a| {
                let n = a.name.to_lowercase();
                (n.contains("engine") || n.contains("editor")) && !n.contains("hub") && n.ends_with(".zip")
            })
        })
        .cloned()
}
