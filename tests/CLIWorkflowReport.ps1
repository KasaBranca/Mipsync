param(
    [Parameter(Mandatory=$true)][string]$EngineExe,
    [Parameter(Mandatory=$true)][string]$CliExe,
    [Parameter(Mandatory=$true)][string]$WorkDir
)
$ErrorActionPreference='Stop'
$PSNativeCommandArgumentPassing='Standard'
$projectDir=Join-Path ([IO.Path]::GetFullPath($WorkDir)) 'workflow-project'
New-Item -ItemType Directory -Force -Path (Join-Path $projectDir 'scenes'),(Join-Path $projectDir 'tools') | Out-Null
@{entities=@(@{id=1;name='Parent';transform=@{position=@(10,0,0);rotation=@(0,90,0);scale=@(-2,1,1)}})} |
    ConvertTo-Json -Depth 20 | Set-Content (Join-Path $projectDir 'scenes/Test.nscene') -Encoding UTF8
@{name='CLI Workflow Regression';defaultScene='scenes/Test.nscene';editorLastScene='scenes/Test.nscene'} |
    ConvertTo-Json | Set-Content (Join-Path $projectDir 'nostalty.project') -Encoding UTF8
$engineProcess=Start-Process -FilePath $EngineExe -ArgumentList @('--no-splash','--project',"`"$projectDir`"") -WindowStyle Hidden -PassThru
function Invoke-Cli([string[]]$Arguments) {
    $text=& $CliExe --project $projectDir --instance $engineProcess.Id @Arguments --json
    if(!$text){throw "No JSON from: $($Arguments -join ' ')"}
    return ($text -join "`n") | ConvertFrom-Json
}
function Success($result) {if(!$result.success){throw ($result|ConvertTo-Json -Depth 30)};return $result.result}
function Require([bool]$condition,[string]$message) {if(!$condition){throw $message}}
function Scene-Text {return ((Success (Invoke-Cli @('scene','get-json'))).scene|ConvertTo-Json -Depth 100 -Compress)}
try {
    $ready=$false
    for($i=0;$i-lt 80;$i++) {
        $instances=& $CliExe instances --json | ConvertFrom-Json
        if(@($instances.result.instances|Where-Object {$_.processId-eq $engineProcess.Id}).Count-eq 1){$ready=$true;break}
        if($engineProcess.HasExited){throw 'Test editor exited before IPC became available'}
        Start-Sleep -Milliseconds 500
    }
    Require $ready 'Test editor failed to expose IPC'
    $null=Success (Invoke-Cli @('material','create','assets/materials/Red.nmat','1','0.1','0.1'))
    $materials=Success (Invoke-Cli @('material','list'))
    Require ($materials.count-eq 1) 'material.list did not find created material'
    $null=Success (Invoke-Cli @('material','inspect','assets/materials/Red.nmat'))
    $null=Success (Invoke-Cli @('asset','list','--type','material'))
    $roof=Success (Invoke-Cli @('promodeler','create','--name','Roof','--shape','pyramid','--size','8,2.5,6','--material','assets/materials/Red.nmat','--render-type','Unlit'))
    Require ($roof.shapeName-eq 'pyramid') 'Pyramid not created'
    foreach($shape in @('box','plane','wedge','stairs','cylinder','cone')) {
        $null=Success (Invoke-Cli @('promodeler','create','--name',$shape,'--shape',$shape,'--size','2,3,4','--sides','8'))
    }
    $null=Success (Invoke-Cli @('promodeler','create','--name','Transformed','--shape','box','--size','2,3,6','--parent','Parent'))
    $transformed=Success (Invoke-Cli @('entity','bounds','Transformed'))
    Require ([Math]::Abs($transformed.bounds.size[0]-6)-lt 0.001-and [Math]::Abs($transformed.bounds.size[2]-4)-lt 0.001-and [Math]::Abs($transformed.bounds.center[0]-10)-lt 0.001) 'Parent rotation or negative scale bounds incorrect'
    $compact=Invoke-Cli @('entity','inspect','Roof','--include-components')
    $raw=Invoke-Cli @('entity','inspect','Roof','--include-mesh-data')
    Require ($compact.result.serialized.proModeler.vertexCount-gt 0) 'Compact inspect missing counts'
    Require ($null-eq $compact.result.serialized.proModeler.vertices) 'Compact inspect leaked mesh data'
    Require ($raw.result.serialized.proModeler.vertices.Count-gt 0) 'Raw inspection lost mesh data'
    Require (($compact|ConvertTo-Json -Depth 50 -Compress).Length-lt ($raw|ConvertTo-Json -Depth 100 -Compress).Length) 'Compact inspect is not smaller'
    $schema=Success (Invoke-Cli @('component','describe','ProModeler'))
    Require ($schema.properties.shape.enumNames.'7'-eq 'pyramid') 'Shape enum names missing'
    $null=Success (Invoke-Cli @('promodeler','set','Roof','--shape','wedge'))
    $null=Success (Invoke-Cli @('entity','transform','Roof','--x','1','--y','2','--z','3'))
    $null=Success (Invoke-Cli @('entity','set-parent','Roof','Parent'))
    $local=Success (Invoke-Cli @('entity','bounds','Roof','--world','false'))
    Require ([Math]::Abs($local.bounds.size[0]-8)-lt 0.001) 'Local render bounds incorrect'
    $list=Success (Invoke-Cli @('entity','list','--with-transform','--with-bounds'))
    Require (@($list.entities|Where-Object {$_.name-eq 'Roof'})[0].bounds.space-eq 'world') 'World bounds missing'
    $overlap=Success (Invoke-Cli @('entity','overlap','box','cone'))
    Require $overlap.overlap 'Coincident geometry did not overlap'
    $ray=Success (Invoke-Cli @('scene','raycast','--origin','0,8,0','--direction','0,-2,0'))
    Require ($ray.count-gt 0) 'AABB raycast missed origin geometry'
    $found=Success (Invoke-Cli @('entity','find','--component','ProModeler','--bounds-contains','0,0,0'))
    Require ($found.count-gt 0) 'Spatial component filter missed origin geometry'
    $before=Scene-Text
    foreach($view in @('top','front','iso')) {
        $null=Success (Invoke-Cli @('scene','capture','--file',"tools/$view.png",'--view',$view,'--projection','orthographic','--width','320','--height','240'))
        $bytes=[IO.File]::ReadAllBytes((Join-Path $projectDir "tools/$view.png"))
        Require ($bytes.Length-gt 200-and $bytes[0]-eq 137-and $bytes[1]-eq 80) 'Invalid PNG capture'
    }
    $null=Success (Invoke-Cli @('scene','capture','--file','tools/custom.png','--position','8,8,8','--target','0,2,0','--width','320','--height','240'))
    $null=Success (Invoke-Cli @('entity','frame','Roof','--capture','tools/roof.png','--view','iso','--width','320','--height','240'))
    Require ((Scene-Text)-eq $before) 'Capture mutated the edit scene'
    $null=Success (Invoke-Cli @('scene','save'))
    $ops=@(
        @{command='entity.create';arguments=@{name='Dry Child';primitive='cube'}},
        @{command='component.set';arguments=@{entity='Dry Child';component='MeshRenderer';properties=@{renderType='Unlit'}}},
        @{command='entity.delete';arguments=@{entity='cone';confirm=$true}}
    )|ConvertTo-Json -Depth 20 -Compress
    $before=Scene-Text
    $preview=Success (Invoke-Cli @('batch','run','--operations',$ops,'--dry-run','--summary'))
    Require ($preview.dryRun-and $preview.rolledBack-and $preview.count-eq 3) 'Dry run response incorrect'
    Require ((Scene-Text)-eq $before) 'Dry run mutated live scene'
    Require (!(Success (Invoke-Cli @('scene','inspect'))).dirty) 'Dry run dirtied a saved scene'
    $badPreviewOps='[{"command":"entity.create","arguments":{"name":"Preview Failure"}},{"command":"entity.transform","arguments":{"entity":"missing","x":1}}]'
    $failedPreview=Invoke-Cli @('batch','run','--operations',$badPreviewOps,'--dry-run','--summary')
    Require (!$failedPreview.success-and $failedPreview.result.dryRun-and $failedPreview.result.rolledBack-and $failedPreview.result.failedIndex-eq 1) 'Dry run failure metadata incorrect'
    Require ((Scene-Text)-eq $before-and !(Success (Invoke-Cli @('scene','inspect'))).dirty) 'Failed dry run changed the live scene or dirty state'
    # An unchanged Undo stack still undoes the prior parenting operation.
    $null=Success (Invoke-Cli @('editor','undo'))
    Require ((Success (Invoke-Cli @('entity','inspect','Roof'))).parentId-eq 0) 'Dry run changed Undo history'
    $null=Success (Invoke-Cli @('editor','redo'))
    Require ((Scene-Text)-eq $before) 'Redo failed after dry run'
    $badOps='[{"command":"entity.create","arguments":{"name":"Rolled Back","primitive":"cube"}},{"command":"entity.transform","arguments":{"entity":"missing","x":1}}]'
    $failed=Invoke-Cli @('batch','run','--operations',$badOps,'--summary')
    Require (!$failed.success-and $failed.result.rolledBack-and $failed.result.failedIndex-eq 1-and $failed.result.completedCount-eq 1) 'Batch rollback metadata incorrect'
    Require ((Scene-Text)-eq $before) 'Failed batch changed scene'
    $stdinOps='[{"command":"entity.create","arguments":{"name":"Stdin Box","primitive":"cube","render-type":"Unlit"}}]'
    $stdinResult=$stdinOps|& $CliExe --project $projectDir --instance $engineProcess.Id batch run --file - --summary --json|ConvertFrom-Json
    $stdin=Success $stdinResult
    Require ($stdin.count-eq 1-and $stdin.results[0].name-eq 'Stdin Box'-and $null-eq $stdin.results[0].transform) 'stdin/summary batch incorrect'
    $null=Success (Invoke-Cli @('mesh','set','Stdin Box','--render-type','Lit'))
    # Exercise authored UVs/normals and several face-material keys through load,
    # serialization and Play restoration in a real OpenGL editor context.
    $document=(Success (Invoke-Cli @('scene','get-json'))).scene
    $roofIndex=0
    for($i=0;$i-lt $document.entities.Count;$i++){if($document.entities[$i].name-eq 'Roof'){$roofIndex=$i;break}}
    $prefix="/entities/$roofIndex/proModeler"
    $patch=@(
        @{op='replace';path="$prefix/vertices/0/uv";value=@(0.12345,0.98765)},
        @{op='replace';path="$prefix/vertices/0/normal";value=@(0.123,0.456,0.789)},
        @{op='replace';path="$prefix/faceMaterials";value=@(@{faceId=4;material='assets/materials/Red.nmat'},@{faceId=1;material='assets/materials/Red.nmat'})}
    )|ConvertTo-Json -Depth 20 -Compress
    $null=Success (Invoke-Cli @('scene','patch','--patch',$patch,'--confirm'))
    $authored=Success (Invoke-Cli @('entity','inspect','Roof','--include-mesh-data'))
    Require ([Math]::Abs($authored.serialized.proModeler.vertices[0].uv[0]-0.12345)-lt 0.00001) 'Scene loading overwrote authored UVs'
    $outputPath=Join-Path $projectDir 'tools/projected.json'
    $stdout=& $CliExe --project $projectDir --instance $engineProcess.Id entity list --with-bounds --fields 'entities.id,entities.bounds' --output-format compact --out $outputPath
    Require (!$stdout-and $LASTEXITCODE-eq 0) '--out should suppress successful stdout'
    $projected=Get-Content -Raw $outputPath|ConvertFrom-Json
    Require ($projected.result.entities.Count-gt 1-and $null-eq $projected.result.entities[0].name) 'Output projection leaked unselected fields'
    $null=Success (Invoke-Cli @('scene','save'))
    $before=Scene-Text
    $null=Success (Invoke-Cli @('runtime','play'))
    $playing=$false
    for($i=0;$i-lt 100;$i++) {
        $state=Success (Invoke-Cli @('runtime','inspect'))
        if($state.playing){$playing=$true;break}
        if($state.state-eq 'failed'){throw $state.error}
        Start-Sleep -Milliseconds 300
    }
    Require $playing 'Play never became active'
    $null=Success (Invoke-Cli @('runtime','capture','tools/runtime.png','--position','8,8,8','--target','0,2,0','--width','320','--height','240'))
    $null=Success (Invoke-Cli @('runtime','stop'))
    Require ((Scene-Text)-eq $before) 'Play/Stop changed scene fingerprint'
    Require (!(Success (Invoke-Cli @('scene','inspect'))).dirty) 'Play/Stop dirtied a saved scene'
    $null=Success (Invoke-Cli @('scene','summary'))
    Write-Output "CLI workflow regression passed. Captures: $projectDir/tools"
} finally {
    if(!$engineProcess.HasExited) {
        $engineProcess.CloseMainWindow()|Out-Null
        if(!$engineProcess.WaitForExit(5000)){Stop-Process -Id $engineProcess.Id}
    }
}
