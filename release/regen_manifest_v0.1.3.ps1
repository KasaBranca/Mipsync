Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = "d:\Nostalty"
$outDir = Join-Path $root "release"
$Version = "0.1.3"
$tag = "v$Version"

$engineAsset = "MipsyncEngine_Windows_x64.zip"
$hubAsset = "MipsyncHub_Windows_x64.zip"
$repository = "KasaBranca/Mipsync"
$downloadBase = "https://github.com/$repository/releases/download/$tag"

$existing = $null
try {
  $existing = (Invoke-WebRequest -UseBasicParsing -Uri "https://github.com/$repository/releases/latest/download/manifest.json" -TimeoutSec 10).Content | ConvertFrom-Json
} catch {
  $existing = $null
}

$prevReleases = @()
try {
  if ($existing -and $existing.editor -and $existing.editor.releases) {
    $prevReleases = @($existing.editor.releases)
  }
} catch {
  $prevReleases = @()
}

$newEntry = [PSCustomObject]@{
  version = $tag
  title = "Mipsync Engine $Version"
  downloadUrl = "$downloadBase/$engineAsset"
  publishedAt = (Get-Date).ToString("o")
}

$manifest = [PSCustomObject]@{
  schemaVersion = 1
  generatedAt = (Get-Date).ToString("o")
  hub = [PSCustomObject]@{
    version = $tag
    assetName = $hubAsset
    downloadUrl = "$downloadBase/$hubAsset"
    releaseNotesUrl = "https://github.com/$repository/releases/tag/$tag"
  }
  editor = [PSCustomObject]@{
    assetName = $engineAsset
    releases = $prevReleases
  }
}

$current = @($manifest.editor.releases | Where-Object { $_.version -ne $tag })
$manifest.editor.releases = @($current + @($newEntry) | Sort-Object version -Descending)

$manifestPath = Join-Path $outDir "manifest.json"
$manifestJson = $manifest | ConvertTo-Json -Depth 6
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($manifestPath, $manifestJson, $utf8NoBom)

Write-Host "Wrote $manifestPath"
