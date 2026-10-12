param(
    [Parameter(Mandatory = $true)][string]$EngineExe,
    [Parameter(Mandatory = $true)][string]$WorkDir
)
$ErrorActionPreference = 'Stop'
$project = Join-Path $WorkDir ('visual export '+[char]0x5BE9+[char]0x67FB+[char]0x7528)
New-Item -ItemType Directory -Force -Path "$project/scenes", "$project/assets" | Out-Null
Add-Type -AssemblyName System.Drawing
$bitmap = [Drawing.Bitmap]::new(320, 200)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([Drawing.Color]::FromArgb(255, 200, 100, 50))
$bitmap.Save("$project/assets/shared.png", [Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
Copy-Item "$project/assets/shared.png" "$project/assets/other.png"
@{
    name = 'VisualExport'; defaultScene = 'scenes/Visual.nscene'
    playerSettings = @{ productName = 'VisualExport'; scenesInBuild = @('scenes/Visual.nscene') }
} | ConvertTo-Json -Depth 10 | Set-Content "$project/nostalty.project" -Encoding utf8
$entities = @(
    @{ id = 1; name = 'Camera'; camera = @{ primary = $true; prerenderedBackground = 'assets/other.png' } },
    @{ id = 2; name = 'Light'; light = @{ type = 0; intensity = 1; color = @(1, 1, 1) } },
    @{ id = 3; name = 'Mesh'; meshRenderer = @{ primitive = 'Sphere'; texture = 'assets/shared.png' } },
    @{ id = 4; name = 'Canvas'; canvas = @{ referenceResolution = @(320, 240) } },
    @{ id = 5; name = 'Large UI'; parent = 4
       rectTransform = @{ sizeDelta = @(704, 448) }
       uiImage = @{ texture = 'assets/shared.png'; color = @(1, 1, 1, 1) } },
    @{ id = 6; name = 'Page UI'; parent = 4
       rectTransform = @{ sizeDelta = @(819.2, 640) }
       uiImage = @{ texture = 'assets/other.png'; color = @(1, 1, 1, 1) } },
    @{ id = 7; name = 'Light parent'; transform = @{ position = @(10, 3, 4); rotation = @(0, 90, 0) } },
    @{ id = 8; name = 'Point'; parent = 7; transform = @{ position = @(2, 1, 0) }
       light = @{ type = 1; range = 10; intensity = 2; color = @(1, 0.5, 0.25) } },
    @{ id = 9; name = 'Spot'; light = @{ type = 2; range = 6; spotAngle = 45; spotInnerAngle = 30 } },
    @{ id = 10; name = 'Disabled'; light = @{ type = 1; enabled = $false } },
    @{ id = 11; name = 'Inactive'; active = $false; light = @{ type = 1 } },
    @{ id = 90; name = 'Repeating floor'; meshRenderer = @{
       primitive = 'Plane'; texture = 'assets/shared.png'; tiling = @(4.91,2); offset = @(-0.25,0.1) } }
)
function Write-Scene($items) {
    @{ entities = @($items) } | ConvertTo-Json -Depth 20 |
        Set-Content "$project/scenes/Visual.nscene" -Encoding utf8
}
Write-Scene $entities
$output = & $EngineExe --export-ps1 $project 2>&1
if ($LASTEXITCODE -ne 0) { throw "Visual export failed: $output" }
$generated = "$project/Builds/PS1/VisualExport/ps1_src/generated"
$textureData = Get-Content -Raw "$generated/textures_data.c"
$rectangles = @()
foreach ($match in [regex]::Matches($textureData, '(?s)static const uint32_t (\w+)_tim\[\] = \{(.*?)\};')) {
    $words = @([regex]::Matches($match.Groups[2].Value, '0x([0-9a-f]+)') |
        ForEach-Object { [Convert]::ToUInt32($_.Groups[1].Value, 16) })
    $rect = [pscustomobject]@{
        name = $match.Groups[1].Value
        x = [int]($words[3] -band 65535); y = [int]($words[3] -shr 16)
        w = [int]($words[4] -band 65535); h = [int]($words[4] -shr 16)
    }
    if ($rect.x -lt 512 -or $rect.x + $rect.w -gt 1024 -or $rect.y + $rect.h -gt 512) {
        throw "Texture overlaps framebuffer or VRAM boundary: $($rect.name)"
    }
    if ($rect.name -match 'mipsyncui' -and
        (($rect.x % 64) -ne 0 -or ($rect.y % 256) + $rect.h -gt 256 -or $rect.w -gt 256)) {
        throw "UI texture exceeds its addressable page: $($rect.name)"
    }
    foreach ($other in $rectangles) {
        if ($rect.x -lt $other.x + $other.w -and $rect.x + $rect.w -gt $other.x -and
            $rect.y -lt $other.y + $other.h -and $rect.y + $rect.h -gt $other.y) {
            throw "VRAM overlap: $($rect.name), $($other.name)"
        }
    }
    $rectangles += $rect
}
$mesh = @($rectangles | Where-Object { $_.name -match 'assets_shared_png' -and $_.name -notmatch 'mipsyncui' })
$ui = @($rectangles | Where-Object { $_.name -match 'mipsyncui.*shared_png' })
$page = @($rectangles | Where-Object { $_.name -match 'mipsyncui.*other_png' })
if ($mesh.Count -ne 1 -or $mesh[0].w -ne 64 -or $ui.Count -ne 1 -or $ui[0].w -ne 220 -or
    $page.Count -ne 1 -or $page[0].w -ne 256) {
    throw 'UI display resolution or independent mesh/UI texture aliases regressed'
}
$sceneData = Get-Content -Raw "$generated/scene_data.c"
if ($sceneData -notmatch '\n\s*1, 13, 13, 20,\s*655360, 2621440,') {
    throw 'Default fog and background differ from the editor settings'
}
$floorRow = @($sceneData -split "`n" | Where-Object { $_.Contains('"Repeating floor"') })
if ($floorRow.Count -ne 1 -or $floorRow[0] -notmatch '1257, 512, -64, 26,' -or
    $textureData.Contains('mipsynctile8')) {
    throw 'Ordinary floor repeats lost full texture resolution or material tiling/offset'
}
if ($sceneData -notmatch '\{ 1, 0, 0, 4096, 255, 255, 255, 5, 5, 5 \}') {
    throw 'Directional light export differs from editor ambient/diffuse parameters'
}
$lightBlock = [regex]::Match($sceneData, '(?s)k_ps1_scene_lights\[\] = \{(.*?)\};').Groups[1].Value
$lightRows = @($lightBlock.Trim() -split "`n")
if ($lightRows.Count -ne 3 -or $sceneData -notmatch 'k_ps1_scene_lights,\s*3u') {
    throw 'Enabled directional/point/spot lights were not all exported, or inactive lights leaked'
}
$pointValues = @([regex]::Matches($lightRows[1], '-?\d+') | ForEach-Object { [int]$_.Value })
if ($pointValues[1] -ne 1 -or [Math]::Abs($pointValues[2] - 655360) -gt 1 -or
    [Math]::Abs($pointValues[3] - 262144) -gt 1 -or [Math]::Abs($pointValues[4] - 131072) -gt 1 -or
    $pointValues[8] -ne 8192 -or $pointValues[9] -ne 4096 -or $pointValues[10] -ne 2048 -or
    $pointValues[11] -ne 655360) {
    throw "Point light world transform, HDR intensity, colour or range was lost: $($lightRows[1])"
}
$spotValues = @([regex]::Matches($lightRows[2], '-?\d+') | ForEach-Object { [int]$_.Value })
if ($spotValues[1] -ne 2 -or $spotValues[12] -ne 2896 -or $spotValues[13] -ne 3547) {
    throw 'Spot cone cosines differ from editor'
}
for ($i = 0; $i -lt 12; ++$i) {
    $entities += @{ id = 20 + $i; name = "Extra light$i"; light = @{ type = 1 } }
}
Write-Scene $entities
$output = & $EngineExe --export-ps1 $project 2>&1
if ($LASTEXITCODE -ne 0) { throw "Multiple light export failed: $output" }
$sceneData = Get-Content -Raw "$generated/scene_data.c"
$lightBlock = [regex]::Match($sceneData, '(?s)k_ps1_scene_lights\[\] = \{(.*?)\};').Groups[1].Value
if (@($lightBlock.Trim() -split "`n").Count -ne 8 -or $sceneData -notmatch 'k_ps1_scene_lights,\s*8u') {
    throw 'PS1 must retain the same first eight enabled lights as the editor'
}

# Imported cards may use negative repeating UVs. Keep their texture detail,
# both sides, and cutout alpha instead of clamping them to the texture edge.
@'
v -1 0 0
v 1 0 0
v 1 2 0
v -1 2 0
vt -0.8 0
vt -0.2 0
vt -0.2 1
vt -0.8 1
vn 0 0 1
f 1/1/1 2/2/1 3/3/1
f 1/1/1 3/3/1 4/4/1
'@ | Set-Content "$project/assets/card.obj" -Encoding ascii
$card = [Drawing.Bitmap]::new(32,32)
$paint = [Drawing.Graphics]::FromImage($card)
$paint.Clear([Drawing.Color]::Transparent)
$paint.FillRectangle([Drawing.Brushes]::Green, 8,8,16,16)
$card.Save("$project/assets/cutout.png", [Drawing.Imaging.ImageFormat]::Png)
$paint.Dispose(); $card.Dispose()
$entities += @{id=70; name='Negative UV card'; meshRenderer=@{
    primitive='File'; mesh='assets/card.obj'; texture='assets/cutout.png'} }
$entities += @{id=71; name='Grading'; postProcessVolume=@{
    colorGradingEnabled=$true; exposure=-0.202; contrast=1.576; saturation=0.7;
    colorFilter=@(0.8,0.9,1); priority=2} }
Write-Scene $entities
$output = & $EngineExe --export-ps1 $project 2>&1
if ($LASTEXITCODE -ne 0) { throw "Card/grading export failed: $output" }
$meshData = Get-Content -Raw "$generated/mesh_data.c"
$uvBlock = [regex]::Match($meshData, '(?s)k_ps1_mesh_1_uvs\[\] = \{(.*?)\};').Groups[1].Value
$uvPairs = [regex]::Matches($uvBlock, '\{\s*(\d+),\s*(\d+)\s*\}')
if ($uvPairs.Count -lt 4 -or @($uvPairs | Where-Object {
    [int]$_.Groups[1].Value -lt 5 -or [int]$_.Groups[1].Value -gt 26 }).Count -or
    @($uvPairs | Where-Object { [int]$_.Groups[2].Value -eq 31 }).Count -eq 0) {
    throw "UV repeats or the 1.0 endpoint collapsed to the texture edge: $uvBlock"
}
$triBlock = [regex]::Match($meshData, '(?s)k_ps1_mesh_1_tris\[\] = \{(.*?)\};').Groups[1].Value
if ([regex]::Matches($triBlock,'\{').Count -ne 4) { throw 'Single imported card lost its back faces' }
$textureData = Get-Content -Raw "$generated/textures_data.c"
if ($textureData -notmatch 'cutout_png_\d+_tim\[0\]\),\s*0,\s*1\s*\}') { throw 'Cutout metadata missing' }
$sceneData = Get-Content -Raw "$generated/scene_data.c"
$lut = [regex]::Match($sceneData, '(?s)k_ps1_color_grading_lut\[32768\] = \{(.*?)\};').Groups[1].Value
if ([regex]::Matches($lut,'\d+u').Count -ne 32768 -or $sceneData -notmatch 'k_ps1_color_grading_lut\s*\n\};') {
    throw 'Authored final-frame color grading LUT was not exported/bound'
}
# Disabled and inactive higher-priority volumes must not shadow the active one.
$entities += @{id=72; name='Inactive grading'; active=$false; postProcessVolume=@{priority=10; colorGradingEnabled=$false} }
Write-Scene $entities
$output = & $EngineExe --export-ps1 $project 2>&1
if ($LASTEXITCODE -ne 0 -or (Get-Content -Raw "$generated/scene_data.c") -notmatch 'k_ps1_color_grading_lut\[32768\]') {
    throw 'Inactive volume shadowed active grading'
}
# Neutral enabled grading must not generate a 64 KiB LUT or serialize GPU rendering.
$volume=$entities | Where-Object {$_.id -eq 71} | Select-Object -First 1
if (!$volume -or !$volume.postProcessVolume) {throw 'Grading test volume missing'}
$volume.postProcessVolume.exposure=0
$volume.postProcessVolume.contrast=1
$volume.postProcessVolume.saturation=1
$volume.postProcessVolume.colorFilter=@(1,1,1)
Write-Scene $entities
$output=& $EngineExe --export-ps1 $project 2>&1
if ($LASTEXITCODE -ne 0 -or (Get-Content -Raw "$generated/scene_data.c") -match 'k_ps1_color_grading_lut') {
    throw 'Neutral grading retained expensive framebuffer pass'
}

# A crowded atlas must fail explicitly rather than silently corrupt other
# textures/framebuffers. Distinct paths prevent normal texture deduplication.
for ($i = 0; $i -lt 20; ++$i) {
    Copy-Item "$project/assets/shared.png" "$project/assets/overflow$i.png"
    $entities += @{ id = 100 + $i; name = "Overflow$i"; parent = 4
        rectTransform = @{ sizeDelta = @(819.2, 640) }
        uiImage = @{ texture = "assets/overflow$i.png" } }
}
Write-Scene $entities
$output = & $EngineExe --export-ps1 $project 2>&1
if ($LASTEXITCODE -eq 0 -or ($output -join "`n") -notmatch 'texture VRAM is full') {
    throw "Atlas exhaustion was not diagnosed: $output"
}
Write-Output 'PS1 UI resolution, VRAM packing and light export regression passed.'
