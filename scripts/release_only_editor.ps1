param(
  [Parameter(Mandatory = $true)]
  [string]$Version
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$tag = "v$Version"
$repository = "KasaBranca/Mipsync"
$root = (Resolve-Path "$PSScriptRoot/..").Path
$outDir = Join-Path $root "release"
$engineZip = Join-Path $outDir "MipsyncEngine_Windows_x64.zip"

if (!(Test-Path $engineZip)) {
  throw "Engine zip not found: $engineZip. Please run release_editor_windows.ps1 first."
}

# Fetch existing manifest.json
$manifestUrl = "https://github.com/$repository/releases/latest/download/manifest.json"
Write-Host "Fetching latest manifest from: $manifestUrl"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$existingContent = (New-Object System.Net.WebClient).DownloadString($manifestUrl)
$existing = $existingContent | ConvertFrom-Json

# Construct new manifest
$newEntry = [PSCustomObject]@{
  version = $tag
  title = "Mipsync Engine $Version"
  downloadUrl = "https://github.com/$repository/releases/download/$tag/MipsyncEngine_Windows_x64.zip"
  publishedAt = (Get-Date).ToString("o")
}

# Filter out if version already exists, then prepend new entry
$prevReleases = @()
if ($null -ne $existing.editor -and $null -ne $existing.editor.releases) {
  $prevReleases = @($existing.editor.releases | Where-Object { $_.version -ne $tag -and $_.version -ne "v$tag" })
}
$allReleases = @($newEntry) + $prevReleases

$manifest = [PSCustomObject]@{
  schemaVersion = 1
  generatedAt = (Get-Date).ToString("o")
  hub = $existing.hub  # Keep Hub version/URL exactly as it was
  editor = [PSCustomObject]@{
    assetName = "MipsyncEngine_Windows_x64.zip"
    releases = $allReleases
  }
}

$manifestPath = Join-Path $outDir "manifest.json"
$manifestJson = $manifest | ConvertTo-Json -Depth 6
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($manifestPath, $manifestJson, $utf8NoBom)

Write-Host "Generated new manifest.json at $manifestPath"

# Construct Release Notes
$notes = @"
## Mipsync $Version

Editor-only update (Windows x64).

### Highlights (v$Version)
- Includes the dedicated Mips# VS Code/Cursor language extension and bundled language server.
- Keeps editor-only packaging aligned with the installed engine layout.
- Hub remains unchanged.

### Assets
- MipsyncEngine_Windows_x64.zip (engine + ``ps1_runtime/`` + ``templates/ps1/`` + editor tools)
- manifest.json (editor feed update; Hub app version remains unchanged)
"@

$notesFile = Join-Path $outDir "RELEASE_NOTES_$Version.md"
$notes | Out-File -FilePath $notesFile -Encoding utf8

Write-Host "Creating GitHub Release $tag..."
gh release create "$tag" -R $repository `
  --title "Mipsync $Version" `
  --notes-file $notesFile `
  $engineZip $manifestPath

Write-Host "Release created successfully!"
