# Mipsync Engine — PS1 runtime fetcher
# ---------------------------------------------------------------------------
# Downloads the latest successful PCSX-Redux Windows x64 build artifact and
# the latest OpenBIOS image from the official grumpycoders Azure DevOps
# pipelines and stages them under `third_party/ps1_runtime/` so the engine
# can bundle a turn-key PS1 toolchain alongside Mipsync. This script is
# idempotent: it skips the download when the staged build matches the
# latest succeeded pipeline build (use `-Force` to override).
#
# Resulting layout:
#   third_party/ps1_runtime/
#     BUILD.txt                 (build numbers / fetched timestamps)
#     LICENSE-PCSX-Redux.txt    (PCSX-Redux project license, GPL-2.0)
#     LICENSE-OpenBIOS.txt      (OpenBIOS project license, MIT)
#     emulator/
#       pcsx-redux.exe + dlls + fonts/ + i18n/ + ...
#     bios/
#       openbios.bin            (PS1 BIOS replacement; loaded by the engine
#                                when launching the bundled emulator)
#     tools/
#       exe2elf.exe exe2iso.exe ps1-packer.exe psyq-obj-parser.exe ...
#
# Notes:
# - PCSX-Redux is GPL-2.0; redistribution is permitted as long as the project
#   LICENSE file ships alongside the binaries.
# - OpenBIOS is MIT-licensed; it is an open-source PS1 BIOS replacement built
#   from `grumpycoders/pcsx-redux` (definition id=2, "pcsx-redux Bios build").
#   PCSX-Redux does NOT statically link OpenBIOS, so the .bin must ship
#   alongside the emulator and be passed via `-bios <path>` (Mipsync does
#   this automatically when it detects the bundled image).

[CmdletBinding()]
param(
    [string]$RepoRoot,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

if (-not $RepoRoot -or -not (Test-Path $RepoRoot)) {
    $scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    $RepoRoot = (Resolve-Path (Join-Path $scriptDir '..')).Path
}

$AzureProject       = "https://dev.azure.com/grumpycoders/pcsx-redux/_apis/build"
$AzureBuildsApi     = "$AzureProject/builds?api-version=7.0&top=1&definitions=1&statusFilter=completed&resultFilter=succeeded"
$AzureBiosBuildsApi = "$AzureProject/builds?api-version=7.0&top=1&definitions=2&statusFilter=completed&resultFilter=succeeded"

$RuntimeDir   = Join-Path $RepoRoot "third_party/ps1_runtime"
$CacheDir     = Join-Path $RepoRoot "third_party/.cache"
$DropZip      = Join-Path $CacheDir "pcsx-redux-drop.zip"
$DropDir      = Join-Path $CacheDir "pcsx-redux-drop"
$BiosZip      = Join-Path $CacheDir "pcsx-redux-openbios.zip"
$BiosDropDir  = Join-Path $CacheDir "pcsx-redux-openbios"
$BuildTxt     = Join-Path $RuntimeDir "BUILD.txt"

function Write-Step($msg) { Write-Host "[ps1-runtime] $msg" -ForegroundColor Cyan }

Write-Step "Querying latest successful Azure DevOps build (PCSX-Redux Windows)..."
$buildsJson = Invoke-WebRequest -UseBasicParsing -Uri $AzureBuildsApi | Select-Object -ExpandProperty Content
$build = ($buildsJson | ConvertFrom-Json).value[0]
if (-not $build) { throw "Could not resolve a successful build from Azure DevOps." }

$buildId     = $build.id
$buildNumber = $build.buildNumber
Write-Step "Latest emulator build: $buildNumber (id $buildId)"

Write-Step "Querying latest successful Azure DevOps build (OpenBIOS)..."
$biosJson = Invoke-WebRequest -UseBasicParsing -Uri $AzureBiosBuildsApi | Select-Object -ExpandProperty Content
$biosBuild = ($biosJson | ConvertFrom-Json).value[0]
if (-not $biosBuild) { throw "Could not resolve a successful OpenBIOS build from Azure DevOps." }
$biosBuildId     = $biosBuild.id
$biosBuildNumber = $biosBuild.buildNumber
Write-Step "Latest OpenBIOS build: $biosBuildNumber (id $biosBuildId)"

# Skip if already in sync (both pipelines must match).
if (-not $Force -and (Test-Path $BuildTxt)) {
    $cached = (Get-Content $BuildTxt -Raw -ErrorAction SilentlyContinue) -as [string]
    if ($cached -match "buildId=$buildId\b" -and $cached -match "biosBuildId=$biosBuildId\b") {
        Write-Step "ps1_runtime is already up-to-date (emu=$buildId, bios=$biosBuildId). Use -Force to refetch."
        return
    }
}

New-Item -ItemType Directory -Path $CacheDir -Force | Out-Null
New-Item -ItemType Directory -Path $RuntimeDir -Force | Out-Null

Write-Step "Fetching artifact metadata..."
$artifactsUrl = "$AzureProject/builds/$buildId/artifacts?api-version=7.0"
$artifacts = (Invoke-WebRequest -UseBasicParsing -Uri $artifactsUrl | Select-Object -ExpandProperty Content | ConvertFrom-Json).value
$drop = $artifacts | Where-Object { $_.name -eq 'drop' } | Select-Object -First 1
if (-not $drop) { throw "No 'drop' artifact in build $buildId." }
$downloadUrl = $drop.resource.downloadUrl
Write-Step "Downloading drop archive (~90 MB)..."
Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -OutFile $DropZip

Write-Step "Extracting..."
if (Test-Path $DropDir) { Remove-Item -Recurse -Force $DropDir }
Expand-Archive -Path $DropZip -DestinationPath $DropDir -Force

# Resolve the canonical inner folder. Azure wraps everything under "drop/".
$inner = Get-ChildItem $DropDir -Directory | Select-Object -First 1
if (-not $inner) { throw "Unexpected drop archive layout." }
$root = $inner.FullName
$binDir = Join-Path $root "binaries/vsprojects/x64/ReleaseWithClangCL"
$assetsDir = Join-Path $root "assets"
if (-not (Test-Path $binDir)) { throw "Could not find binaries in drop: $binDir" }

Write-Step "Staging runtime to $RuntimeDir ..."
# Clean output but keep the BUILD.txt so we can roll back if needed.
Get-ChildItem $RuntimeDir -Force -Recurse -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
$emuOut = Join-Path $RuntimeDir 'emulator'
$biosOut = Join-Path $RuntimeDir 'bios'
New-Item -ItemType Directory -Path $emuOut -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $emuOut 'fonts') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $emuOut 'i18n') -Force | Out-Null
New-Item -ItemType Directory -Path $biosOut -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $RuntimeDir 'tools') -Force | Out-Null

