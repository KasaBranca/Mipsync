mod installer;
mod manifest;
mod paths;
mod project;
mod ps1_toolchain;
mod registry;
mod settings;

use project::ProjectInfo;
use registry::Registry;
use serde::Serialize;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};
use tauri::Manager;

#[cfg(windows)]
fn shell_open_url(url: &str) -> Result<(), String> {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Foundation::HINSTANCE;
    use windows_sys::Win32::UI::Shell::ShellExecuteW;

    fn w(value: &std::ffi::OsStr) -> Vec<u16> {
        let mut encoded: Vec<u16> = value.encode_wide().collect();
        encoded.push(0);
        encoded
    }

    let verb = w(std::ffi::OsStr::new("open"));
    let target = w(std::ffi::OsStr::new(url));
    let result: HINSTANCE = unsafe {
        ShellExecuteW(
            std::ptr::null_mut(),
            verb.as_ptr(),
            target.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            1,
        )
    };
    let code = result as isize;
    if code <= 32 {
        return Err(format!(
            "Could not open the default browser (ShellExecuteW: {code})"
        ));
    }
    Ok(())
}

#[tauri::command]
fn open_ko_fi() -> Result<(), String> {
    const KO_FI_URL: &str = "https://ko-fi.com/mipsync";
    #[cfg(windows)]
    {
        shell_open_url(KO_FI_URL)
    }
    #[cfg(target_os = "macos")]
    {
        Command::new("open")
            .arg(KO_FI_URL)
            .spawn()
            .map(|_| ())
            .map_err(|e| format!("Could not open Ko-fi: {e}"))
    }
    #[cfg(all(unix, not(target_os = "macos")))]
    {
        Command::new("xdg-open")
            .arg(KO_FI_URL)
            .spawn()
            .map(|_| ())
            .map_err(|e| format!("Could not open Ko-fi: {e}"))
    }
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct ProjectEntry {
    name: String,
    path: String,
    engine_version: String,
    default_scene: String,
    last_opened: i64,
    valid: bool,
}

#[derive(Serialize)]
struct HubDefaults {
    projects_root: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct EditorRelease {
    version: String,
    title: String,
    published_at: String,
    is_prerelease: bool,
    is_draft: bool,
    asset_name: Option<String>,
    download_url: Option<String>,
    size: Option<u64>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct InstallsState {
    hub_version: String,
    installs_root: String,
    active_version: Option<String>,
    installed: Vec<installer::InstalledEditor>,
    psn00bsdk_dir: Option<String>,
    openbios_path: Option<String>,
}

fn parse_semver_triplet(version: &str) -> Option<(u32, u32, u32)> {
    let stripped = version.strip_prefix('v').unwrap_or(version);
    let mut parts = stripped.split('.');
    let major = parts.next()?.parse().ok()?;
    let minor = parts.next()?.parse().ok()?;
    let patch = parts.next()?.parse().ok()?;
    Some((major, minor, patch))
}

/// Mipsync engine releases switched naming from semver `0.X.Y` to
/// **date-stamped** `YYYY.M.D` (e.g. `v2026.5.28`) on 2026-05-28. We define
/// "date-style" as a triplet whose major component is a plausible year
/// (>= 2000) — this works because Cargo's strict semver does not accept
/// leading zeros, so dates are emitted as `2026.5.28` rather than
/// `2026.05.28`. This lets the Hub treat the legacy 0.1.x range as
/// pre-release scaffolding and hide it from the installable list.
fn is_date_version(version: &str) -> bool {
    matches!(parse_semver_triplet(version), Some((y, _, _)) if y >= 2000)
}

fn semver_is_less(current: &str, latest: &str) -> bool {
    match (parse_semver_triplet(current), parse_semver_triplet(latest)) {
        (Some(a), Some(b)) => a < b,
        _ => {
            current != latest && current != format!("v{latest}") && format!("v{current}") != latest
        }
    }
}

fn reconcile_active_editor(settings: &mut settings::HubSettings) -> Result<(), String> {
    let installed = installer::list_installed(settings)?;
    if installed.is_empty() {
        return Ok(());
    }

    let active_ok = settings.active_version.as_ref().is_some_and(|active| {
        installed
            .iter()
            .any(|e| installer::versions_equal(&e.version, active))
    });

    if active_ok {
        if let Some(active) = settings.active_version.clone() {
            settings.active_version = installer::canonical_editor_version(&active);
            settings.save()?;
        }
        return Ok(());
    }

    let mut best = &installed[0];
    for entry in &installed[1..] {
        if semver_is_less(&best.version, &entry.version) {
            best = entry;
        }
    }
    settings.active_version = Some(best.version.clone());
    settings.save()
}

fn load_installs_state() -> Result<InstallsState, String> {
    let mut settings = settings::HubSettings::load();
    settings::migrate_legacy_storage(&mut settings)?;
    reconcile_active_editor(&mut settings)?;
    let installed = installer::list_installed(&settings)?;
    Ok(InstallsState {
        hub_version: format!("v{}", env!("CARGO_PKG_VERSION")),
        installs_root: settings.installs_root_dir().to_string_lossy().into_owned(),
        active_version: settings.active_version,
        installed,
        psn00bsdk_dir: settings.psn00bsdk_dir.clone(),
        openbios_path: settings.openbios_path.clone(),
    })
}

fn now_unix() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

fn to_entry(p: &ProjectInfo) -> ProjectEntry {
    ProjectEntry {
        name: p.name.clone(),
        path: p.path.clone(),
        engine_version: p.engine_version.clone(),
        default_scene: p.default_scene.clone(),
        last_opened: p.last_opened,
        valid: project::is_valid_dir(&p.path),
    }
}

fn find_engine_exe(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    if let Ok(path) = std::env::var("MIPSYNC_ENGINE") {
        let p = PathBuf::from(&path);
        if p.is_file() {
            return Ok(p);
        }
    }

    #[cfg(debug_assertions)]
    {
        if let Ok(exe) = std::env::current_exe() {
            let dir = exe.parent().unwrap_or(Path::new("."));
            for name in ["MipsyncEngine.exe", "MipsyncEngine"] {
                let candidate = dir.join(name);
                if candidate.is_file() {
                    return Ok(candidate);
                }
            }
        }

        if let Ok(dir) = app.path().resource_dir() {
            for name in ["MipsyncEngine.exe", "MipsyncEngine"] {
                let candidate = dir.join(name);
                if candidate.is_file() {
                    return Ok(candidate);
                }
            }
        }
    }

    #[cfg(debug_assertions)]
    {
        let dev_candidates = [
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/src/MipsyncEngine.exe"),
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/src/MipsyncEngine"),
        ];
        for candidate in dev_candidates {
            if candidate.is_file() {
                return Ok(candidate);
            }
        }
    }

    Err("MipsyncEngine executable not found. Build the engine or set MIPSYNC_ENGINE.".into())
}

fn find_active_engine_exe(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    let settings = settings::HubSettings::load();
    if let Some(active) = settings.active_version.as_ref() {
        if let Some(p) = installer::list_installed(&settings)?
            .into_iter()
            .find(|e| installer::versions_equal(&e.version, active))
            .and_then(|e| e.engine_exe)
        {
            let exe = PathBuf::from(&p);
            if exe.is_file() {
                return Ok(exe);
            }
        }
    }
    find_engine_exe(app)
}

fn find_engine_for_project(app: &tauri::AppHandle, info: &ProjectInfo) -> Result<PathBuf, String> {
    let settings = settings::HubSettings::load();
    let installed = installer::list_installed(&settings)?;
    if let Some(p) = installed
        .iter()
        .find(|e| installer::versions_equal(&e.version, &info.engine_version))
        .and_then(|e| e.engine_exe.as_ref())
    {
        let exe = PathBuf::from(p);
        if exe.is_file() {
            return Ok(exe);
        }
    }

    // Fallback: active editor (if any), then dev/sibling engine.
    find_active_engine_exe(app)
}

#[tauri::command]
fn list_projects() -> Result<Vec<ProjectEntry>, String> {
    let list = Registry::load()?;
    Ok(list.iter().map(to_entry).collect())
}

#[tauri::command]
fn get_defaults() -> HubDefaults {
    HubDefaults {
        projects_root: registry::default_projects_root(),
    }
}

#[tauri::command]
fn create_project(
    name: String,
    parent_dir: String,
    engine_version: String,
) -> Result<ProjectEntry, String> {
    let mut info = project::create(&parent_dir, &name, &engine_version)?;
    info.last_opened = now_unix();
    let mut list = Registry::load()?;
    registry::add_or_update(&mut list, &info);
    Registry::save(&list)?;
    Ok(to_entry(&info))
}

#[tauri::command]
fn add_existing_project(path: String) -> Result<ProjectEntry, String> {
    let info = project::load_from_dir(&path)?;
    let mut list = Registry::load()?;
    registry::add_or_update(&mut list, &info);
    Registry::save(&list)?;
    Ok(to_entry(&info))
}

#[tauri::command]
fn remove_project(path: String) -> Result<(), String> {
    let mut list = Registry::load()?;
    registry::remove(&mut list, &path);
    Registry::save(&list)
}

#[tauri::command]
fn set_project_engine_version(
    path: String,
    engine_version: String,
) -> Result<ProjectEntry, String> {
    let mut info = project::load_from_dir(&path)?;
    info.engine_version = installer::canonical_editor_version(&engine_version)
        .ok_or_else(|| "invalid editor version".to_string())?;
    project::save_to_dir(&info)?;

    let mut list = Registry::load()?;
    registry::add_or_update(&mut list, &info);
    Registry::save(&list)?;

    Ok(to_entry(&info))
}

#[tauri::command]
fn open_project(app: tauri::AppHandle, path: String) -> Result<(), String> {
    let mut info = project::load_from_dir(&path)?;
    info.last_opened = now_unix();
    project::save_to_dir(&info)?;

    let mut list = Registry::load()?;
    registry::add_or_update(&mut list, &info);
    Registry::save(&list)?;

    let engine = find_engine_for_project(&app, &info)?;
    let mut cmd = Command::new(&engine);
    // If Hub-managed PSn00bSDK is installed, pass it to the editor build pipeline.
    let hub_settings = settings::HubSettings::load();
    if let Some(sdk) = hub_settings.psn00bsdk_dir {
        cmd.env("PSN00BSDK", sdk);
    }
    if let Some(bios) = hub_settings.openbios_path {
        cmd.env("MIPSYNC_OPENBIOS_PATH", bios);
    }
    let status = cmd
        .arg("--project")
        .arg(&info.path)
        .spawn()
        .map_err(|e| format!("Failed to start engine: {e}"))?;

    drop(status);
    Ok(())
}

#[tauri::command]
fn list_editor_releases() -> Result<Vec<EditorRelease>, String> {
    let m = manifest::fetch_latest()?;
    let mut out = Vec::new();

    for r in m.editor.releases.into_vec() {
        // Hide tags like v0.1.1-hubtest from editor list.
        if r.version.contains('-') {
            continue;
        }
        // Naming convention switched to YYYY.M.D on 2026-05-28; everything
        // older (0.1.x scaffolding) is intentionally excluded from the
        // installer UI to give users a clean cut-over.
        if !is_date_version(&r.version) {
            continue;
        }
        out.push(EditorRelease {
            version: r.version.clone(),
            title: r.title.clone(),
            published_at: r.published_at.unwrap_or_default(),
            is_prerelease: false,
            is_draft: false,
            asset_name: Some(m.editor.asset_name.clone()),
            download_url: Some(r.download_url.clone()),
            size: None,
        });
    }
    Ok(out)
}

#[tauri::command]
fn get_installs_state() -> Result<InstallsState, String> {
    load_installs_state()
}

#[tauri::command]
fn set_active_editor(version: Option<String>) -> Result<InstallsState, String> {
    let mut s = settings::HubSettings::load();
    s.active_version = version
        .as_deref()
        .and_then(installer::canonical_editor_version);
    s.save()?;
    load_installs_state()
}

#[tauri::command]
fn install_editor_release(version: String) -> Result<InstallsState, String> {
    let settings = settings::HubSettings::load();
    let m = manifest::fetch_latest()?;
    let releases = m.editor.releases.into_vec();
    let entry = releases
        .iter()
        .find(|r| installer::versions_equal(&r.version, &version))
        .ok_or_else(|| "release not found in manifest".to_string())?;
    let canonical = installer::canonical_editor_version(&entry.version)
        .ok_or_else(|| "invalid editor version in manifest".to_string())?;
    installer::install_from_url(&settings, &canonical, &entry.download_url)?;

    let mut s = settings::HubSettings::load();
    s.active_version = Some(canonical);
    s.save()?;

    load_installs_state()
}

#[tauri::command]
fn uninstall_editor_release(version: String) -> Result<InstallsState, String> {
    let mut settings = settings::HubSettings::load();
    let installs_root = settings.installs_root_dir();
    if installs_root.is_dir() {
        for entry in std::fs::read_dir(&installs_root).map_err(|e| e.to_string())? {
            let entry = entry.map_err(|e| e.to_string())?;
            let folder_name = entry.file_name().to_string_lossy().into_owned();
            if entry.file_type().map_err(|e| e.to_string())?.is_dir()
                && installer::versions_equal(&folder_name, &version)
            {
                std::fs::remove_dir_all(entry.path()).map_err(|e| e.to_string())?;
            }
        }
    }

    if settings
        .active_version
        .as_deref()
        .is_some_and(|active| installer::versions_equal(active, &version))
    {
        settings.active_version = None;
    }
    settings.save()?;

    load_installs_state()
}

#[tauri::command]
fn get_ps1_toolchain_state() -> Result<ps1_toolchain::ToolchainState, String> {
    let settings = settings::HubSettings::load();
    Ok(ps1_toolchain::get_state(&settings))
}

#[tauri::command]
fn install_ps1_toolchain() -> Result<ps1_toolchain::ToolchainState, String> {
    let mut settings = settings::HubSettings::load();
    settings::migrate_legacy_storage(&mut settings)?;
    ps1_toolchain::install(&mut settings)
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct BiosState {
    openbios_path: Option<String>,
    openbios_valid: bool,
}

#[tauri::command]
fn get_bios_state() -> Result<BiosState, String> {
    let settings = settings::HubSettings::load();
    let valid = settings
        .openbios_path
        .as_ref()
        .map(|p| Path::new(p).is_file())
        .unwrap_or(false);
    Ok(BiosState {
        openbios_path: settings.openbios_path,
        openbios_valid: valid,
    })
}

#[tauri::command]
fn set_openbios_path(path: Option<String>) -> Result<BiosState, String> {
    let mut s = settings::HubSettings::load();
    let trimmed = path.map(|p| p.trim().to_string()).filter(|p| !p.is_empty());
    s.openbios_path = trimmed;
    s.save()?;
    get_bios_state()
}

#[tauri::command]
fn launch_editor(app: tauri::AppHandle) -> Result<(), String> {
    let engine = find_active_engine_exe(&app)?;
    let mut cmd = Command::new(&engine);
    let hub_settings = settings::HubSettings::load();
    if let Some(sdk) = hub_settings.psn00bsdk_dir {
        cmd.env("PSN00BSDK", sdk);
    }
    if let Some(bios) = hub_settings.openbios_path {
        cmd.env("MIPSYNC_OPENBIOS_PATH", bios);
    }
    cmd.spawn()
        .map_err(|e| format!("Failed to start engine: {e}"))?;
    Ok(())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![
            list_projects,
            get_defaults,
            create_project,
            add_existing_project,
            remove_project,
            set_project_engine_version,
            open_project,
            list_editor_releases,
            get_installs_state,
            install_editor_release,
            uninstall_editor_release,
            set_active_editor,
            launch_editor,
            get_ps1_toolchain_state,
            install_ps1_toolchain,
            get_bios_state,
            set_openbios_path,
            open_ko_fi,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
