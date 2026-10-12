param(
    [Parameter(Mandatory = $true)][string]$EngineExe,
    [Parameter(Mandatory = $true)][string]$WorkDir,
    [switch]$Compile
)

$ErrorActionPreference = 'Stop'
$projectDir = Join-Path $WorkDir 'startup scene project'
$scenesDir = Join-Path $projectDir 'scenes'
New-Item -ItemType Directory -Force -Path $scenesDir | Out-Null
foreach ($sceneName in @('First', 'Second')) {
    @{ entities = @(@{ id = 1; name = "StartupMarker$sceneName" }) } |
        ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath (Join-Path $scenesDir "$sceneName.nscene") -Encoding UTF8
}
$scriptsDir = Join-Path $projectDir 'assets/scripts'
New-Item -ItemType Directory -Force -Path $scriptsDir | Out-Null
@'
class SceneNavigation : MipsBehaviour {
    public int destination = 1;
    public void LoadByIndex() { Scene.LoadBuildIndex(destination); }
    public void LoadByPath() { Scene.Load("scenes/Second.nscene"); }
    public void ShowText() { UI.SetTextVisible(2, true); }
    public void ShowImage() { UI.SetImageVisible(2, true); }
}
'@ | Set-Content -LiteralPath (Join-Path $scriptsDir 'SceneNavigation.mips') -Encoding UTF8
@{ entities = @(@{
    id = 1; name = 'StartupMarkerFirst'
    mipsScripts = @(@{ path = 'assets/scripts/SceneNavigation.mips'; fields = @{ destination = 1 } })
}, @{
    id = 2; name = 'HiddenText'; parent = 3
    rectTransform = @{ anchorMin = @(0.5, 0.5); anchorMax = @(0.5, 0.5); sizeDelta = @(200, 50) }
    uiText = @{ text = 'Loading...'; enabled = $false }
    uiImage = @{ color = @(0, 0, 0, 1); enabled = $false }
}, @{
    id = 3; name = 'Canvas'; canvas = @{ referenceResolution = @(320, 240) }
}) } | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath (Join-Path $scenesDir 'First.nscene') -Encoding UTF8

function Write-Project($sceneOrder, $legacyStartupIndex) {
    @{
        name = 'StartupOrder'
        defaultScene = 'scenes/Second.nscene'
        editorLastScene = 'scenes/Second.nscene'
        playerSettings = @{
            productName = 'StartupOrder'
            scenesInBuild = @($sceneOrder)
            startupSceneIndex = $legacyStartupIndex
        }
    } | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath (Join-Path $projectDir 'nostalty.project') -Encoding UTF8
}

function Assert-Export($sceneOrder, $legacyStartupIndex, $expectedScene) {
    Write-Project $sceneOrder $legacyStartupIndex
    $output = & $EngineExe --export-ps1 $projectDir 2>&1
    if ($LASTEXITCODE -ne 0) { throw "PS1 export failed: $output" }
    $sceneData = Get-Content -Raw -LiteralPath (
        Join-Path $projectDir 'Builds/PS1/StartupOrder/ps1_src/generated/scene_data.c')
    if (-not $sceneData.Contains("StartupMarker$expectedScene")) {
        throw "PS1 ignored scene order: expected $expectedScene with legacy startup index $legacyStartupIndex"
    }
    $otherScene = if ($expectedScene -eq 'First') { 'Second' } else { 'First' }
    if ($sceneData.Contains("StartupMarker$otherScene")) {
        throw "PS1 exported the wrong scene: $otherScene"
    }
    $sourceDir = Join-Path $projectDir 'Builds/PS1/StartupOrder/ps1_src'
    $destination = Get-Content -Raw -LiteralPath (Join-Path $sourceDir 'scenes/1/generated/scene_data.c')
    if (-not $destination.Contains("StartupMarker$otherScene")) {
        throw 'PS1 did not export the other build scene for runtime loading'
    }
    $catalog = Get-Content -Raw -LiteralPath (Join-Path $sourceDir 'generated/scene_catalog.h')
    $otherCatalog = Get-Content -Raw -LiteralPath (Join-Path $sourceDir 'scenes/1/generated/scene_catalog.h')
    if (-not $catalog.Contains('MIPSYNC_BUILD_SCENE_COUNT 2') -or
        -not $catalog.Contains('MIPSYNC_CURRENT_SCENE_INDEX 0') -or
        -not $otherCatalog.Contains('MIPSYNC_CURRENT_SCENE_INDEX 1') -or
        -not $catalog.Contains('SCN00001.EXE')) {
        throw 'PS1 scene catalog is inconsistent with the build list'
    }
    $firstData = if ($expectedScene -eq 'First') { $sceneData } else { $destination }
    if (-not $firstData.Contains('Loading...') -or $firstData -notmatch '2u, 0 \},') {
        throw 'Initially hidden UIText was not exported for runtime visibility changes'
    }
}

