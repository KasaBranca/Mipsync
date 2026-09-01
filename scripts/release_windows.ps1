param(
  # Optional override. When omitted we default to today's date in YYYY.M.D
  # form (no leading zeros; required because Cargo's strict semver parser
  # rejects "05" etc.). Use the override only for emergency re-cuts or hub-
  # only test channels.
  [string]$Version,

  # Build and stage the release without publishing it. This lets CI or a
  # maintainer push the matching source commit before creating the GitHub tag.
  [switch]$SkipPublish
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $Version) {
  $now = Get-Date
  $Version = "{0}.{1}.{2}" -f $now.Year, $now.Month, $now.Day
  Write-Host "[release] Defaulting to today's version: $Version"
}

# Date-stamped versions: <year>.<month>.<day>, year >= 2000. The Hub uses
# this same shape to hide pre-2026.05.28 (legacy 0.1.x) releases from the
# installer list.
function Test-IsDateVersion {
  param([string]$Ver)
  $clean = $Ver -replace '^v', ''
  if ($clean -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { return $false }
  $parts = $clean -split '\.'
  return [int]$parts[0] -ge 2000
}

$tag = "v$Version"
$repository = "KasaBranca/Mipsync"

$root = (Resolve-Path "$PSScriptRoot\\..").Path
$outDir = Join-Path $root "release"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$cmakeRoot = Join-Path $root "CMakeLists.txt"
if (Test-Path $cmakeRoot) {
  $t = Get-Content -Raw -Path $cmakeRoot
  $t2 = $t -replace '(?m)^project\(MipsyncEngine\s+VERSION\s+[0-9]+\.[0-9]+\.[0-9]+\s+LANGUAGES\s+CXX\s+C\)\s*$',
                  ("project(MipsyncEngine VERSION $Version LANGUAGES CXX C)")
  if ($t2 -ne $t) {
    Set-Content -Path $cmakeRoot -Value $t2 -Encoding UTF8
    Write-Host "Updated CMake project version to $Version"
  }
}

# Build engine so the embedded version string matches this release.
Write-Host "Building Engine (cmake build)..."
& cmake --build (Join-Path $root "build") --config Release | Out-Host

$engineExe = Join-Path $root "build\\src\\MipsyncEngine.exe"
$cliExe = Join-Path $root "build\\src\\mipsync.exe"
if (!(Test-Path $engineExe)) { throw "Engine exe not found after build: $engineExe" }
if (!(Test-Path $cliExe)) { throw "Mipsync CLI not found after build: $cliExe" }

$hubExe = Join-Path $root "hub-tauri\\src-tauri\\target\\release\\MipsyncHub.exe"
$hubUpdaterExe = Join-Path $root "hub-tauri\\src-tauri\\target\\release\\MipsyncHubUpdater.exe"

Write-Host "Building Hub (tauri build)..."
Push-Location (Join-Path $root "hub-tauri")
try {
  # Keep Hub's embedded version in sync with the release tag.
  function Set-HubPackageVersion {
    param([string]$Path, [string]$Ver)
    $inPackage = $false
    $out = foreach ($line in (Get-Content $Path)) {
      if ($line -match '^\[package\]') { $inPackage = $true; $line }
      elseif ($line -match '^\[') { $inPackage = $false; $line }
      elseif ($inPackage -and $line -match '^\s*version\s*=') { "version = `"$Ver`""; Write-Host "Updated Hub Cargo.toml version to $Ver" }
      else { $line }
    }
    $out | Set-Content -Path $Path -Encoding UTF8
  }
  $cargoToml = Join-Path $root "hub-tauri\\src-tauri\\Cargo.toml"
  if (Test-Path $cargoToml) { Set-HubPackageVersion -Path $cargoToml -Ver $Version }
  $tauriConf = Join-Path $root "hub-tauri\\src-tauri\\tauri.conf.json"
  if (Test-Path $tauriConf) {
    $conf = Get-Content $tauriConf -Raw | ConvertFrom-Json
    $conf.version = $Version
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($tauriConf, ($conf | ConvertTo-Json -Depth 20), $utf8NoBom)
  }
  npm run tauri build | Out-Host
} finally {
  Pop-Location
}

if (!(Test-Path $hubExe)) { throw "Hub exe not found: $hubExe" }
if (!(Test-Path $hubUpdaterExe)) { throw "Hub updater exe not found: $hubUpdaterExe" }

function New-Zip($stagingDir, $zipPath) {
  if (Test-Path $stagingDir) { Remove-Item $stagingDir -Recurse -Force }
  New-Item -ItemType Directory -Force -Path $stagingDir | Out-Null
  if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
  Compress-Archive -Path "$stagingDir\\*" -DestinationPath $zipPath
}

Write-Host "Staging Engine zip..."
$engineStage = Join-Path $outDir "engine-$Version"
$engineZip = Join-Path $outDir "MipsyncEngine_Windows_x64.zip"

if (Test-Path $engineStage) { Remove-Item $engineStage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $engineStage | Out-Null
Copy-Item $engineExe $engineStage -Force
Copy-Item $cliExe $engineStage -Force

New-Item -ItemType Directory -Force -Path (Join-Path $engineStage "resources\\icons") | Out-Null
Copy-Item (Join-Path $root "resources\\icons\\app_icon.png") (Join-Path $engineStage "resources\\icons\\app_icon.png") -Force
Copy-Item (Join-Path $root "resources\\icons\\project") (Join-Path $engineStage "resources\\icons\\project") -Recurse -Force
Copy-Item (Join-Path $root "resources\\fonts") (Join-Path $engineStage "resources\\fonts") -Recurse -Force
if (Test-Path (Join-Path $root "resources\\licenses")) {
  Copy-Item (Join-Path $root "resources\\licenses") (Join-Path $engineStage "resources\\licenses") -Recurse -Force
}

# Editor integrations and language tooling built alongside the engine.
$builtTools = Join-Path $root "build\\src\\tools"
if (Test-Path $builtTools) {
  New-Item -ItemType Directory -Force -Path (Join-Path $engineStage "tools") | Out-Null
  foreach ($toolName in @("vscode-mips", "mips-language-server")) {
    $toolSource = Join-Path $builtTools $toolName
    if (Test-Path $toolSource) {
      Copy-Item $toolSource (Join-Path $engineStage "tools") -Recurse -Force
    }
  }
}

# Agent Skills matching this editor and CLI protocol.
$agentSkills = Join-Path $root "skills"
if (Test-Path $agentSkills) {
  Copy-Item $agentSkills (Join-Path $engineStage "skills") -Recurse -Force
}

# PS1 build templates (PSn00bSDK starter project).
$ps1Templates = Join-Path $root "templates\\ps1"
if (Test-Path $ps1Templates) {
  New-Item -ItemType Directory -Force -Path (Join-Path $engineStage "templates") | Out-Null
  Copy-Item $ps1Templates (Join-Path $engineStage "templates\\ps1") -Recurse -Force
}

# Bundled PS1 runtime: PCSX-Redux emulator + built-in OpenBIOS + helper tools.
$ps1RuntimeStaged = Join-Path $root "third_party\\ps1_runtime"
if (-not (Test-Path (Join-Path $ps1RuntimeStaged "emulator\\pcsx-redux.exe"))) {
  Write-Host "[release] Fetching PCSX-Redux PS1 runtime..."
  powershell -ExecutionPolicy Bypass -File (Join-Path $root "scripts\\fetch_ps1_runtime.ps1")
}
if (Test-Path (Join-Path $ps1RuntimeStaged "emulator\\pcsx-redux.exe")) {
  Copy-Item $ps1RuntimeStaged (Join-Path $engineStage "ps1_runtime") -Recurse -Force
} else {
  Write-Warning "PS1 runtime not available; engine zip will ship without bundled emulator."
}

# MinGW runtime DLLs (if present)
$mingwBin = "C:\\msys64\\mingw64\\bin"
$dlls = @("libgcc_s_seh-1.dll","libwinpthread-1.dll","libstdc++-6.dll")
foreach ($dll in $dlls) {
  $p = Join-Path $mingwBin $dll
  if (Test-Path $p) { Copy-Item $p $engineStage -Force }
}

# Ninja build tool (required by PSn00bSDK CMake preset for Build PS1).
$ninjaSrc = Join-Path $root "third_party\\.cache\\ninja\\ninja.exe"
if (Test-Path $ninjaSrc) {
  New-Item -ItemType Directory -Force -Path (Join-Path $engineStage "tools") | Out-Null
  Copy-Item $ninjaSrc (Join-Path $engineStage "tools\\ninja.exe") -Force
  Write-Host "[release] Bundled tools/ninja.exe for PS1 builds"
} else {
  Write-Warning "third_party/.cache/ninja/ninja.exe not found; PS1 script builds may fail on user machines."
}

if (Test-Path $engineZip) { Remove-Item $engineZip -Force }
Compress-Archive -Path "$engineStage\\*" -DestinationPath $engineZip

Write-Host "Staging Hub zip..."
$hubStage = Join-Path $outDir "hub-$Version"
$hubZip = Join-Path $outDir "MipsyncHub_Windows_x64.zip"
if (Test-Path $hubStage) { Remove-Item $hubStage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $hubStage | Out-Null
Copy-Item $hubExe (Join-Path $hubStage "MipsyncHub.exe") -Force
Copy-Item $hubUpdaterExe (Join-Path $hubStage "MipsyncHubUpdater.exe") -Force
if (Test-Path $hubZip) { Remove-Item $hubZip -Force }
Compress-Archive -Path "$hubStage\\*" -DestinationPath $hubZip

# Generate/merge manifest.json for Hub. Preserve the checked-out/local feed if
# GitHub is temporarily unavailable so a release never erases install history.
$manifestPath = Join-Path $outDir "manifest.json"
$manifestUrl = "https://github.com/$repository/releases/latest/download/manifest.json"
$existing = $null
if (Test-Path $manifestPath) {
  try {
    $existing = Get-Content -Raw -Path $manifestPath | ConvertFrom-Json
  } catch {
    Write-Warning "Local release manifest could not be parsed: $($_.Exception.Message)"
  }
}
try {
  $remoteContent = (Invoke-WebRequest -UseBasicParsing -Uri $manifestUrl -TimeoutSec 10).Content
  if ($remoteContent -is [byte[]]) {
    $remoteContent = [System.Text.Encoding]::UTF8.GetString($remoteContent)
  }
  $remoteManifest = $remoteContent | ConvertFrom-Json
  if ($null -ne $remoteManifest) { $existing = $remoteManifest }
} catch {
  Write-Warning "Published manifest could not be fetched; preserving the local release history. $($_.Exception.Message)"
}

$engineAsset = "MipsyncEngine_Windows_x64.zip"
$hubAsset = "MipsyncHub_Windows_x64.zip"
$downloadBase = "https://github.com/$repository/releases/download/$tag"

$newEntry = [PSCustomObject]@{
  version = $tag
  title = "Mipsync Engine $Version"
  downloadUrl = "$downloadBase/$engineAsset"
  publishedAt = (Get-Date).ToString("o")
}

# Always generate a fresh manifest. If an old manifest exists, carry over editor
# releases — but only ones that follow the new YYYY.M.D naming. Anything older
# (0.1.x scaffolding) is intentionally dropped from the installer feed so users
# only see clean, date-stamped builds going forward.
$prevReleases = @()
try {
  $prev = ($existing | ConvertTo-Json -Depth 6) | ConvertFrom-Json
  if ($null -ne $prev -and $null -ne $prev.editor -and $null -ne $prev.editor.releases) {
    $prevReleases = @(
      @($prev.editor.releases) |
        Where-Object { $_ -ne $null -and (Test-IsDateVersion $_.version) }
    )
  }
} catch {
  $prevReleases = @()
}

$manifest = [PSCustomObject]@{
  schemaVersion = 1
  generatedAt = (Get-Date).ToString("o")
  hub = [PSCustomObject]@{
    version = $tag
    assetName = $hubAsset
    downloadUrl = "$downloadBase/$hubAsset"
    releaseNotesUrl = "https://github.com/$repository/releases/tag/$tag"
  }
  editor = [PSCustomObject]@{
    assetName = $engineAsset
    releases = $prevReleases
  }
}

# Do not add editor entries for hyphen tags (e.g. v0.1.1-hubtest)
if ($tag -notmatch "-") {
  $currentReleases = @($manifest.editor.releases | Where-Object { $_.version -ne $tag })
  $manifest.editor.releases = @(
    $currentReleases + @($newEntry) |
      Sort-Object @{ Expression = { [version]($_.version -replace '^v', '') }; Descending = $true }
  )
}

$manifestJson = $manifest | ConvertTo-Json -Depth 6
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($manifestPath, $manifestJson, $utf8NoBom)

$notes = @"
## Mipsync $Version

Windows x64 release. Mipsync targets real PS1 hardware — the PC executable is
the Editor, while **Build PS1** compiles your startup scene and Mips# scripts
into a real PSX.EXE via PSn00bSDK.

### Highlights (v$Version)
- Agent-ready ``mipsync`` CLI with structured command discovery, Editor IPC,
  safe scene authoring, undo support, and project-scoped Codex Skills.
- Version-matched CLI binding: projects automatically point agents at the CLI
  shipped beside their exact Editor build.
- Major Editor UX pass across multi-selection, the common Inspector, hierarchy
  and Scene View selection, inline asset creation/renaming, and build progress.
- ProModeler cylinder creation, face deletion, optional mesh subdivision
  previews, collider defaults, and pivot fixes.
- PS1 renderer improvements for depth ordering, fog, frustum/distance culling,
  UV tiling, textures, character rendering, and large planar meshes.
- Mips# runtime and animation improvements, including Has Exit Time parity and
  additional APIs used by project scripts.
- Updated PS1 runtime templates, bundled language tools, and ``tools/ninja.exe``.

### Versioning
- Date-stamped releases: ``YYYY.M.D`` (e.g. ``$Version``). Legacy ``v0.1.x``
  editors are hidden from the Hub installer list.

### Assets
- MipsyncEngine_Windows_x64.zip (Editor + CLI + Skills + PS1 runtime/templates)
- MipsyncHub_Windows_x64.zip
"@

$notesFile = Join-Path $outDir "RELEASE_NOTES_$Version.md"
$notes | Out-File -FilePath $notesFile -Encoding utf8

if ($SkipPublish) {
  Write-Host "Release assets are ready. Publishing was skipped."
} else {
  Write-Host "Publishing GitHub Release..."
  gh release create "v$Version" -R $repository `
    --title "Mipsync $Version" `
    --notes-file $notesFile `
    $engineZip $hubZip $manifestPath
}

Write-Host "Done."
