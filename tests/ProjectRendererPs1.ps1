param(
    [Parameter(Mandatory=$true)][string]$EngineExe,
    [Parameter(Mandatory=$true)][string]$WorkDir
)
$ErrorActionPreference = 'Stop'
$project = Join-Path ([IO.Path]::GetFullPath($WorkDir)) 'renderer game'
New-Item -ItemType Directory -Force -Path "$project/scenes" | Out-Null
[IO.File]::WriteAllText("$project/nostalty.project", '{"name":"RendererPS1","defaultScene":"scenes/start.nscene","playerSettings":{"productName":"RendererPS1","scenesInBuild":["scenes/start.nscene","scenes/second.nscene"]}}')
foreach ($scene in @('start','second')) { [IO.File]::WriteAllText("$project/scenes/$scene.nscene", '{"entities":[]}') }
$output = & $EngineExe --export-ps1 $project 2>&1
if ($LASTEXITCODE -ne 0) { throw "Initial renderer export failed: $output" }
$renderer = "$project/renderer/ps1/runtime"
[IO.File]::AppendAllText("$renderer/render.c", "`n#pragma message(`"PROJECT_RENDERER_COMPILED`")`n")
[IO.File]::WriteAllText("$renderer/project_effect.c", "#pragma message(`"PROJECT_RENDERER_HELPER_COMPILED`")`nint project_effect(void) { return 42; }`n")
$output = & $EngineExe --build-ps1 $project 2>&1
if ($LASTEXITCODE -ne 0) { throw "Custom PS1 renderer compilation failed: $output" }
$product = "$project/Builds/PS1/RendererPS1"
$log = [IO.File]::ReadAllText("$product/build.log")
if (!$log.Contains('PROJECT_RENDERER_COMPILED') -or !$log.Contains('PROJECT_RENDERER_HELPER_COMPILED')) { throw 'PS1 compiler did not compile custom renderer and added helper' }
foreach ($scene in @('ps1_src','ps1_src/scenes/1')) {
    if (![IO.File]::ReadAllText("$product/$scene/runtime/render.c").Contains('PROJECT_RENDERER_COMPILED')) { throw 'Custom renderer missing from a build scene' }
}
foreach ($exe in @('PSX.EXE','SCN00001.EXE')) { if (!(Test-Path -LiteralPath "$product/out/$exe")) { throw "Compiled scene executable missing: $exe" } }
# A compilation failure must not succeed with a standard fallback executable.
[IO.File]::WriteAllText("$renderer/project_effect.c", "#error PROJECT_RENDERER_EXPECTED_FAILURE`n")
$output = & $EngineExe --build-ps1 $project 2>&1
if ($LASTEXITCODE -eq 0) { throw 'Invalid custom renderer succeeded via fallback' }
if (![IO.File]::ReadAllText("$product/build.log").Contains('PROJECT_RENDERER_EXPECTED_FAILURE')) { throw 'Additional C source was not compiled' }
Write-Output 'Custom PS1 renderer and added helper compiled for two scenes; compiler failure correctly rejected.'