# Neither a previously selected scene nor the editor's open/default scene
# may override row zero. Reordering must change the generated scene tables.
Assert-Export @('scenes/First.nscene', 'scenes/Second.nscene') 1 'First'
Assert-Export @('scenes/Second.nscene', 'scenes/First.nscene') 1 'Second'
Assert-Export @('scenes/First.nscene', 'scenes/Second.nscene') 999 'First'

# A missing first scene must fail instead of silently booting a later scene.
Write-Project @('scenes/Missing.nscene', 'scenes/Second.nscene') 1
$output = & $EngineExe --export-ps1 $projectDir 2>&1
if ($LASTEXITCODE -eq 0 -or ($output -join "`n") -notmatch 'Startup scene not found:.*Missing.nscene') {
    throw "PS1 did not reject the missing first scene: $output"
}

# Export failures happen before the SDK script, but must replace stale logs.
$productDir = Join-Path $projectDir 'Builds/PS1/StartupOrder'
'Old successful build' | Set-Content -LiteralPath (Join-Path $productDir 'build.log')
$output = & $EngineExe --build-ps1 $projectDir 2>&1
$buildLog = Get-Content -Raw -LiteralPath (Join-Path $productDir 'build.log')
if ($LASTEXITCODE -eq 0 -or $buildLog.Contains('Old successful build') -or
    $buildLog -notmatch 'PS1 build failed:.*PS1 scene export failed:' -or
    $buildLog -notmatch 'Startup scene not found:.*Missing.nscene') {
    throw "Export failure did not write the current build.log: $buildLog"
}

# Optional SDK integration check also exercises the normal Build path and
# verifies that its boot manifest and compiled scene agree on build index zero.
if ($Compile) {
    Write-Project @('scenes/First.nscene', 'scenes/Second.nscene') 1
    $output = & $EngineExe --build-ps1 $projectDir 2>&1
    if ($LASTEXITCODE -ne 0) { throw "PS1 compilation failed: $output" }
    $productDir = Join-Path $projectDir 'Builds/PS1/StartupOrder'
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $productDir 'mipsync_export/boot.json') |
        ConvertFrom-Json
    if ($manifest.startupSceneIndex -ne 0 -or $manifest.scenesInBuild[0] -ne 'scenes/First.nscene') {
        throw 'PS1 boot manifest does not start at build index zero'
    }
    $sceneData = Get-Content -Raw -LiteralPath (Join-Path $productDir 'ps1_src/generated/scene_data.c')
    if (-not $sceneData.Contains('StartupMarkerFirst') -or $sceneData.Contains('StartupMarkerSecond')) {
        throw 'Normal PS1 build compiled the wrong startup scene'
    }
    $buildLog = Get-Content -Raw -LiteralPath (Join-Path $productDir 'build.log')
    if (-not $buildLog.Contains('PS1 RAM image:') -or
        -not (Test-Path -LiteralPath (Join-Path $productDir 'out/PSX.EXE')) -or
        -not (Test-Path -LiteralPath (Join-Path $productDir 'out/SCN00001.EXE'))) {
        throw 'SDK did not compile all PS1 scene executables'
    }
    $savedProject = Get-Content -Raw -LiteralPath (Join-Path $projectDir 'nostalty.project') |
        ConvertFrom-Json
    if ($savedProject.playerSettings.startupSceneIndex -ne 1) {
        throw 'PS1 build unexpectedly modified the project settings'
    }
}
Write-Output 'PS1 startup scene order regression passed.'
