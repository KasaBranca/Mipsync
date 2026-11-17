$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$file = Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/release_windows.ps1'
$ast = [Management.Automation.Language.Parser]::ParseFile($file, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($name in @('Test-IsSupportedVersion', 'Get-ReleaseSortKey', 'Get-HubInstaller')) {
  $function = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
  . ([scriptblock]::Create($function.Extent.Text))
}
$versions = @('v2026.10.18', 'v1.0.0f2', 'v1.0.0b10', 'v1.0.0f10', 'v1.0.0a10', 'v1.0.0f1')
$actual = @($versions | Sort-Object { Get-ReleaseSortKey $_ }) -join ','
$expected = 'v2026.10.18,v1.0.0a10,v1.0.0b10,v1.0.0f1,v1.0.0f2,v1.0.0f10'
if ($actual -ne $expected) { throw "Unexpected release ordering: $actual" }
if (!(Test-IsSupportedVersion 'v1.0.0f1') -or !(Test-IsSupportedVersion 'v2026.10.18') -or (Test-IsSupportedVersion 'v0.1.16')) { throw 'Release history filter failed' }
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('mipsync-installer-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
  $oldInstaller = Join-Path $fixture 'Mipsync Hub_0.1.16_x64-setup.exe'
  [IO.File]::WriteAllText($oldInstaller, 'old')
  $rejected = $false
  try { Get-HubInstaller $fixture '1.0.0+f.1' | Out-Null }
  catch { $rejected = $true }
  if (!$rejected) { throw 'An older installer was accepted for a missing release' }
  $expectedInstaller = Join-Path $fixture 'Mipsync Hub_1.0.0+f.1_x64-setup.exe'
  [IO.File]::WriteAllText($expectedInstaller, 'current')
  if ((Get-HubInstaller $fixture '1.0.0+f.1') -ne $expectedInstaller) {
    throw 'The exact release installer was not selected'
  }
} finally {
  # Delete only the two files created by this test, then the empty directory.
  [IO.File]::Delete($oldInstaller)
  [IO.File]::Delete((Join-Path $fixture 'Mipsync Hub_1.0.0+f.1_x64-setup.exe'))
  [IO.Directory]::Delete($fixture)
}
Write-Output 'Release script parsing, version ordering and installer packaging checks passed.'