# Emulator + supporting binaries (everything PCSX-Redux needs at runtime).
# `pcsx-redux.exe` is a thin wrapper that re-launches `pcsx-redux.main`; both
# must sit next to all DLLs in the same directory.
$emulatorFiles = @(
    'pcsx-redux.exe', 'pcsx-redux.main',
    'crashpad_handler.exe', 'crashpad_wer.dll',
    'glfw3.dll', 'libwinpthread-1.dll', 'lua51.dll', 'sentry.dll',
    'avcodec-59.dll', 'avdevice-59.dll', 'avfilter-8.dll',
    'avformat-59.dll', 'avutil-57.dll', 'swresample-4.dll', 'swscale-6.dll'
)
foreach ($f in $emulatorFiles) {
    $src = Join-Path $binDir $f
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $emuOut $f) -Force
    }
    else {
        Write-Warning "missing expected file in drop: $f"
    }
}

# Build-pipeline helper tools.
$toolFiles = @('exe2elf.exe', 'exe2iso.exe', 'ps1-packer.exe', 'psyq-obj-parser.exe', 'authoring.exe', 'modconv.exe')
foreach ($f in $toolFiles) {
    $src = Join-Path $binDir $f
    if (Test-Path $src) { Copy-Item $src (Join-Path $RuntimeDir "tools/$f") -Force }
}

# PCSX-Redux's findResource() in src/core/system.cc walks:
#   <binDir>/<name>, <binDir>/<releasePath>/<name>, <binDir>/../share/pcsx-redux/<releasePath>/<name>, ...
# It does NOT look in <binDir>/assets/, so we flatten the Azure drop layout
# (assets/fonts → fonts, assets/i18n → i18n, etc.) into <binDir>/.
$fontsSrc = Join-Path $assetsDir 'fonts'
if (Test-Path $fontsSrc) {
    Copy-Item (Join-Path $fontsSrc '*') (Join-Path $emuOut 'fonts') -Recurse -Force
}
$i18nSrc = Join-Path $assetsDir 'i18n'
if (Test-Path $i18nSrc) {
    Copy-Item (Join-Path $i18nSrc '*') (Join-Path $emuOut 'i18n') -Recurse -Force
}
foreach ($f in @('gamecontrollerdb.txt')) {
    $src = Join-Path $assetsDir $f
    if (Test-Path $src) { Copy-Item $src (Join-Path $emuOut $f) -Force }
}

