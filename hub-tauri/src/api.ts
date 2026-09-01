import { invoke } from "@tauri-apps/api/core";
import { open } from "@tauri-apps/plugin-dialog";

export type ProjectEntry = {
  name: string;
  path: string;
  engineVersion: string;
  defaultScene: string;
  lastOpened: number;
  valid: boolean;
};

export type HubDefaults = {
  projectsRoot: string;
};

export async function listProjects(): Promise<ProjectEntry[]> {
  return invoke<ProjectEntry[]>("list_projects");
}

export async function getDefaults(): Promise<HubDefaults> {
  return invoke<HubDefaults>("get_defaults");
}

export async function createProject(
  name: string,
  parentDir: string,
  engineVersion: string
): Promise<ProjectEntry> {
  return invoke<ProjectEntry>("create_project", { name, parentDir, engineVersion });
}

export async function addExistingProject(path: string): Promise<ProjectEntry> {
  return invoke<ProjectEntry>("add_existing_project", { path });
}

export async function removeProject(path: string): Promise<void> {
  return invoke("remove_project", { path });
}

export async function openProject(path: string): Promise<void> {
  return invoke("open_project", { path });
}

export async function setProjectEngineVersion(
  path: string,
  engineVersion: string
): Promise<ProjectEntry> {
  return invoke<ProjectEntry>("set_project_engine_version", { path, engineVersion });
}

export async function pickFolder(title: string): Promise<string | null> {
  const selected = await open({ directory: true, multiple: false, title });
  if (selected === null) return null;
  return typeof selected === "string" ? selected : selected[0] ?? null;
}

export async function pickFile(
  title: string,
  filters?: { name: string; extensions: string[] }[]
): Promise<string | null> {
  const selected = await open({ directory: false, multiple: false, title, filters });
  if (selected === null) return null;
  return typeof selected === "string" ? selected : selected[0] ?? null;
}

export type EditorRelease = {
  version: string;
  title: string;
  publishedAt: string;
  isPrerelease: boolean;
  isDraft: boolean;
  assetName: string | null;
  downloadUrl: string | null;
  size: number | null;
};

export type InstalledEditor = {
  version: string;
  rootDir: string;
  engineExe: string | null;
};

export type InstallsState = {
  hubVersion: string;
  installsRoot: string;
  activeVersion: string | null;
  installed: InstalledEditor[];
  psn00bsdkDir: string | null;
  openbiosPath: string | null;
};

export type BiosState = {
  openbiosPath: string | null;
  openbiosValid: boolean;
};

export async function getBiosState(): Promise<BiosState> {
  return invoke<BiosState>("get_bios_state");
}

export async function setOpenbiosPath(path: string | null): Promise<BiosState> {
  return invoke<BiosState>("set_openbios_path", { path });
}

export async function listEditorReleases(): Promise<EditorRelease[]> {
  return invoke<EditorRelease[]>("list_editor_releases");
}

export async function getInstallsState(): Promise<InstallsState> {
  return invoke<InstallsState>("get_installs_state");
}

export async function installEditorRelease(version: string): Promise<InstallsState> {
  return invoke<InstallsState>("install_editor_release", { version });
}

export async function uninstallEditorRelease(version: string): Promise<InstallsState> {
  return invoke<InstallsState>("uninstall_editor_release", { version });
}

export async function setActiveEditor(version: string | null): Promise<InstallsState> {
  return invoke<InstallsState>("set_active_editor", { version });
}

export type Ps1ToolchainState = {
  installed: boolean;
  version: string | null;
  rootDir: string | null;
  message: string | null;
};

export async function getPs1ToolchainState(): Promise<Ps1ToolchainState> {
  return invoke<Ps1ToolchainState>("get_ps1_toolchain_state");
}

export async function installPs1Toolchain(): Promise<Ps1ToolchainState> {
  return invoke<Ps1ToolchainState>("install_ps1_toolchain");
}

export async function launchEditor(): Promise<void> {
  return invoke("launch_editor");
}

export type HubUpdateInfo = {
  currentVersion: string;
  latestVersion: string;
  assetName: string | null;
  downloadUrl: string | null;
  isUpdateAvailable: boolean;
};

export async function checkHubUpdate(): Promise<HubUpdateInfo> {
  return invoke<HubUpdateInfo>("check_hub_update");
}

export async function updateHub(): Promise<void> {
  return invoke("update_hub");
}

export async function openKoFi(): Promise<void> {
  return invoke("open_ko_fi");
}
