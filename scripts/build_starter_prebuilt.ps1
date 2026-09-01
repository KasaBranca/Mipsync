# Mipsync Engine — regenerate templates/ps1/starter/prebuilt/*
# ---------------------------------------------------------------------------
# Builds the PS1 starter through PSn00bSDK + Ninja and copies the resulting
# PSX.EXE / game.cue / game.bin into templates/ps1/starter/prebuilt/. The
# committed prebuilt files are what Mipsync ships so that "Build && Run in
# Emulator" works out-of-the-box even before the user installs PSn00bSDK.
#
# Requirements:
#   - Environment variable PSN00BSDK pointing to a PSn00bSDK 0.24+ install
#     (the one downloaded by the Hub's "Install PSn00bSDK" works).
#   - Ninja on PATH (or in third_party/.cache/ninja/ninja.exe).
#   - CMake 3.21+ on PATH.

[CmdletBinding()]
param(
    [string]$RepoRoot,
    [string]$SdkRoot = $env:PSN00BSDK
)

$ErrorActionPreference = "Continue"

if (-not $RepoRoot -or -not (Test-Path $RepoRoot)) {
    $scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    $RepoRoot = (Resolve-Path (Join-Path $scriptDir '..')).Path
}

if (-not $SdkRoot -or -not (Test-Path $SdkRoot)) {
    Write-Error "Set PSN00BSDK or pass -SdkRoot to point at a PSn00bSDK 0.24+ install."
    exit 1
}

$psn00bLibs = Join-Path $SdkRoot "lib/libpsn00b"
if (-not (Test-Path (Join-Path $psn00bLibs "cmake/sdk.cmake"))) {
    Write-Error "PSn00bSDK CMake toolchain not found at $psn00bLibs (need 0.24+)."
    exit 1
}

$ninja = $null
foreach ($candidate in @(
    (Join-Path $RepoRoot "third_party/.cache/ninja/ninja.exe"),
    "ninja.exe"
)) {
    if (Test-Path $candidate) { $ninja = (Resolve-Path $candidate).Path; break }
    $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($cmd) { $ninja = $cmd.Source; break }
}
if (-not $ninja) {
    Write-Error "ninja.exe not found. Install ninja-build or unzip ninja-win to third_party/.cache/ninja/."
    exit 1
}

$starterSrc = Join-Path $RepoRoot "templates/ps1/starter"
$workRoot   = Join-Path $RepoRoot "third_party/.cache/starter-build"
$prebuilt   = Join-Path $starterSrc "prebuilt"

if (Test-Path $workRoot) { Remove-Item -Recurse -Force $workRoot }
New-Item -ItemType Directory -Path (Join-Path $workRoot "ps1_src") -Force | Out-Null
Copy-Item (Join-Path $starterSrc "*") (Join-Path $workRoot "ps1_src") -Recurse -Force -Exclude "prebuilt"

$env:PSN00BSDK = $SdkRoot
$env:PSN00BSDK_LIBS = $psn00bLibs
$env:Path = "$(Split-Path $ninja);$(Join-Path $SdkRoot 'bin');$env:Path"

$src = Join-Path $workRoot "ps1_src"
Push-Location $src
try {
    Write-Host "[starter] Configuring..."
    cmake -S . -B build -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$psn00bLibs/cmake/sdk.cmake" `
        "-DCMAKE_BUILD_TYPE=Release" `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DPSN00BSDK_TARGET=mipsel-none-elf" 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

    Write-Host "[starter] Building..."
    cmake --build build 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

    $exe = Get-ChildItem (Join-Path $src "build") -Filter "*.exe" -Recurse | Select-Object -First 1
    $cue = Get-ChildItem (Join-Path $src "build") -Filter "*.cue" -Recurse | Select-Object -First 1
    $bin = Get-ChildItem (Join-Path $src "build") -Filter "*.bin" -Recurse | Select-Object -First 1
    if (-not $exe) { throw "No PS-EXE produced." }
} finally {
    Pop-Location
}

New-Item -ItemType Directory -Path $prebuilt -Force | Out-Null
Copy-Item $exe.FullName (Join-Path $prebuilt "PSX.EXE") -Force
if ($cue -and $bin) {
    Copy-Item $cue.FullName (Join-Path $prebuilt "game.cue") -Force
    Copy-Item $bin.FullName (Join-Path $prebuilt "game.bin") -Force
    (Get-Content (Join-Path $prebuilt "game.cue")) -replace '\.bin\b', '.bin' `
        -replace '"[^"]*\.bin"', '"game.bin"' |
        Set-Content -Encoding ASCII (Join-Path $prebuilt "game.cue")
}

@"
Pre-built PS1 starter, compiled with PSn00bSDK 0.24.
This artifact is bundled with the Mipsync engine so that "Build && Run in
Emulator" succeeds even before PSn00bSDK is installed locally.

Generated from templates/ps1/starter/. To refresh:
  scripts/build_starter_prebuilt.ps1 -SdkRoot <path to PSn00bSDK>
"@ | Out-File -FilePath (Join-Path $prebuilt "README.txt") -Encoding ASCII

Write-Host "[starter] Prebuilt staged at $prebuilt"
Get-ChildItem $prebuilt | Format-Table Name, Length -AutoSize
