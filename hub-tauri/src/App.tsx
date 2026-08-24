import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { getCurrentWindow } from "@tauri-apps/api/window";
import {
  addExistingProject,
  createProject,
  getDefaults,
  getInstallsState,
  installEditorRelease,
  listProjects,
  listEditorReleases,
  getPs1ToolchainState,
  installPs1Toolchain,
  getBiosState,
  setOpenbiosPath,
  pickFile,
  openProject,
  openKoFi,
  pickFolder,
  removeProject,
  setProjectEngineVersion,
  setActiveEditor,
  uninstallEditorRelease,
  type EditorRelease,
  type BiosState,
  type InstallsState,
  type Ps1ToolchainState,
  type ProjectEntry,
} from "./api";

type ModalKind = "new" | "add" | null;
type Tab = "projects" | "installs";

function parseVersion(v: string): [number, number, number] {
  const stripped = v.trim().replace(/^v/i, "");
  const parts = stripped.split(".").map(Number);
  return [parts[0] || 0, parts[1] || 0, parts[2] || 0];
}

function compareVersions(a: string, b: string): number {
  const av = parseVersion(a);
  const bv = parseVersion(b);
  for (let i = 0; i < 3; i += 1) {
    if (av[i] !== bv[i]) return av[i] - bv[i];
  }
  return a.localeCompare(b);
}

function versionsEqual(a: string | null | undefined, b: string | null | undefined): boolean {
  if (!a || !b) return false;
  const at = a.trim();
  const bt = b.trim();
  return at === bt || at.replace(/^v/i, "") === bt.replace(/^v/i, "");
}

function canonicalEditorVersion(version: string | null | undefined): string | null {
  const match = version?.trim().match(/^v?(\d+)\.(\d+)\.(\d+)$/i);
  return match ? `v${match[1]}.${match[2]}.${match[3]}` : null;
}

function latestEditorReleaseFrom(releases: EditorRelease[]): EditorRelease | null {
  const candidates = releases.filter((r) => !r.isDraft && r.downloadUrl);
  if (!candidates.length) return null;
  return candidates.reduce((latest, current) =>
    compareVersions(current.version, latest.version) > 0 ? current : latest
  );
}

function formatRelative(ts: number): string {
  if (!ts) return "—";
  const now = Date.now() / 1000;
  const diff = now - ts;
  const days = Math.floor(diff / 86400);
  if (days < 1) return "今日";
  if (days < 30) return `${days}日前`;
  if (days < 365) return `${Math.floor(days / 30)}ヶ月前`;
  return `${Math.floor(days / 365)}年前`;
}

function basename(p: string): string {
  const s = p.replace(/\\/g, "/");
  const parts = s.split("/");
  return parts[parts.length - 1] || p;
}

const LAST_NEW_PROJECT_LOCATION_KEY = "mipsync.lastNewProjectLocation";

function readLastNewProjectLocation(): string {
  try {
    return localStorage.getItem(LAST_NEW_PROJECT_LOCATION_KEY)?.trim() ?? "";
  } catch {
    return "";
  }
}

function writeLastNewProjectLocation(location: string) {
  const trimmed = location.trim();
  if (!trimmed) return;
  try {
    localStorage.setItem(LAST_NEW_PROJECT_LOCATION_KEY, trimmed);
  } catch {
    // Ignore storage failures; the Hub can still create projects normally.
  }
}

function preferredNewProjectLocation(defaultLocation: string): string {
  return readLastNewProjectLocation() || defaultLocation;
}

type SmoothSelectOption = {
  value: string;
  label: string;
};

