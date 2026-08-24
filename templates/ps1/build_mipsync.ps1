param(
    [string]$ProductRoot = $PSScriptRoot,
    [string]$SdkRoot = "",
    [string]$EngineRoot = ""
)

# CMake/Ninja write warnings to stderr; PowerShell's "Stop" would treat them as
# fatal native-command errors, so we keep ErrorActionPreference loose and rely
# on $LASTEXITCODE for failure detection.
$ErrorActionPreference = "Continue"

$log = Join-Path $ProductRoot "build.log"
function Log($msg) { "$(Get-Date -Format o) $msg" | Tee-Object -FilePath $log -Append | Out-Null }

Remove-Item -ErrorAction SilentlyContinue $log

Log "Mipsync PS1 build starting in $ProductRoot"

$src = Join-Path $ProductRoot "ps1_src"
$out = Join-Path $ProductRoot "out"
New-Item -ItemType Directory -Force -Path $out | Out-Null

if ($SdkRoot -and (Test-Path $SdkRoot)) {
    $env:PSN00BSDK = $SdkRoot
}

if (-not $env:PSN00BSDK -or -not (Test-Path $env:PSN00BSDK)) {
    Log "PSN00BSDK not set. Cannot compile scripts into PSX.EXE."
    Log "Install PSn00bSDK, set the environment variable, then re-run."
    exit 2
}

# PSn00bSDK 0.24+ ships a CMake toolchain under lib/libpsn00b. Resolve it so
# CMakePresets.json's $env{PSN00BSDK_LIBS} expansion picks the right path.
$psn00bLibs = Join-Path $env:PSN00BSDK "lib/libpsn00b"
if (-not (Test-Path (Join-Path $psn00bLibs "cmake/sdk.cmake"))) {
    Log "PSn00bSDK CMake toolchain not found under: $psn00bLibs"
    Log "Expected file: lib/libpsn00b/cmake/sdk.cmake"
    exit 1
}
$env:PSN00BSDK_LIBS = $psn00bLibs

# Ninja is required by the PSn00bSDK preset and must be available on PATH.
$ninja = ""
$ninjaCandidates = @("ninja.exe")
foreach ($candidate in $ninjaCandidates) {
    try {
        if (Test-Path $candidate) { $ninja = (Resolve-Path $candidate).Path; break }
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($cmd) { $ninja = $cmd.Source; break }
    } catch { }
}
if (-not $ninja) {
    Log "ninja.exe not found. Install ninja-build and add it to PATH."
    exit 1
}

Log "Using PSN00BSDK_LIBS=$env:PSN00BSDK_LIBS"
Log "Using ninja: $ninja"

# Make PSn00bSDK's MIPS toolchain available to CMake.
$psnBin = Join-Path $env:PSN00BSDK "bin"
if (Test-Path $psnBin) {
    if (-not ($env:Path.Split(';') -contains $psnBin)) {
        $env:Path = "$psnBin;$env:Path"
    }
}

$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmake) {
    Log "cmake.exe not found on PATH."
    exit 1
}

Push-Location $src
try {
    Log "Configuring PSn00bSDK build (CMake + Ninja)..."
    & $cmake.Source -S . -B build -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$psn00bLibs/cmake/sdk.cmake" `
        "-DCMAKE_BUILD_TYPE=Release" `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DPSN00BSDK_TARGET=mipsel-none-elf" 2>&1 | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

    Log "Building..."
    & $cmake.Source --build build 2>&1 | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($LASTEXITCODE)" }

    $exe = Get-ChildItem (Join-Path $src "build") -Filter "*.exe" -Recurse | Select-Object -First 1
    if (-not $exe) {
        $exe = Get-ChildItem (Join-Path $src "build") -Filter "*.psexe" -Recurse | Select-Object -First 1
    }
    if (-not $exe) { throw "PSn00bSDK did not produce a PS-EXE binary" }
    Copy-Item $exe.FullName (Join-Path $out "PSX.EXE") -Force
    Log "Copied $($exe.FullName) -> $out\PSX.EXE"

    $cue = Get-ChildItem (Join-Path $src "build") -Filter "*.cue" -Recurse | Select-Object -First 1
    $bin = Get-ChildItem (Join-Path $src "build") -Filter "*.bin" -Recurse | Select-Object -First 1
    if ($cue -and $bin) {
        $trackName = "game.bin"
        Copy-Item $cue.FullName (Join-Path $out "game.cue") -Force
        try {
            Copy-Item $bin.FullName (Join-Path $out $trackName) -Force -ErrorAction Stop
        } catch {
            # Emulators keep the current BIN open on Windows. Never silently
            # leave a stale disc behind: write a fresh uniquely named track and
            # point game.cue at it instead.
            $trackName = "game_" + [DateTime]::UtcNow.ToString("yyyyMMddHHmmssfff") + ".bin"
            Copy-Item $bin.FullName (Join-Path $out $trackName) -Force -ErrorAction Stop
            Log "Existing game.bin is in use; wrote $trackName instead."
        }
        # Accept quoted and unquoted mkpsxiso FILE syntax.
        (Get-Content (Join-Path $out "game.cue")) `
            -replace 'FILE\s+(?:"[^"]+"|[^\s]+)\s+BINARY', "FILE `"$trackName`" BINARY" |
            Set-Content -Encoding ASCII (Join-Path $out "game.cue")
        Log "Disc image: $out\game.cue + $trackName"
    } else {
        # Fallback minimal cue pointing directly at the PS-EXE (sufficient for
        # PCSX-Redux/DuckStation's "-iso" loaders that accept raw binaries).
        @"
FILE "PSX.EXE" BINARY
  TRACK 01 MODE2/2352
    INDEX 01 00:00:00
"@ | Out-File -FilePath (Join-Path $out "game.cue") -Encoding ascii
        Log "No CD image produced; wrote stub game.cue pointing at PSX.EXE"
    }
} finally {
    Pop-Location
}

Log "Done."
exit 0
