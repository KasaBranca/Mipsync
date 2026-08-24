use super::registry::Registry;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HubSettings {
    #[serde(default)]
    pub installs_root: Option<String>,
    #[serde(default)]
    pub active_version: Option<String>,
    #[serde(default)]
    pub psn00bsdk_dir: Option<String>,
    #[serde(default)]
    pub openbios_path: Option<String>,
    #[serde(default)]
    pub installed_hub_version: Option<String>,
}

impl HubSettings {
    pub fn path() -> PathBuf {
        Registry::config_dir().join("hub-settings.json")
    }

    pub fn load() -> HubSettings {
        let path = Self::path();
        let Ok(text) = fs::read_to_string(path) else {
            return HubSettings::default();
        };
        serde_json::from_str(&text).unwrap_or_default()
    }

    pub fn save(&self) -> Result<(), String> {
        let path = Self::path();
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        let text = serde_json::to_string_pretty(self).map_err(|e| e.to_string())?;
        fs::write(path, text).map_err(|e| e.to_string())
    }

    pub fn installs_root_dir(&self) -> PathBuf {
        if let Some(p) = &self.installs_root {
            return PathBuf::from(p);
        }
        default_installs_root()
    }

    pub fn psn00bsdk_dir(&self) -> Option<PathBuf> {
        self.psn00bsdk_dir.as_ref().map(PathBuf::from)
    }
}

pub fn default_installs_root() -> PathBuf {
    Registry::config_dir().join("Installs")
}

/// Older builds stored settings under %LOCALAPPDATA% while installs used the same tree.
/// Newer builds use %APPDATA%\\MipsyncEngine for both.
pub fn migrate_legacy_storage(settings: &mut HubSettings) -> Result<(), String> {
    let config_dir = Registry::config_dir();
    let _ = ensure_dir(&config_dir);

    #[cfg(windows)]
    {
        if let Ok(local) = std::env::var("LOCALAPPDATA") {
            let legacy_root = PathBuf::from(local).join("MipsyncEngine");
            let legacy_settings = legacy_root.join("hub-settings.json");
            let canonical_settings = HubSettings::path();

            if !canonical_settings.is_file() && legacy_settings.is_file() {
                fs::copy(&legacy_settings, &canonical_settings).map_err(|e| e.to_string())?;
                if let Ok(text) = fs::read_to_string(&canonical_settings) {
                    if let Ok(loaded) = serde_json::from_str::<HubSettings>(&text) {
                        *settings = loaded;
                    }
                }
            }

            let legacy_installs = legacy_root.join("Installs");
            let canonical_installs = default_installs_root();
            if legacy_installs.is_dir() {
                let has_legacy_installs = fs::read_dir(&legacy_installs)
                    .map(|mut it| it.next().is_some())
                    .unwrap_or(false);
                if has_legacy_installs {
                    if settings.installs_root.is_none() {
                        settings.installs_root =
                            Some(canonical_installs.to_string_lossy().into_owned());
                    }
                    merge_install_dirs(&legacy_installs, &canonical_installs)?;
                    settings.save()?;
                }
            }
        }
    }

    Ok(())
}

fn merge_install_dirs(from: &Path, to: &Path) -> Result<(), String> {
    ensure_dir(to)?;
    for entry in fs::read_dir(from).map_err(|e| e.to_string())? {
        let entry = entry.map_err(|e| e.to_string())?;
        if !entry.file_type().map_err(|e| e.to_string())?.is_dir() {
            continue;
        }
        let name = entry.file_name();
        let dest = to.join(&name);
        if dest.exists() {
            continue;
        }
        copy_dir_all(&entry.path(), &dest)?;
    }
    Ok(())
}

fn copy_dir_all(from: &Path, to: &Path) -> Result<(), String> {
    ensure_dir(to)?;
    for entry in fs::read_dir(from).map_err(|e| e.to_string())? {
        let entry = entry.map_err(|e| e.to_string())?;
        let dest = to.join(entry.file_name());
        if entry.file_type().map_err(|e| e.to_string())?.is_dir() {
            copy_dir_all(&entry.path(), &dest)?;
        } else {
            fs::copy(&entry.path(), &dest).map_err(|e| e.to_string())?;
        }
    }
    Ok(())
}

pub fn ensure_dir(path: &Path) -> Result<(), String> {
    std::fs::create_dir_all(path).map_err(|e| e.to_string())
}
