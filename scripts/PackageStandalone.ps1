# Legacy wrapper — prefer File > Build in the editor, or PlayerBuild from the engine.
# Packages using Unity-style layout: <Product>/<Product>.exe + <Product>_Data/

param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [string]$EngineDir = "",
    [string]$ProductName = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $EngineDir) {
    $EngineDir = Join-Path $repoRoot "build\src"
}

$projectDir = (Resolve-Path $ProjectDir).Path
$projectFile = Join-Path $projectDir "nostalty.project"
if (-not (Test-Path $projectFile)) {
    throw "nostalty.project not found in $projectDir"
}

$json = Get-Content $projectFile -Raw | ConvertFrom-Json
if (-not $ProductName) {
    if ($json.playerSettings.productName) { $ProductName = $json.playerSettings.productName }
    else { $ProductName = $json.name }
}

$engineExe = Join-Path $EngineDir "MipsyncEngine.exe"
if (-not (Test-Path $engineExe)) {
    throw "Build the engine first: build\src\MipsyncEngine.exe"
}

$outputParent = Join-Path $projectDir "Builds\Windows"
New-Item -ItemType Directory -Force -Path $outputParent | Out-Null

# Invoke the editor's build path by running engine with a one-shot is not available;
# duplicate layout here for CI/scripts users.

$buildRoot = Join-Path $outputParent $ProductName
$dataDir = Join-Path $buildRoot ($ProductName + "_Data")
$projectCopy = Join-Path $dataDir "Project"
$resourcesDir = Join-Path $dataDir "Resources"

if (Test-Path $buildRoot) { Remove-Item $buildRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $projectCopy, $resourcesDir | Out-Null

$exclude = @('Builds', '.git', '.nostalty')
Get-ChildItem $projectDir | Where-Object { $exclude -notcontains $_.Name } | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $projectCopy $_.Name) -Recurse -Force
}

$scenes = @()
if ($json.playerSettings.scenesInBuild) { $scenes = @($json.playerSettings.scenesInBuild) }
elseif ($json.defaultScene) { $scenes = @($json.defaultScene) }
else { $scenes = @("scenes/default.nscene") }

$boot = @{
    version = 1
    productName = $ProductName
    companyName = $json.playerSettings.companyName
    startupSceneIndex = [int]($json.playerSettings.startupSceneIndex)
    scenesInBuild = $scenes
}
$boot | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $dataDir "boot.json") -Encoding UTF8

Copy-Item $engineExe (Join-Path $buildRoot ($ProductName + ".exe"))
Get-ChildItem $EngineDir -Filter "*.dll" | Copy-Item -Destination $buildRoot
if (Test-Path (Join-Path $EngineDir "resources")) {
    Copy-Item (Join-Path $EngineDir "resources") $resourcesDir -Recurse -Force
}
if (Test-Path (Join-Path $EngineDir "fonts")) {
    Copy-Item (Join-Path $EngineDir "fonts") (Join-Path $resourcesDir "fonts") -Recurse -Force
}

Write-Host "Built: $buildRoot"
