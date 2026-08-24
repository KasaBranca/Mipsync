use super::paths;
use super::project::ProjectInfo;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

#[derive(Deserialize, Serialize)]
struct HubFile {
    version: u32,
    #[serde(default)]
    projects: Vec<ProjectInfo>,
}

pub struct Registry;

impl Registry {
    pub fn config_dir() -> PathBuf {
        #[cfg(windows)]
        {
            if let Ok(appdata) = std::env::var("APPDATA") {
                return PathBuf::from(appdata).join("MipsyncEngine");
            }
        }
        #[cfg(not(windows))]
        {
            if let Some(home) = dirs::home_dir() {
                return home.join(".config").join("nostalty");
            }
        }
        std::env::current_dir()
            .unwrap_or_else(|_| PathBuf::from("."))
            .join(".nostalty")
    }

    pub fn path() -> PathBuf {
        Self::config_dir().join("hub.json")
    }

    pub fn load() -> Result<Vec<ProjectInfo>, String> {
        let path = Self::path();
        if !path.is_file() {
            return Ok(Vec::new());
        }
        let text = fs::read_to_string(&path).map_err(|e| e.to_string())?;
        let file: HubFile = serde_json::from_str(&text).unwrap_or(HubFile {
            version: 1,
            projects: Vec::new(),
        });
        let before = file.projects.len();
        let deduped = dedupe_projects(file.projects);
        if deduped.len() != before {
            let _ = Self::save(&deduped);
        }
        Ok(deduped)
    }

    pub fn save(projects: &[ProjectInfo]) -> Result<(), String> {
        let path = Self::path();
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        let file = HubFile {
            version: 1,
            projects: projects.to_vec(),
        };
        let text = serde_json::to_string_pretty(&file).map_err(|e| e.to_string())?;
        fs::write(path, text).map_err(|e| e.to_string())
    }
}

pub fn default_projects_root() -> String {
    #[cfg(windows)]
    {
        if let Ok(profile) = std::env::var("USERPROFILE") {
            return PathBuf::from(profile)
                .join("MipsyncProjects")
                .to_string_lossy()
                .into_owned();
        }
    }
    #[cfg(not(windows))]
    {
        if let Some(home) = dirs::home_dir() {
            return home.join("MipsyncProjects").to_string_lossy().into_owned();
        }
    }
    std::env::current_dir()
        .unwrap_or_else(|_| PathBuf::from("."))
        .join("MipsyncProjects")
        .to_string_lossy()
        .into_owned()
}

fn dedupe_projects(projects: Vec<ProjectInfo>) -> Vec<ProjectInfo> {
    let mut result: Vec<ProjectInfo> = Vec::new();
    for mut project in projects {
        project.path = paths::normalize_path(&project.path);
        if let Some(existing) = result
            .iter_mut()
            .find(|p| paths::paths_equal(&p.path, &project.path))
        {
            if project.last_opened >= existing.last_opened {
                *existing = project;
            }
        } else {
            result.push(project);
        }
    }
    result.sort_by(|a, b| b.last_opened.cmp(&a.last_opened));
    result
}

pub fn add_or_update(list: &mut Vec<ProjectInfo>, info: &ProjectInfo) {
    let mut updated = info.clone();
    updated.path = paths::normalize_path(&updated.path);

    if let Some(existing) = list
        .iter_mut()
        .find(|p| paths::paths_equal(&p.path, &updated.path))
    {
        *existing = updated;
    } else {
        list.push(updated);
    }
    list.sort_by(|a, b| b.last_opened.cmp(&a.last_opened));
}

pub fn remove(list: &mut Vec<ProjectInfo>, path: &str) {
    list.retain(|p| !paths::paths_equal(&p.path, path));
}
