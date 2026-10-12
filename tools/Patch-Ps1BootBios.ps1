# Create a local compatibility copy of a user-provided SCPH-1000 BIOS.
# No BIOS image is bundled with this tool.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BiosPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$sourcePath = [IO.Path]::GetFullPath($BiosPath)
$destinationPath = [IO.Path]::GetFullPath($OutputPath)
if ([String]::Equals($sourcePath, $destinationPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputPath must differ from the original BIOS path.'
}
if (Test-Path -LiteralPath $destinationPath) { throw 'OutputPath already exists; it will not be overwritten.' }
$bytes = [IO.File]::ReadAllBytes($sourcePath)
$md5 = [Security.Cryptography.MD5]::Create()
try { $originalHash = ([BitConverter]::ToString($md5.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
finally { $md5.Dispose() }
if ($bytes.Length -ne 524288 -or $originalHash -ne '239665b1a3dade1b5a52c06338011044') {
    throw 'Supported BIOS: original SCPH-1000 NTSC-J v1.0 (MD5 239665b1a3dade1b5a52c06338011044). No output was written.'
}
# The uncompressed shell is relocated from ROM 0x18000 to RAM 0x80030000.
# Keep both boot animations and all normal CD/GPU initialization intact.
$patches = @(
    @{ Offset = 0x187d8; Before = [uint32]0x14400008; After = [uint32]0;
       Purpose = 'Allow the disc region string to differ at RAM 0x800307d8' },
    @{ Offset = 0x26644; Before = [uint32]0x1062000b; After = [uint32]0x1000000b;
       Purpose = 'Accept custom disc TMD bytes at RAM 0x8003e644; keep pointer increments and loop' }
)
foreach ($patch in $patches) {
    if ([BitConverter]::ToUInt32($bytes, $patch.Offset) -ne $patch.Before) { throw 'Unexpected BIOS instruction; no output was written.' }
}
foreach ($patch in $patches) {
    [Array]::Copy([BitConverter]::GetBytes($patch.After), 0, $bytes, $patch.Offset, 4)
}
# CreateNew prevents races from overwriting any existing BIOS or destination.
$output = [IO.File]::Open($destinationPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try { $output.Write($bytes, 0, $bytes.Length) }
finally { $output.Dispose() }
[PSCustomObject]@{
    OriginalBios = $sourcePath
    CompatibleBios = $destinationPath
    OriginalMd5 = $originalHash
    PatchedMd5 = (Get-FileHash -LiteralPath $destinationPath -Algorithm MD5).Hash.ToLowerInvariant()
    PatchedInstructions = $patches.Count
}