# License.
$license = Join-Path $assetsDir 'LICENSE'
if (Test-Path $license) {
    Copy-Item $license (Join-Path $RuntimeDir 'LICENSE-PCSX-Redux.txt') -Force
}

# ---------------------------------------------------------------------------
# Stage OpenBIOS (separate Azure DevOps pipeline; artifact name "bios").
# Mipsync auto-loads ps1_runtime/bios/openbios.bin when launching the bundled
# emulator so PCSX-Redux can boot without a retail BIOS dump.
# ---------------------------------------------------------------------------
Write-Step "Fetching OpenBIOS artifact metadata..."
$biosArtifactsUrl = "$AzureProject/builds/$biosBuildId/artifacts?api-version=7.0"
$biosArtifacts = (Invoke-WebRequest -UseBasicParsing -Uri $biosArtifactsUrl | Select-Object -ExpandProperty Content | ConvertFrom-Json).value
$biosArt = $biosArtifacts | Where-Object { $_.name -eq 'bios' } | Select-Object -First 1
if (-not $biosArt) { throw "No 'bios' artifact in OpenBIOS build $biosBuildId." }
Write-Step "Downloading OpenBIOS archive (~2 MB)..."
Invoke-WebRequest -UseBasicParsing -Uri $biosArt.resource.downloadUrl -OutFile $BiosZip

Write-Step "Extracting OpenBIOS..."
if (Test-Path $BiosDropDir) { Remove-Item -Recurse -Force $BiosDropDir }
Expand-Archive -Path $BiosZip -DestinationPath $BiosDropDir -Force

# Azure wraps everything as bios/openbios/src/mips/openbios/openbios.bin
$openBiosBin = Get-ChildItem -Path $BiosDropDir -Recurse -Filter 'openbios.bin' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $openBiosBin) { throw "openbios.bin not found in extracted artifact." }
Copy-Item $openBiosBin.FullName (Join-Path $biosOut 'openbios.bin') -Force
Write-Step "Staged openbios.bin ($($openBiosBin.Length) bytes)"

# OpenBIOS MIT license lives in the pcsx-redux drop under src/mips/openbios/LICENSE.
$openBiosLicense = Join-Path $root 'src/mips/openbios/LICENSE'
if (-not (Test-Path $openBiosLicense)) {
    # Fallback short MIT notice so we never ship binaries without an attribution.
    @'
OpenBIOS is licensed under the MIT License. The original notice ships with
the source tree at https://github.com/grumpycoders/pcsx-redux under
src/mips/openbios/LICENSE.

Copyright (c) Nicolas "Pixel" Noble and PCSX-Redux contributors.
Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the conditions of the MIT
License.
'@ | Out-File -FilePath (Join-Path $RuntimeDir 'LICENSE-OpenBIOS.txt') -Encoding ASCII
} else {
    Copy-Item $openBiosLicense (Join-Path $RuntimeDir 'LICENSE-OpenBIOS.txt') -Force
}

@"
PS1 runtime bundled with Mipsync Engine.

PCSX-Redux emulator:
  buildId=$buildId
  buildNumber=$buildNumber
  license=GPL-2.0 (see LICENSE-PCSX-Redux.txt)
  source=https://github.com/grumpycoders/pcsx-redux

OpenBIOS:
  biosBuildId=$biosBuildId
  biosBuildNumber=$biosBuildNumber
  license=MIT (see LICENSE-OpenBIOS.txt)
  source=https://github.com/grumpycoders/pcsx-redux/tree/main/src/mips/openbios

fetched=$(Get-Date -Format 's')
"@ | Out-File -FilePath $BuildTxt -Encoding ASCII

@"
Mipsync Engine bundles PCSX-Redux ($buildNumber) plus OpenBIOS as the
default PS1 emulator + BIOS. When the engine launches a PS1 build it
auto-loads ``ps1_runtime/bios/openbios.bin`` via PCSX-Redux's ``-bios`` flag,
so out-of-the-box PS1 builds can be tested without supplying a retail BIOS
dump.

To override:
  - emulator: set MIPSYNC_PS1_EMULATOR=<path> or change
    "Editor: PS1 Emulator" in Build Settings.
  - BIOS: provide a retail BIOS in Build Settings → "BIOS override
    (optional)" or set MIPSYNC_OPENBIOS_PATH via the Hub.
"@ | Out-File -FilePath (Join-Path $RuntimeDir 'README.txt') -Encoding ASCII

Write-Step "Done. Runtime staged under: $RuntimeDir"
