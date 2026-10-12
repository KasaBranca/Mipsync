param(
    [Parameter(Mandatory = $true)][string]$EngineExe,
    [Parameter(Mandatory = $true)][string]$CliExe,
    [Parameter(Mandatory = $true)][string]$WorkDir
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandArgumentPassing = 'Standard'
$changedName = '変更済みタイトル'
$projectDir = Join-Path $WorkDir 'scene open project'
New-Item -ItemType Directory -Force -Path (Join-Path $projectDir 'scenes') | Out-Null
foreach ($name in @('First', 'Second')) {
    @{ entities = @(@{ id = 1; name = $name }) } | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath (Join-Path $projectDir "scenes/$name.nscene") -Encoding UTF8
}
New-Item -ItemType Directory -Force -Path (Join-Path $projectDir 'assets/scripts') | Out-Null
@'
class VisibilityTest : MipsBehaviour {
    void Start() { UI.SetTextVisible(2, true); UI.SetImageVisible(2, true); }
}
'@ | Set-Content -LiteralPath (Join-Path $projectDir 'assets/scripts/VisibilityTest.mips') -Encoding UTF8
@{ entities = @(
    @{ id = 1; name = 'First'; mipsScripts = @(@{ path = 'assets/scripts/VisibilityTest.mips' }) },
    @{ id = 2; name = 'Indicator'; uiText = @{ text = 'Loading...'; enabled = $false }; uiImage = @{ enabled = $false } }
) } | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath (Join-Path $projectDir 'scenes/First.nscene') -Encoding UTF8
@{ entities = @(@{ id = 1; name = 'Partial' }, @{ id = 'invalid' }) } | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath (Join-Path $projectDir 'scenes/Invalid.nscene') -Encoding UTF8
@{ name = 'SceneOpenTest'; defaultScene = 'scenes/First.nscene'; editorLastScene = 'scenes/First.nscene' } |
    ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $projectDir 'nostalty.project') -Encoding UTF8
$engineProcess = Start-Process -FilePath $EngineExe -ArgumentList @('--no-splash', '--project', "`"$projectDir`"") `
    -WindowStyle Hidden -PassThru
function Invoke-Cli([string[]]$Arguments) {
    Write-Output -InputObject "CLI: $($Arguments -join ' ')" | Out-Host
    $text = & $CliExe --project $projectDir --instance $engineProcess.Id @Arguments --json
    return $text | ConvertFrom-Json
}
function Assert-Success($result) {
    if (-not $result.success) { throw ($result | ConvertTo-Json -Depth 20) }
}
try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        $instances = & $CliExe instances --json | ConvertFrom-Json
        if (@($instances.result.instances | Where-Object { $_.processId -eq $engineProcess.Id }).Count -eq 1) {
            $ready = $true; break
        }
        if ($engineProcess.HasExited) { throw 'Editor exited before exposing CLI' }
        Start-Sleep -Milliseconds 500
    }
    if (-not $ready) { throw 'Editor did not expose CLI within 30 seconds' }
    $open = Invoke-Cli @('scene', 'open', 'scenes/Second.nscene')
    Assert-Success $open
    if ($open.result.entityCount -ne 1 -or $open.result.dirty) { throw 'Unexpected opened scene state' }
    Assert-Success (Invoke-Cli @('entity', 'set', '1', '--name', $changedName))
    $blocked = Invoke-Cli @('scene', 'open', 'scenes/First.nscene')
    if ($blocked.success) { throw 'Opening discarded unsaved edits' }
    $malformed = Invoke-Cli @('scene', 'open', 'scenes/Invalid.nscene', '--save-current', 'true')
    if ($malformed.success) { throw 'Malformed scene was accepted' }
    $current = Invoke-Cli @('entity', 'inspect', '1')
    Assert-Success $current
    if ($current.result.name -ne $changedName) { throw 'Failed opening modified current scene or corrupted Unicode' }
    $unsaved = Get-Content -LiteralPath (Join-Path $projectDir 'scenes/Second.nscene') -Raw | ConvertFrom-Json
    if ($unsaved.entities[0].name -ne 'Second') { throw 'Invalid destination unexpectedly saved current scene' }
    Assert-Success (Invoke-Cli @('scene', 'open', 'scenes/First.nscene', '--save-current', 'true'))
    $saved = Get-Content -LiteralPath (Join-Path $projectDir 'scenes/Second.nscene') -Raw | ConvertFrom-Json
    if ($saved.entities[0].name -ne $changedName) { throw 'save-current did not persist edits' }
    foreach ($path in @('../outside.nscene', 'nostalty.project', 'scenes/Missing.nscene', 'scenes/Invalid.nscene')) {
        if ((Invoke-Cli @('scene', 'open', $path)).success) { throw "Invalid path accepted: $path" }
        $unchanged = Invoke-Cli @('entity', 'inspect', '1')
        Assert-Success $unchanged
        if ($unchanged.result.name -ne 'First') { throw "Failed open changed scene: $path" }
    }
    Assert-Success (Invoke-Cli @('runtime', 'play'))
    $runtimeScene = Invoke-Cli @('scene', 'get-json')
    Assert-Success $runtimeScene
    $indicator = $runtimeScene.result.scene.entities | Where-Object { $_.id -eq 2 }
    if ($indicator.uiText.enabled -eq $false) { throw 'UI.SetTextVisible did not show text in Play Mode' }
    if ($indicator.uiImage.enabled -eq $false) { throw 'UI.SetImageVisible did not show image in Play Mode' }
    if ((Invoke-Cli @('scene', 'open', 'scenes/Second.nscene')).success) { throw 'Opening allowed during Play Mode' }
    Assert-Success (Invoke-Cli @('runtime', 'stop'))
    $editScene = Invoke-Cli @('scene', 'get-json')
    Assert-Success $editScene
    $indicator = $editScene.result.scene.entities | Where-Object { $_.id -eq 2 }
    if ($indicator.uiText.enabled -ne $false) { throw 'Runtime text visibility was not restored on Stop' }
    if ($indicator.uiImage.enabled -ne $false) { throw 'Runtime image visibility was not restored on Stop' }
    Assert-Success (Invoke-Cli @('scene', 'open', (Join-Path $projectDir 'scenes/Second.nscene')))
    $lastScene = Get-Content -LiteralPath (Join-Path $projectDir 'nostalty.project') -Raw | ConvertFrom-Json
    if ($lastScene.editorLastScene.Replace('\', '/') -ne 'scenes/Second.nscene') { throw 'Last open scene was not persisted' }
    Write-Output 'Editor scene.open integration passed.'
} finally {
    if (-not $engineProcess.HasExited) {
        $engineProcess.CloseMainWindow() | Out-Null
        if (-not $engineProcess.WaitForExit(5000)) {
            Write-Warning 'Stopping the isolated test editor after a failed graceful close'
            Stop-Process -Id $engineProcess.Id
        }
    }
}
