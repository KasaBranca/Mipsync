param([Parameter(Mandatory=$true)][string]$EngineExe,
      [Parameter(Mandatory=$true)][string]$CliExe,
      [Parameter(Mandatory=$true)][string]$WorkDir)
$ErrorActionPreference='Stop'
$PSNativeCommandArgumentPassing='Standard'
$projectDir=Join-Path $WorkDir 'undo workflow project'
New-Item -ItemType Directory -Force -Path "$projectDir/scenes", "$projectDir/assets/scripts" | Out-Null
@{name='UndoWorkflow';defaultScene='scenes/default.nscene';editorLastScene='scenes/default.nscene'} |
    ConvertTo-Json -Depth 12 | Set-Content -LiteralPath "$projectDir/nostalty.project" -Encoding utf8
@{entities=@(@{id=1;name='Initial'},@{id=2;name='Canvas';canvas=@{referenceResolution=@(320,240)}})} |
    ConvertTo-Json -Depth 12 | Set-Content -LiteralPath "$projectDir/scenes/default.nscene" -Encoding utf8
$process=Start-Process -FilePath $EngineExe -ArgumentList @('--no-splash','--project',"`"$projectDir`"") -WindowStyle Hidden -PassThru
function Invoke-Editor([string[]]$Arguments) {
    $result=& $CliExe --project $projectDir --instance $process.Id @Arguments --json | ConvertFrom-Json
    if(-not $result.success) { throw ($result | ConvertTo-Json -Depth 15) }
    return $result.result
}
function Assert-Name([string]$Name) {
    $entity=Invoke-Editor @('entity','inspect','1')
    if($entity.name -ne $Name) {throw "Expected $Name, got $($entity.name)"}
}
try {
    $ready=$false
    for($i=0;$i -lt 60;++$i) {
        $instances=& $CliExe instances --json | ConvertFrom-Json
        if(@($instances.result.instances | Where-Object {$_.processId -eq $process.Id}).Count) {$ready=$true;break}
        if($process.HasExited) {throw 'Isolated editor exited'}
        Start-Sleep -Milliseconds 500
    }
    if(-not $ready) {throw 'Isolated editor unavailable'}
    $null=Invoke-Editor @('entity','set','1','--name','First edit')
    $null=Invoke-Editor @('entity','set','1','--name','Second edit')
    Assert-Name 'Second edit'
    $null=Invoke-Editor @('editor','undo'); Assert-Name 'First edit'
    $null=Invoke-Editor @('editor','undo'); Assert-Name 'Initial'
    $null=Invoke-Editor @('editor','redo'); Assert-Name 'First edit'
    $null=Invoke-Editor @('editor','redo'); Assert-Name 'Second edit'
    $null=Invoke-Editor @('editor','undo')
    $null=Invoke-Editor @('entity','set','1','--name','First edit') # unchanged
    $null=Invoke-Editor @('editor','redo'); Assert-Name 'Second edit'
    $scene=Invoke-Editor @('scene','get-json')
    $canvas=$scene.scene.entities | Where-Object {$_.id -eq 2}
    if(($canvas.canvas.referenceResolution -join ',') -ne '320,240') {throw 'Game View overwrote Canvas reference resolution'}
    $null=Invoke-Editor @('runtime','play')
    $null=Invoke-Editor @('runtime','stop'); Assert-Name 'Second edit'
    $null=Invoke-Editor @('editor','undo'); Assert-Name 'First edit'
    $null=Invoke-Editor @('editor','redo'); Assert-Name 'Second edit'
    $null=Invoke-Editor @('scene','save')
    Write-Output 'Native editor Undo/Redo, command boundaries, no-op Redo, Canvas preservation and Play/Stop history passed.'
} finally {
    if(-not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if(-not $process.WaitForExit(5000)) {Stop-Process -Id $process.Id}
    }
}
