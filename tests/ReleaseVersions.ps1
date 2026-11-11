$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$file = Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/release_windows.ps1'
$ast = [Management.Automation.Language.Parser]::ParseFile($file, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($name in @('Test-IsSupportedVersion', 'Get-ReleaseSortKey')) {
  $function = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
  . ([scriptblock]::Create($function.Extent.Text))
}
$versions = @('v2026.10.18', 'v1.0.0f2', 'v1.0.0b10', 'v1.0.0f10', 'v1.0.0a10', 'v1.0.0f1')
$actual = @($versions | Sort-Object { Get-ReleaseSortKey $_ }) -join ','
$expected = 'v2026.10.18,v1.0.0a10,v1.0.0b10,v1.0.0f1,v1.0.0f2,v1.0.0f10'
if ($actual -ne $expected) { throw "Unexpected release ordering: $actual" }
if (!(Test-IsSupportedVersion 'v1.0.0f1') -or !(Test-IsSupportedVersion 'v2026.10.18') -or (Test-IsSupportedVersion 'v0.1.16')) { throw 'Release history filter failed' }
Write-Output 'Release script parsing and version ordering checks passed.'