function SmoothSelect({
  value,
  options,
  onChange,
  disabled = false,
  id,
  ariaLabel,
  className = "",
}: {
  value: string;
  options: SmoothSelectOption[];
  onChange: (value: string) => void;
  disabled?: boolean;
  id?: string;
  ariaLabel?: string;
  className?: string;
}) {
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement>(null);
  const selected = options.find((option) => option.value === value) ?? options[0];

  useEffect(() => {
    if (!open) return;
    const closeOutside = (event: PointerEvent) => {
      if (!rootRef.current?.contains(event.target as Node)) setOpen(false);
    };
    window.addEventListener("pointerdown", closeOutside);
    return () => window.removeEventListener("pointerdown", closeOutside);
  }, [open]);

  useEffect(() => {
    if (disabled) setOpen(false);
  }, [disabled]);

  const moveSelection = (direction: 1 | -1) => {
    if (!options.length) return;
    const current = Math.max(0, options.findIndex((option) => option.value === value));
    const next = (current + direction + options.length) % options.length;
    onChange(options[next].value);
  };

  return (
    <div
      ref={rootRef}
      className={`smooth-select ${open ? "open" : ""} ${className}`}
      onClick={(event) => event.stopPropagation()}
      onDoubleClick={(event) => event.stopPropagation()}
    >
      <button
        id={id}
        type="button"
        className="smooth-select-trigger"
        disabled={disabled}
        aria-label={ariaLabel}
        aria-haspopup="listbox"
        aria-expanded={open}
        onClick={() => setOpen((current) => !current)}
        onKeyDown={(event) => {
          if (event.key === "Escape") setOpen(false);
          if (event.key === "ArrowDown") {
            event.preventDefault();
            setOpen(true);
            moveSelection(1);
          }
          if (event.key === "ArrowUp") {
            event.preventDefault();
            setOpen(true);
            moveSelection(-1);
          }
        }}
      >
        <span>{selected?.label ?? "—"}</span>
        <span className="smooth-select-chevron" aria-hidden="true">
          <svg viewBox="0 0 16 16" focusable="false">
            <path d="M4 6.25 8 10l4-3.75" />
          </svg>
        </span>
      </button>
      <div className="smooth-select-menu" role="listbox" aria-hidden={!open}>
        {options.map((option) => (
          <button
            key={option.value}
            type="button"
            role="option"
            aria-selected={option.value === value}
            className={`smooth-select-option ${option.value === value ? "selected" : ""}`}
            tabIndex={open ? 0 : -1}
            onClick={() => {
              onChange(option.value);
              setOpen(false);
            }}
          >
            <span>{option.label}</span>
            {option.value === value && <span className="smooth-select-check" aria-hidden="true">●</span>}
          </button>
        ))}
      </div>
    </div>
  );
}

