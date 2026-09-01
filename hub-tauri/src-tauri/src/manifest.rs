use serde::{Deserialize, Serialize};

const OWNER_REPO: &str = "KasaBranca/Mipsync";
const MANIFEST_LATEST_URL: &str =
    "https://github.com/KasaBranca/Mipsync/releases/latest/download/manifest.json";

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Manifest {
    pub schema_version: u32,
    pub generated_at: String,
    pub hub: HubManifest,
    pub editor: EditorManifest,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HubManifest {
    pub version: String,
    pub asset_name: String,
    pub download_url: String,
    #[serde(default)]
    pub release_notes_url: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EditorManifest {
    pub asset_name: String,
    #[serde(default)]
    pub releases: ReleasesField,
}

#[derive(Clone, Debug, Deserialize, Serialize, Default)]
#[serde(untagged)]
pub enum ReleasesField {
    #[default]
    None,
    One(EditorReleaseEntry),
    Many(Vec<EditorReleaseEntry>),
}

impl ReleasesField {
    pub fn into_vec(self) -> Vec<EditorReleaseEntry> {
        match self {
            ReleasesField::None => Vec::new(),
            ReleasesField::One(v) => vec![v],
            ReleasesField::Many(v) => v,
        }
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EditorReleaseEntry {
    pub version: String,
    pub title: String,
    pub download_url: String,
    #[serde(default)]
    pub published_at: Option<String>,
}

pub fn fetch_latest() -> Result<Manifest, String> {
    let client = reqwest::blocking::Client::builder()
        .user_agent("mipsync-hub")
        .build()
        .map_err(|e| e.to_string())?;

    let resp = client
        .get(MANIFEST_LATEST_URL)
        .send()
        .map_err(|e| e.to_string())?;

    if !resp.status().is_success() {
        return Err(format!(
            "manifest fetch failed: {} ({})",
            resp.status(),
            MANIFEST_LATEST_URL
        ));
    }

    let bytes = resp.bytes().map_err(|e| e.to_string())?;
    let s = strip_utf8_bom(&bytes);
    serde_json::from_str::<Manifest>(s).map_err(|e| {
        let head = s.chars().take(120).collect::<String>();
        format!("error decoding response body: {e}. head=`{head}`")
    })
}

pub fn repo() -> &'static str {
    OWNER_REPO
}

fn strip_utf8_bom(bytes: &[u8]) -> &str {
    let s = std::str::from_utf8(bytes).unwrap_or("");
    s.strip_prefix('\u{feff}').unwrap_or(s)
}
