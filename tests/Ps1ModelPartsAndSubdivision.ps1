param([Parameter(Mandatory=$true)][string]$EngineExe,
      [Parameter(Mandatory=$true)][string]$WorkDir)
$ErrorActionPreference='Stop'
$project=Join-Path $WorkDir 'model-parts-and-floor'
New-Item -ItemType Directory -Force -Path "$project/scenes","$project/assets" | Out-Null
@{name='ModelPartsFloor';defaultScene='scenes/default.nscene'} |
    ConvertTo-Json | Set-Content "$project/nostalty.project" -Encoding utf8
@'
o Lower
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
o Upper
v 0 4 0
v 1 4 0
v 0 5 0
f 4 5 6
'@ | Set-Content "$project/assets/parts.obj" -Encoding utf8
@{entities=@(
    @{id=1;name='Camera';camera=@{primary=$true}},
    @{id=2;name='Floor';transform=@{scale=@(10,1,10)};
      meshRenderer=@{primitive='Plane';size=10;typePreset='Floor';renderType='Unlit'};
      meshSubdivider=@{enabled=$true;maxLevels=1;maxEdgeLength=1}},
    @{id=3;name='Model root'},
    @{id=4;name='Lower';parent=3;transform=@{position=@(0,0.1,0)};
      meshRenderer=@{primitive='File';mesh='assets/parts.obj';meshPart=0}},
    @{id=5;name='Upper';parent=3;transform=@{position=@(0,0.9,0)};
      meshRenderer=@{primitive='File';mesh='assets/parts.obj';meshPart=1}}
)} | ConvertTo-Json -Depth 15 | Set-Content "$project/scenes/default.nscene" -Encoding utf8
$output=& $EngineExe --export-ps1 $project 2>&1
if($LASTEXITCODE) {throw "Export failed: $output"}
if(($output -join "`n") -notmatch 'Mesh Subdivider .*200 -> 800 triangles') {
    throw "PS1 subdivision no longer matches the editor's 200-face base plane: $output"
}
$generated=Join-Path $project 'Builds/PS1/ModelPartsFloor/ps1_src/generated'
$scene=Get-Content -Raw "$generated/scene_data.c"
$mesh=Get-Content -Raw "$generated/mesh_data.c"
function Mesh-Index([string]$name) {
    $row=($scene -split "`n" | Where-Object { $_.Contains('"'+$name+'"') })
    $match=[regex]::Match($row,'\},\s*4,\s*(\d+)u,')
    if(!$match.Success) {throw "No custom mesh exported for $name"}
    return [int]$match.Groups[1].Value-1
}
$lower=Mesh-Index 'Lower';$upper=Mesh-Index 'Upper';$floor=Mesh-Index 'Floor'
if($lower -eq $upper) {throw 'Different imported mesh parts share a combined PS1 mesh'}
foreach($index in @($lower,$upper)) {
    $block=[regex]::Match($mesh,"(?s)static const ps1_mesh_tri k_ps1_mesh_${index}_tris\[\] = \{(.*?)\};").Groups[1].Value
    $count=[regex]::Matches($block,'(?m)^\s*\{').Count
    if($count -lt 1 -or $count -gt 2) {throw "Child mesh $index contains the other part's geometry ($count triangles)"}
}
$block=[regex]::Match($mesh,"(?s)static const ps1_mesh_tri k_ps1_mesh_${floor}_tris\[\] = \{(.*?)\};").Groups[1].Value
if([regex]::Matches($block,'(?m)^\s*\{').Count -lt 800) {throw 'Subdivision density was discarded later in PS1 export'}
Write-Output 'PS1 model parts stay separate, and the floor preserves the editor base grid and subdivision density.'
