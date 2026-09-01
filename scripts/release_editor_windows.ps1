param(
  [Parameter(Mandatory = $true)]
  [string]$Version
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$outDir = Join-Path $root "release"
$engineExe = Join-Path $root "build\src\MipsyncEngine.exe"
$cliExe = Join-Path $root "build\src\mipsync.exe"
$engineStage = Join-Path $outDir "engine-$Version"
$engineZip = Join-Path $outDir "MipsyncEngine_Windows_x64.zip"

& cmake --build (Join-Path $root "build") --config Release --target MipsyncEngine MipsyncCLI
if ($LASTEXITCODE -ne 0) { throw "Engine build failed." }
if (!(Test-Path $engineExe)) { throw "Engine executable not found: $engineExe" }
if (!(Test-Path $cliExe)) { throw "Mipsync CLI executable not found: $cliExe" }

if (Test-Path $engineStage) { Remove-Item $engineStage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $engineStage | Out-Null
Copy-Item $engineExe $engineStage -Force
Copy-Item $cliExe $engineStage -Force

New-Item -ItemType Directory -Force -Path (Join-Path $engineStage "resources\icons") | Out-Null
Copy-Item (Join-Path $root "resources\icons\app_icon.png") (Join-Path $engineStage "resources\icons\app_icon.png") -Force
Copy-Item (Join-Path $root "resources\icons\project") (Join-Path $engineStage "resources\icons\project") -Recurse -Force
Copy-Item (Join-Path $root "resources\fonts") (Join-Path $engineStage "resources\fonts") -Recurse -Force
if (Test-Path (Join-Path $root "resources\licenses")) {
  Copy-Item (Join-Path $root "resources\licenses") (Join-Path $engineStage "resources\licenses") -Recurse -Force
}
Copy-Item (Join-Path $root "templates\ps1") (Join-Path $engineStage "templates\ps1") -Recurse -Force
Copy-Item (Join-Path $root "skills") (Join-Path $engineStage "skills") -Recurse -Force

$builtTools = Join-Path $root "build\src\tools"
if (Test-Path $builtTools) {
  New-Item -ItemType Directory -Force -Path (Join-Path $engineStage "tools") | Out-Null
  foreach ($toolName in @("vscode-mips", "mips-language-server")) {
    $toolSource = Join-Path $builtTools $toolName
    if (Test-Path $toolSource) {
      Copy-Item $toolSource (Join-Path $engineStage "tools") -Recurse -Force
    }
  }
}

$ps1Runtime = Join-Path $root "third_party\ps1_runtime"
if (Test-Path (Join-Path $ps1Runtime "emulator\pcsx-redux.exe")) {
  Copy-Item $ps1Runtime (Join-Path $engineStage "ps1_runtime") -Recurse -Force
} else {
  Write-Warning "Bundled PS1 runtime was not found."
}

$mingwBin = "C:\msys64\mingw64\bin"
foreach ($dll in @("libgcc_s_seh-1.dll", "libwinpthread-1.dll", "libstdc++-6.dll")) {
  $source = Join-Path $mingwBin $dll
  if (Test-Path $source) { Copy-Item $source $engineStage -Force }
}

$ninja = Join-Path $root "third_party\.cache\ninja\ninja.exe"
if (Test-Path $ninja) {
  New-Item -ItemType Directory -Force -Path (Join-Path $engineStage "tools") | Out-Null
  Copy-Item $ninja (Join-Path $engineStage "tools\ninja.exe") -Force
}

if (Test-Path $engineZip) { Remove-Item $engineZip -Force }
Compress-Archive -Path "$engineStage\*" -DestinationPath $engineZip -CompressionLevel Optimal
Write-Host "Editor package ready: $engineZip"
