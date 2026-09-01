use super::paths;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

const PROJECT_FILE: &str = "nostalty.project";

const DEFAULT_SCENE: &str = r#"{
  "version": 1,
  "entities": [
    {
      "id": 1,
      "name": "PS1 Cube",
      "transform": { "position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0] },
      "meshRenderer": { "primitive": "Cube", "size": 2.0, "color": [1.0, 1.0, 1.0, 1.0] },
      "mipsScript": { "path": "Rotator.mips" }
    },
    {
      "id": 2,
      "name": "Floor",
      "transform": { "position": [0.0, -1.5, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0] },
      "meshRenderer": { "primitive": "Plane", "size": 20.0, "color": [1.0, 1.0, 1.0, 1.0] },
      "collider": { "shape": 0, "center": [0.0, 0.0, 0.0], "halfExtents": [10.0, 0.05, 10.0], "radius": 0.5, "capsuleHeight": 1.0, "isTrigger": false },
      "rigidbody": { "bodyType": 0, "mass": 1.0, "useGravity": true, "linearDrag": 0.05, "bounciness": 0.2, "freezeRotation": false }
    },
    {
      "id": 3,
      "name": "Main Camera",
      "transform": { "position": [0.0, 2.0, 6.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0] },
      "camera": { "primary": true, "fov": 60.0, "nearClip": 0.1, "farClip": 100.0 }
    }
  ]
}
"#;

const ROTATOR_SCRIPT: &str = r#"class Rotator : MipsBehaviour
{
    public float speed = 90.0;

    void Awake()
    {
        Log.Info("Rotator awake");
    }

    void Start()
    {
        Log.Info("Rotator ready");
    }

    void Update()
    {
        transform.rotation.y = transform.rotation.y + speed * Time.deltaTime;
    }

    void OnDestroy()
    {
        Log.Info("Rotator destroyed");
    }
}
"#;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProjectInfo {
    pub name: String,
    pub path: String,
    #[serde(default = "default_engine_version")]
    pub engine_version: String,
    #[serde(default = "default_scene_name")]
    pub default_scene: String,
    #[serde(default)]
    pub last_opened: i64,
}

#[derive(Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
struct ProjectFile {
    name: String,
    #[serde(default = "default_engine_version")]
    engine_version: String,
    #[serde(default = "default_scene_name")]
    default_scene: String,
    #[serde(default)]
    last_opened: i64,
}

fn default_engine_version() -> String {
    "0.1.0".into()
}

fn default_scene_name() -> String {
    "scenes/default.nscene".into()
}

fn now_unix() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

pub fn is_valid_dir(dir: &str) -> bool {
    Path::new(dir).join(PROJECT_FILE).is_file()
}

pub fn load_from_dir(project_dir: &str) -> Result<ProjectInfo, String> {
    let root = PathBuf::from(project_dir);
    let file_path = root.join(PROJECT_FILE);
    if !file_path.is_file() {
        return Err(format!(
            "missing nostalty.project in: {}",
            root.display()
        ));
    }
    let text = fs::read_to_string(&file_path).map_err(|e| e.to_string())?;
    let j: ProjectFile = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    let abs = paths::normalize_path(&root.to_string_lossy());
    Ok(ProjectInfo {
        name: j.name,
        path: abs,
        engine_version: j.engine_version,
        default_scene: j.default_scene,
        last_opened: j.last_opened,
    })
}

pub fn save_to_dir(info: &ProjectInfo) -> Result<(), String> {
    let file_path = PathBuf::from(&info.path).join(PROJECT_FILE);
    let mut root = if file_path.is_file() {
        fs::read_to_string(&file_path)
            .ok()
            .and_then(|text| serde_json::from_str::<serde_json::Value>(&text).ok())
            .and_then(|value| value.as_object().cloned())
            .unwrap_or_default()
    } else {
        serde_json::Map::new()
    };

    root.insert("name".into(), serde_json::Value::String(info.name.clone()));
    root.insert(
        "engineVersion".into(),
        serde_json::Value::String(info.engine_version.clone()),
    );
    root.insert(
        "defaultScene".into(),
        serde_json::Value::String(info.default_scene.clone()),
    );
    root.insert("lastOpened".into(), serde_json::Value::Number(info.last_opened.into()));

    root.entry("editorLastScene")
        .or_insert_with(|| serde_json::Value::String(info.default_scene.clone()));
    root.entry("playerSettings").or_insert_with(|| {
        serde_json::json!({
            "productName": info.name.clone(),
            "companyName": "",
            "startupSceneIndex": 0,
            "scenesInBuild": [info.default_scene.clone()]
        })
    });

    let text = serde_json::to_string_pretty(&serde_json::Value::Object(root))
        .map_err(|e| e.to_string())?;
    fs::write(file_path, text).map_err(|e| e.to_string())
}

pub fn create(parent_dir: &str, name: &str, engine_version: &str) -> Result<ProjectInfo, String> {
    if name.trim().is_empty() {
        return Err("project name is empty".into());
    }
    let project_path = PathBuf::from(parent_dir).join(name);
    if project_path.exists() {
        let mut non_empty = false;
        if let Ok(mut entries) = fs::read_dir(&project_path) {
            non_empty = entries.next().is_some();
        }
        if non_empty {
            return Err(format!(
                "directory already exists and is not empty: {}",
                project_path.display()
            ));
        }
    }
    fs::create_dir_all(&project_path).map_err(|e| e.to_string())?;

    let abs = paths::normalize_path(&project_path.to_string_lossy());
    let info = ProjectInfo {
        name: name.to_string(),
        path: abs,
        engine_version: engine_version.trim().to_string(),
        default_scene: "scenes/default.nscene".into(),
        last_opened: now_unix(),
    };

    save_to_dir(&info)?;
    write_text(
        &PathBuf::from(&info.path).join("scenes/default.nscene"),
        DEFAULT_SCENE,
    )?;
    write_text(
        &PathBuf::from(&info.path).join("scripts/Rotator.mips"),
        ROTATOR_SCRIPT,
    )?;

    for sub in [
        "assets",
        "assets/textures",
        "assets/materials",
        "assets/prefabs",
        "assets/models",
    ] {
        fs::create_dir_all(PathBuf::from(&info.path).join(sub)).map_err(|e| e.to_string())?;
    }

    Ok(info)
}

fn write_text(path: &Path, contents: &str) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    fs::write(path, contents).map_err(|e| e.to_string())
}
