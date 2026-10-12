param(
    [Parameter(Mandatory = $true)][string]$EngineExe,
    [Parameter(Mandatory = $true)][string]$WorkDir
)
$ErrorActionPreference = 'Stop'
$project = Join-Path $WorkDir 'project'
New-Item -ItemType Directory -Force -Path (Join-Path $project 'scenes'), (Join-Path $project 'assets/scripts') | Out-Null
@'
class BootSceneCycle : MipsBehaviour {
    public int destination = 1;
    public float elapsed = 0.0;
    public void Update() {
        elapsed = elapsed + Time.deltaTime;
        if (elapsed > 0.3) Scene.LoadBuildIndex(destination);
    }
}
'@ | Set-Content -LiteralPath (Join-Path $project 'assets/scripts/BootSceneCycle.mips') -Encoding UTF8
for ($index = 0; $index -lt 2; $index++) {
    @{ entities = @(@{
        id = 1; name = "BootScene$index"
        mipsScripts = @(@{ path = 'assets/scripts/BootSceneCycle.mips'; fields = @{ destination = (1 - $index) } })
    }) } | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath (Join-Path $project "scenes/Scene$index.nscene") -Encoding UTF8
}
@{
    name = 'BootBrandCheck'; defaultScene = 'scenes/Scene0.nscene'
    playerSettings = @{ productName = 'BootBrandCheck'; scenesInBuild = @('scenes/Scene0.nscene', 'scenes/Scene1.nscene') }
} | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath (Join-Path $project 'nostalty.project') -Encoding UTF8
$output = & $EngineExe --build-ps1 $project 2>&1
if ($LASTEXITCODE -ne 0) { throw "Branding integration build failed: $output" }
$product = Join-Path $project 'Builds/PS1/BootBrandCheck'
$runtime = Join-Path $product 'ps1_src/runtime'
foreach ($file in @('boot_brand.c', 'boot_brand_image.h', 'boot_brand_policy.h')) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $file))) { throw "Missing application branding: $file" }
}
$standardArea = Join-Path $product 'ps1_src/licensej.dat'
if (-not (Test-Path -LiteralPath $standardArea)) { throw 'Missing standard NTSC-J system area' }
$sourceArea = Join-Path (Split-Path -Parent $EngineExe) 'templates/ps1/licensej.dat'
if ((Get-FileHash $sourceArea).Hash -ne (Get-FileHash $standardArea).Hash) {
    throw 'Build changed standard BIOS logo or region data'
}
if ((Get-Content (Join-Path $runtime 'boot_brand.c') -Raw).Contains('bios_logo_seen')) {
    throw 'Application branding still depends on BIOS GPU state'
}
foreach ($file in @('main.c', 'runtime/scene_loader.c', 'CMakeLists.txt')) {
    if ((Get-Content -LiteralPath (Join-Path $product "ps1_src/$file") -Raw) -notmatch 'boot_brand') {
        throw "Application branding is not connected in $file"
    }
}
foreach ($file in @('PSX.EXE', 'SCN00001.EXE', 'game.cue')) {
    if (-not (Test-Path -LiteralPath (Join-Path $product "out/$file"))) { throw "Missing compiled scene/disc: $file" }
}
$discVerifier = Join-Path $PSScriptRoot '../tools/Verify-Ps1Disc.cjs'
if (Get-Command node -ErrorAction SilentlyContinue) {
    & node $discVerifier (Join-Path $product 'out/game.cue') $sourceArea
    if ($LASTEXITCODE -ne 0) { throw 'Generated PS1 disc verification failed' }
}
Write-Output "Boot branding two-scene PS1 compilation passed: $product/out/game.cue"
