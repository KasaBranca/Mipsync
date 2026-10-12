param([Parameter(Mandatory=$true)][string]$EngineExe,
      [Parameter(Mandatory=$true)][string]$CliExe,
      [Parameter(Mandatory=$true)][string]$WorkDir)
$ErrorActionPreference='Stop'
$project=Join-Path $WorkDir 'fps capture project'
New-Item -ItemType Directory -Force -Path "$project/scenes" | Out-Null
@{name='FPSCapture';defaultScene='scenes/default.nscene';editorLastScene='scenes/default.nscene'} |
    ConvertTo-Json | Set-Content -LiteralPath "$project/nostalty.project" -Encoding utf8
@{entities=@(@{id=1;name='FPS';camera=@{primary=$true};
    mipsScripts=@(@{path='assets/scripts/FirstPersonController.mips';fields=@{lookSpeed=28.8}});
    collider=@{shape=2;radius=.35;capsuleHeight=1.35};
    rigidbody=@{bodyType=1;characterController=$true;freezeRotation=$true;useGravity=$false}})} |
    ConvertTo-Json -Depth 12 | Set-Content -LiteralPath "$project/scenes/default.nscene" -Encoding utf8
$process=Start-Process -FilePath $EngineExe -ArgumentList @('--no-splash','--project',"`"$project`"") -WindowStyle Hidden -PassThru
function Invoke-Editor([string[]]$Arguments) {
    $response=& $CliExe --project $project --instance $process.Id @Arguments --json | ConvertFrom-Json
    if (!$response.success) { throw ($response | ConvertTo-Json -Depth 12) }
    return $response.result
}
try {
    $ready=$false
    for ($i=0; $i -lt 60; ++$i) {
        $instances=& $CliExe instances --json | ConvertFrom-Json
        if (@($instances.result.instances | Where-Object {$_.processId -eq $process.Id}).Count) {$ready=$true;break}
        if ($process.HasExited) {throw 'FPS editor exited'}
        Start-Sleep -Milliseconds 500
    }
    if (!$ready) {throw 'FPS editor unavailable'}
    $scene=Invoke-Editor @('scene','get-json')
    if ([Math]::Abs($scene.scene.entities[0].mipsScripts[0].fields.lookSpeed-28.8) -gt .000001) {throw 'Builtin update discarded FPS tuning'}
    $null=Invoke-Editor @('runtime','play')
    $state=Invoke-Editor @('runtime','inspect')
    if (!$state.playing -or $state.input.cursorLocked) {throw 'FPS automatically hid cursor before Game View click'}
    $null=Invoke-Editor @('runtime','stop')
    $state=Invoke-Editor @('runtime','inspect')
    if ($state.playing -or $state.input.cursorLocked) {throw 'FPS Stop did not release capture'}
    Write-Output 'Native editor click-required FPS capture, Stop release and builtin tuning preservation passed.'
} finally {
    if (!$process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (!$process.WaitForExit(5000)) {Stop-Process -Id $process.Id}
    }
}