export default function App() {
  const [projects, setProjects] = useState<ProjectEntry[]>([]);
  const [search, setSearch] = useState("");
  const [modal, setModal] = useState<ModalKind>(null);
  const [tab, setTab] = useState<Tab>("projects");
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(true);

  const [newName, setNewName] = useState("MyGame");
  const [newLocation, setNewLocation] = useState("");
  const [defaultProjectsRoot, setDefaultProjectsRoot] = useState("");
  const [newEngineVersion, setNewEngineVersion] = useState<string>("");
  const [addPath, setAddPath] = useState("");

  const [releases, setReleases] = useState<EditorRelease[]>([]);
  const [installs, setInstalls] = useState<InstallsState | null>(null);
  const [installing, setInstalling] = useState<string | null>(null);
  const [hasEditorUpdate, setHasEditorUpdate] = useState(false);
  const [showUpdateToast, setShowUpdateToast] = useState(true);
  const [ps1Toolchain, setPs1Toolchain] = useState<Ps1ToolchainState | null>(null);
  const [installingPs1Toolchain, setInstallingPs1Toolchain] = useState(false);
  const [bios, setBios] = useState<BiosState | null>(null);
  const [pendingUpdateProject, setPendingUpdateProject] = useState<ProjectEntry | null>(null);
  const [openingProject, setOpeningProject] = useState(false);

  const latestEditorRelease = useMemo(
    () => latestEditorReleaseFrom(releases),
    [releases]
  );

  const availableProjectVersions = useMemo(() => {
    const set = new Set<string>();
    const add = (v: string | null | undefined) => {
      const canonical = canonicalEditorVersion(v);
      if (canonical) set.add(canonical);
    };
    for (const v of installs?.installed ?? []) add(v.version);
    return Array.from(set).sort((a, b) => compareVersions(b, a));
  }, [installs]);

  const refresh = useCallback(async () => {
    setProjects(await listProjects());
  }, []);

  const refreshInstalls = useCallback(async () => {
    const state = await getInstallsState();
    setInstalls(state);
    return state;
  }, []);

  useEffect(() => {
    (async () => {
      try {
        const defaults = await getDefaults();
        setDefaultProjectsRoot(defaults.projectsRoot);
        setNewLocation(preferredNewProjectLocation(defaults.projectsRoot));
        await refresh();
        await refreshInstalls();
        const s = await getInstallsState();
        setInstalls(s);
        setNewEngineVersion(s.activeVersion ?? s.installed[0]?.version ?? "0.1.0");

        // Background update checks for sidebar badges.
        const [r, tc, bs] = await Promise.all([
          listEditorReleases(),
          getPs1ToolchainState(),
          getBiosState(),
        ]);
        setReleases(r);
        setPs1Toolchain(tc);
        setBios(bs);
        const installedSet = new Set((s.installed ?? []).map((x) => x.version));
        const latestRel = latestEditorReleaseFrom(r);
        const missing = !!latestRel && !Array.from(installedSet).some((v) => versionsEqual(v, latestRel.version));
        setHasEditorUpdate(missing);
      } catch (e) {
        setError(String(e));
      } finally {
        setLoading(false);
      }
    })();
  }, [refresh, refreshInstalls]);

  useEffect(() => {
    if (tab !== "installs") return;
    (async () => {
      try {
        setError("");
        setReleases(await listEditorReleases());
        await refreshInstalls();
        setPs1Toolchain(await getPs1ToolchainState());
        setBios(await getBiosState());
      } catch (e) {
        setError(String(e));
      }
    })();
  }, [tab, refreshInstalls]);

  useEffect(() => {
    let disposed = false;
    let unlisten: (() => void) | undefined;
    (async () => {
      const win = getCurrentWindow();
      unlisten = await win.onFocusChanged(({ payload: focused }) => {
        if (focused && !disposed) {
          void refreshInstalls();
          void getPs1ToolchainState().then(setPs1Toolchain);
          void getBiosState().then(setBios);
        }
      });
    })();
    return () => {
      disposed = true;
      unlisten?.();
    };
  }, [refreshInstalls]);

  const updateToast =
    showUpdateToast && hasEditorUpdate ? (
      <div className="toast toast-loud">
        <div className="toast-title">
          Updates available
          <button
            type="button"
            className="toast-close"
            onClick={() => setShowUpdateToast(false)}
            aria-label="Dismiss"
          >
            ×
          </button>
        </div>
        <div className="toast-body">
          {hasEditorUpdate && (
            <div>
              <strong>Editor</strong> has a new release.
            </div>
          )}
        </div>
        <div className="toast-actions">
          <button
            type="button"
            className="btn btn-secondary"
            onClick={() => setTab("installs")}
          >
            Open Installs
          </button>
        </div>
      </div>
    ) : null;

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase();
    if (!q) return projects;
    return projects.filter(
      (p) =>
        p.name.toLowerCase().includes(q) ||
        p.path.toLowerCase().includes(q)
    );
  }, [projects, search]);

  const ensureLatestEditorRelease = async (): Promise<EditorRelease | null> => {
    if (latestEditorRelease) return latestEditorRelease;
    const fetched = await listEditorReleases();
    setReleases(fetched);
    return latestEditorReleaseFrom(fetched);
  };

  const openProjectNow = async (p: ProjectEntry) => {
    await openProject(p.path);
  };

  const handleOpen = async (p: ProjectEntry, bypassUpdatePrompt = false) => {
    if (!p.valid) return;
    setError("");
    try {
      if (!bypassUpdatePrompt) {
        const latest = await ensureLatestEditorRelease();
        if (latest && !versionsEqual(p.engineVersion, latest.version)) {
          setPendingUpdateProject(p);
          return;
        }
      }
      await openProjectNow(p);
    } catch (e) {
      setError(String(e));
    }
  };

  const handleOpenWithLatestEditor = async () => {
    if (!pendingUpdateProject) return;
    setError("");
    setOpeningProject(true);
    try {
      const latest = await ensureLatestEditorRelease();
      if (!latest) {
        await openProjectNow(pendingUpdateProject);
        setPendingUpdateProject(null);
        return;
      }

      const installed = installs?.installed?.some((e) => versionsEqual(e.version, latest.version)) ?? false;
      let installState = installs;
      if (!installed) {
        installState = await installEditorRelease(latest.version);
        setInstalls(installState);
        setHasEditorUpdate(false);
      }

      const updated = await setProjectEngineVersion(pendingUpdateProject.path, latest.version);
      setProjects((prev) => prev.map((p) => (p.path === updated.path ? updated : p)));
      setPendingUpdateProject(null);
      await openProjectNow(updated);
    } catch (e) {
      setError(String(e));
    } finally {
      setOpeningProject(false);
    }
  };

  const handleProjectVersionChange = async (projectPath: string, engineVersion: string) => {
    setError("");
    try {
      const updated = await setProjectEngineVersion(projectPath, engineVersion);
      setProjects((prev) => prev.map((p) => (p.path === projectPath ? updated : p)));
    } catch (e) {
      setError(String(e));
    }
  };

  const handleCreate = async () => {
    setError("");
    try {
      const created = await createProject(
        newName.trim(),
        newLocation,
        newEngineVersion || "0.1.0"
      );
      writeLastNewProjectLocation(newLocation);
      setModal(null);
      await refresh();
      await openProject(created.path);
    } catch (e) {
      setError(String(e));
    }
  };

  const handleAdd = async () => {
    setError("");
    try {
      await addExistingProject(addPath.trim());
      setModal(null);
      setAddPath("");
      await refresh();
    } catch (e) {
      setError(String(e));
    }
  };

  const handleRemove = async (path: string, e: React.MouseEvent) => {
    e.stopPropagation();
    setError("");
    try {
      await removeProject(path);
      await refresh();
    } catch (err) {
      setError(String(err));
    }
  };

  const isPs1ToolchainInstalled = Boolean(
    ps1Toolchain?.installed || installs?.psn00bsdkDir
  );

  return (
    <div className="app">
      <aside className="sidebar">
        <div className="brand">
          <img src="/app-icon.png" alt="" />
          <div className="brand-title">
            <strong>Mipsync</strong>
            <span>Hub</span>
          </div>
        </div>
        <nav className="nav">
          <span className={`nav-highlight ${tab}`} aria-hidden="true" />
          <button
            type="button"
            className={`nav-item ${tab === "projects" ? "active" : ""}`}
            onClick={() => setTab("projects")}
          >
            Projects
          </button>
          <button
            type="button"
            className={`nav-item ${tab === "installs" ? "active" : ""}`}
            onClick={() => setTab("installs")}
          >
            Installs
            {hasEditorUpdate && <span className="nav-badge" />}
          </button>
          <button type="button" className="nav-item" disabled>
            Learn
          </button>
        </nav>
        <section className="support-card" aria-label="Support Mipsync development">
          <div className="support-card-heading">
            <span className="support-card-heart" aria-hidden="true">♥</span>
            <strong>Support Mipsync</strong>
          </div>
          <p>Help fund engine development and PS1 hardware testing.</p>
          <button
            type="button"
            className="support-card-button"
            onClick={async () => {
              setError("");
              try {
                await openKoFi();
              } catch (e) {
                setError(`Could not open Ko-fi: ${String(e)}`);
              }
            }}
          >
            Open Ko-fi <span aria-hidden="true">↗</span>
          </button>
        </section>
        <div className="sidebar-footer">
          Mipsync Hub
          <br />
          <span className="mono">{installs?.hubVersion ?? "—"}</span>
        </div>
      </aside>

      <main className="main">
        {tab === "projects" ? (
          <>
            <header className="header">
              <h1>Projects</h1>
              <div className="toolbar">
                <div className="search">
                  <span className="search-icon">⌕</span>
                  <input
                    type="search"
                    placeholder="Search projects"
                    value={search}
                    onChange={(e) => setSearch(e.target.value)}
                  />
                </div>
                <div className="toolbar-spacer" />
                <button
                  type="button"
                  className="btn btn-secondary"
                  onClick={() => {
                    setError("");
                    setModal("add");
                  }}
                >
                  Add existing
                </button>
                <button
                  type="button"
                  className="btn btn-primary"
                  onClick={() => {
                    setError("");
                    setNewLocation(preferredNewProjectLocation(defaultProjectsRoot));
                    setModal("new");
                  }}
                >
                  + New project
                </button>
              </div>
            </header>

            <section className="content">
              {updateToast}
              {loading ? (
                <div className="empty">Loading…</div>
              ) : filtered.length === 0 ? (
                <div className="empty">
                  <strong>
                    {projects.length === 0 ? "No projects yet" : "No matches"}
                  </strong>
                  <p>
                    {projects.length === 0
                      ? "Create a project to get started."
                      : "Try a different search term."}
                  </p>
                </div>
              ) : (
                <div className="project-grid">
                  {filtered.map((p) => (
                    <article
                      key={p.path}
                      className={`project-row ${p.valid ? "" : "invalid"}`}
                      onDoubleClick={() => handleOpen(p)}
                      role="button"
                      tabIndex={0}
                      onKeyDown={(e) => {
                        if (e.key === "Enter") handleOpen(p);
                      }}
                    >
                      <div className="project-name">
                        {!p.valid && (
                          <span className="badge-warn" title="Missing folder">
                            !
                          </span>
                        )}
                        {p.name}
                      </div>
                      <div className="project-path">{p.path}</div>
                      <div className="project-engine">
                        <SmoothSelect
                          className="select mono"
                          value={canonicalEditorVersion(p.engineVersion) ?? p.engineVersion}
                          onChange={(value) => handleProjectVersionChange(p.path, value)}
                          ariaLabel="Editor version"
                          options={[
                            ...(!availableProjectVersions.some((v) => versionsEqual(v, p.engineVersion))
                              ? [{ value: p.engineVersion, label: canonicalEditorVersion(p.engineVersion) ?? p.engineVersion }]
                              : []),
                            ...availableProjectVersions.map((version) => ({ value: version, label: version })),
                          ]}
                        />
                      </div>
                      <div className="project-meta">
                        {formatRelative(p.lastOpened)}
                      </div>
                      <div className="project-actions">
                        <button
                          type="button"
                          className="btn btn-primary"
                          disabled={!p.valid}
                          onClick={(e) => {
                            e.stopPropagation();
                            handleOpen(p);
                          }}
                        >
                          Open
                        </button>
                        <button
                          type="button"
                          className="btn btn-ghost"
                          onClick={(e) => handleRemove(p.path, e)}
                        >
                          Remove
                        </button>
                      </div>
                    </article>
                  ))}
                </div>
              )}
              {error && <p className="error">{error}</p>}
            </section>
          </>
        ) : (
          <>
            <header className="header">
              <h1>Installs</h1>
              <div className="toolbar">
                <div className="toolbar-spacer" />
                <button
                  type="button"
                  className="btn btn-secondary"
                  onClick={async () => {
                    setError("");
                    try {
                      setReleases(await listEditorReleases());
                      await refreshInstalls();
                      setPs1Toolchain(await getPs1ToolchainState());
                      setBios(await getBiosState());
                    } catch (e) {
                      setError(String(e));
                    }
                  }}
                >
                  Refresh
                </button>
              </div>
            </header>

            <section className="content">
              {updateToast}

              <h2 className="section-title">PS1 Toolchain</h2>
              <div className="install-grid">
                <article className="install-row">
                  <div className="install-title">
                    <strong>PSn00bSDK</strong>
                    {isPs1ToolchainInstalled ? (
                      <span className="muted">installed</span>
                    ) : (
                      <span className="badge-warn">not installed</span>
                    )}
                  </div>
                  <div className="install-sub">
                    <div className="muted">
                      Root:{" "}
                      <span className="mono">
                        {ps1Toolchain?.rootDir ?? installs?.psn00bsdkDir ?? "—"}
                      </span>
                      {ps1Toolchain?.version ? (
                        <>
                          {" "}
                          • <span className="mono">{ps1Toolchain.version}</span>
                        </>
                      ) : null}
                    </div>
                  </div>
                  <div className="install-actions">
                    <button
                      type="button"
                      className={`btn ${isPs1ToolchainInstalled ? "btn-secondary" : "btn-primary"}`}
                      disabled={isPs1ToolchainInstalled || installingPs1Toolchain}
                      onClick={async () => {
                        setError("");
                        setInstallingPs1Toolchain(true);
                        try {
                          const state = await installPs1Toolchain();
                          setPs1Toolchain(state);
                          await refreshInstalls();
                        } catch (e) {
                          setError(String(e));
                        } finally {
                          setInstallingPs1Toolchain(false);
                        }
                      }}
                    >
                      {isPs1ToolchainInstalled
                        ? "Installed"
                        : installingPs1Toolchain
                          ? "Installing…"
                          : "Install PSn00bSDK"}
                    </button>
                  </div>
                </article>
              </div>

              <h2 className="section-title">PS1 BIOS</h2>
              <div className="install-grid">
                <article className="install-row">
                  <div className="install-title">
                    <strong>OpenBIOS</strong>
                    {bios?.openbiosValid ? (
                      <span className="muted">configured</span>
                    ) : (
                      <span className="badge-warn">no file (PCSX-Redux: built-in)</span>
                    )}
                  </div>
                  <div className="install-sub">
                    <div className="muted">
                      Default BIOS. PCSX-Redux ships OpenBIOS internally — no file
                      required. For DuckStation and other emulators that need an external
                      BIOS file, point Mipsync at an <span className="mono">openbios.bin</span>{" "}
                      extracted from a PCSX-Redux build.
                    </div>
                    <div className="muted" style={{ marginTop: 4 }}>
                      Path:{" "}
                      <span className="mono">
                        {bios?.openbiosPath ?? "— (using emulator's built-in if available)"}
                      </span>
                    </div>
                  </div>
                  <div className="install-actions">
                    <button
                      type="button"
                      className="btn btn-primary"
                      onClick={async () => {
                        setError("");
                        try {
                          const picked = await pickFile("Select openbios.bin", [
                            { name: "PS1 BIOS", extensions: ["bin", "rom"] },
                            { name: "All files", extensions: ["*"] },
                          ]);
                          if (!picked) return;
                          const state = await setOpenbiosPath(picked);
                          setBios(state);
                          await refreshInstalls();
                        } catch (e) {
                          setError(String(e));
                        }
                      }}
                    >
                      {bios?.openbiosValid ? "Replace openbios.bin…" : "Set openbios.bin…"}
                    </button>
                    {bios?.openbiosPath ? (
                      <button
                        type="button"
                        className="btn"
                        onClick={async () => {
                          setError("");
                          try {
                            const state = await setOpenbiosPath(null);
                            setBios(state);
                            await refreshInstalls();
                          } catch (e) {
                            setError(String(e));
                          }
                        }}
                      >
                        Clear
                      </button>
                    ) : null}
                  </div>
                </article>
              </div>
              <div className="hint" style={{ marginTop: 8 }}>
                User-specified retail BIOS (e.g. SCPH-xxxx) can still be set per-editor in
                Build Settings → PS1 → BIOS override. The override takes priority over OpenBIOS.
              </div>

              <div className="install-info">
                <div>
                  <div className="muted">Installs folder</div>
                  <div className="mono">{installs?.installsRoot ?? "—"}</div>
                </div>
                <div>
                  <div className="muted">Active editor</div>
                  <div className="mono">{installs?.activeVersion ?? "—"}</div>
                  <div className="hint" style={{ marginTop: 4 }}>
                    Hub updates do not change this. Install an editor release below.
                  </div>
                </div>
              </div>

              <h2 className="section-title">Releases (GitHub)</h2>
              {releases.length ? (
                <div className="install-grid">
                  {releases.map((r) => {
                    const canInstall = !!r.downloadUrl;
                    const isInstalling = installing === r.version;
                    const already =
                       installs?.installed?.some((i) => i.version === r.version) ??
                      false;
                    return (
                      <article key={r.version} className="install-row">
                        <div className="install-title">
                          <strong>{r.title}</strong>
                          <span className="muted mono">{r.version}</span>
                          {r.isPrerelease && <span className="badge-warn">pre</span>}
                          {r.isDraft && <span className="badge-warn">draft</span>}
                        </div>
                        <div className="install-sub">
                          <div className="muted">
                            {r.assetName
                              ? `asset: ${r.assetName}`
                              : "no windows zip asset"}
                          </div>
                        </div>
                        <div className="install-actions">
                          {already ? (
                            <button
                              type="button"
                              className="btn btn-secondary"
                              onClick={async () => {
                                setError("");
                                try {
                                  setInstalls(await setActiveEditor(r.version));
                                } catch (err) {
                                  setError(String(err));
                                }
                              }}
                            >
                              Set active
                            </button>
                          ) : (
                            <button
                              type="button"
                              className="btn btn-primary"
                              disabled={!canInstall || isInstalling}
                              onClick={async () => {
                                setError("");
                                setInstalling(r.version);
                                try {
                                  setInstalls(await installEditorRelease(r.version));
                                } catch (e) {
                                  setError(String(e));
                                } finally {
                                  setInstalling(null);
                                }
                              }}
                            >
                              {isInstalling ? "Installing…" : "Install"}
                            </button>
                          )}
                        </div>
                      </article>
                    );
                  })}
                </div>
              ) : (
                <div className="empty">
                  <strong>No releases found</strong>
                  <p>
                    Publish a `manifest.json` to the latest Release
                    (latest/download/manifest.json), and include Windows editor zips.
                  </p>
                </div>
              )}

              <h2 className="section-title">Installed</h2>
              {installs?.installed?.length ? (
                <div className="install-grid">
                  {installs.installed.map((e) => (
                    <article key={e.version} className="install-row">
                      <div className="install-title">
                        <strong>{e.version}</strong>
                        {installs.activeVersion === e.version && (
                          <span className="badge-active">ACTIVE</span>
                        )}
                      </div>
                      <div className="install-sub">
                        <div className="mono ellipsis">
                          {e.engineExe ? basename(e.engineExe) : "(missing exe)"}
                        </div>
                        <div className="muted mono ellipsis">{e.rootDir}</div>
                      </div>
                      <div className="install-actions">
                        <button
                          type="button"
                          className="btn btn-secondary"
                          onClick={async () => {
                            setError("");
                            try {
                              setInstalls(await setActiveEditor(e.version));
                            } catch (err) {
                              setError(String(err));
                            }
                          }}
                        >
                          Set active
                        </button>
                        <button
                          type="button"
                          className="btn btn-ghost"
                          onClick={async () => {
                            if (!confirm(`Are you sure you want to uninstall ${e.version}?`)) return;
                            setError("");
                            try {
                              setInstalls(await uninstallEditorRelease(e.version));
                            } catch (err) {
                              setError(String(err));
                            }
                          }}
                        >
                          Uninstall
                        </button>
                      </div>
                    </article>
                  ))}
                </div>
              ) : (
                <div className="empty">
                  <strong>No installs</strong>
                  <p>Install a version from Releases above.</p>
                </div>
              )}
              {error && <p className="error">{error}</p>}
            </section>
          </>
        )}
      </main>

      {modal === "new" && (
        <div
          className="modal-backdrop"
          onClick={() => setModal(null)}
          role="presentation"
        >
          <div
            className="modal"
            onClick={(e) => e.stopPropagation()}
            role="dialog"
          >
            <h2>New project</h2>
            <p>Create a Mipsync Engine project with a default scene.</p>
            <div className="field">
              <label htmlFor="name">Project name</label>
              <input
                id="name"
                value={newName}
                onChange={(e) => setNewName(e.target.value)}
              />
            </div>
            <div className="field">
              <label htmlFor="engine">Editor version</label>
              <SmoothSelect
                id="engine"
                value={newEngineVersion}
                onChange={setNewEngineVersion}
                disabled={!availableProjectVersions.length}
                options={(availableProjectVersions.length ? availableProjectVersions : ["v0.1.0"]).map(
                  (version) => ({
                    value: version,
                    label: `${version}${versionsEqual(installs?.activeVersion, version) ? " (active)" : ""}`,
                  })
                )}
              />
              {!installs?.installed?.length && (
                <div className="hint">
                  No installed editors yet. Install one in Installs tab.
                </div>
              )}
            </div>
            <div className="field">
              <label htmlFor="loc">Location</label>
              <div className="field-row">
                <input
                  id="loc"
                  value={newLocation}
                  onChange={(e) => setNewLocation(e.target.value)}
                />
                <button
                  type="button"
                  className="btn btn-secondary"
                  onClick={async () => {
                    const picked = await pickFolder("Choose parent folder");
                    if (picked) setNewLocation(picked);
                  }}
                >
                  Browse
                </button>
              </div>
            </div>
            {error && <p className="error">{error}</p>}
            <div className="modal-actions">
              <button
                type="button"
                className="btn btn-secondary"
                onClick={() => setModal(null)}
              >
                Cancel
              </button>
              <button
                type="button"
                className="btn btn-primary"
                onClick={handleCreate}
              >
                Create &amp; open
              </button>
            </div>
          </div>
        </div>
      )}

      {modal === "add" && (
        <div
          className="modal-backdrop"
          onClick={() => setModal(null)}
          role="presentation"
        >
          <div
            className="modal"
            onClick={(e) => e.stopPropagation()}
            role="dialog"
          >
            <h2>Add existing project</h2>
            <p>Folder must contain a nostalty.project file.</p>
            <div className="field">
              <label htmlFor="add">Project folder</label>
              <div className="field-row">
                <input
                  id="add"
                  value={addPath}
                  onChange={(e) => setAddPath(e.target.value)}
                />
                <button
                  type="button"
                  className="btn btn-secondary"
                  onClick={async () => {
                    const picked = await pickFolder("Select project folder");
                    if (picked) setAddPath(picked);
                  }}
                >
                  Browse
                </button>
              </div>
            </div>
            {error && <p className="error">{error}</p>}
            <div className="modal-actions">
              <button
                type="button"
                className="btn btn-secondary"
                onClick={() => setModal(null)}
              >
                Cancel
              </button>
              <button
                type="button"
                className="btn btn-primary"
                onClick={handleAdd}
              >
                Add
              </button>
            </div>
          </div>
        </div>
      )}

      {pendingUpdateProject && (
        <div
          className="modal-backdrop"
          onClick={() => {
            if (!openingProject) setPendingUpdateProject(null);
          }}
          role="presentation"
        >
          <div
            className="modal"
            onClick={(e) => e.stopPropagation()}
            role="dialog"
          >
            <h2>Editor update recommended</h2>
            <p>
              This project is set to{" "}
              <span className="mono">{pendingUpdateProject.engineVersion}</span>, but
              the latest editor release is{" "}
              <span className="mono">{latestEditorRelease?.version ?? "checking…"}</span>.
              Update the project before opening?
            </p>
            {latestEditorRelease && !installs?.installed?.some((e) => versionsEqual(e.version, latestEditorRelease.version)) && (
              <div className="hint">
                The latest editor is not installed yet. Choosing update will install it first.
              </div>
            )}
            {error && <p className="error">{error}</p>}
            <div className="modal-actions">
              <button
                type="button"
                className="btn btn-secondary"
                disabled={openingProject}
                onClick={() => setPendingUpdateProject(null)}
              >
                Cancel
              </button>
              <button
                type="button"
                className="btn btn-secondary"
                disabled={openingProject}
                onClick={() => handleOpen(pendingUpdateProject, true)}
              >
                Open anyway
              </button>
              <button
                type="button"
                className="btn btn-primary"
                disabled={openingProject}
                onClick={handleOpenWithLatestEditor}
              >
                {openingProject ? "Updating…" : "Update and open"}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
