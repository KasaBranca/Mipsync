param([Parameter(Mandatory = $true)][string]$BiosPath,
      [Parameter(Mandatory = $true)][string]$WorkDir)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
$tool = Join-Path $PSScriptRoot '../tools/Patch-Ps1BootBios.ps1'
$originalHash = (Get-FileHash -LiteralPath $BiosPath -Algorithm SHA256).Hash
$output = Join-Path $WorkDir 'compatible.bin'
$result = & $tool -BiosPath $BiosPath -OutputPath $output
if ($result.PatchedMd5 -ne '70216b654f48dd56dcc702c941f6ece2') { throw 'Unexpected compatible BIOS hash' }
if ((Get-FileHash -LiteralPath $BiosPath -Algorithm SHA256).Hash -ne $originalHash) { throw 'Original BIOS was changed' }
function Reject([string]$source, [string]$destination) {
    $rejected = $false
    try { & $tool -BiosPath $source -OutputPath $destination | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw 'Unsafe patch request was accepted' }
}
Reject $BiosPath $output
Reject $BiosPath $BiosPath
$unknown = Join-Path $WorkDir 'unknown.bin'
[IO.File]::WriteAllBytes($unknown, [byte[]]@(1,2,3))
$badOutput = Join-Path $WorkDir 'must-not-exist.bin'
Reject $unknown $badOutput
if (Test-Path -LiteralPath $badOutput) { throw 'Unsupported BIOS produced output' }
Reject $output $badOutput
if (Test-Path -LiteralPath $badOutput) { throw 'Patched BIOS was patched again' }
if ((Get-FileHash -LiteralPath $output -Algorithm MD5).Hash.ToLowerInvariant() -ne $result.PatchedMd5) { throw 'Existing output was overwritten' }
Write-Output 'SCPH-1000 compatibility patch, original preservation and rejected-write checks passed.'
