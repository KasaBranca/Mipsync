param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$repo = [IO.Path]::GetFullPath($RepoRoot)
$source = Join-Path $repo "tools\vscode-mips"
$server = Join-Path $repo "tools\mips-language-server"
if (-not (Test-Path -LiteralPath (Join-Path $source "package.json") -PathType Leaf)) {
    throw "VS Code extension source was not found: $source"
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repo "build\vsix"
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

$package = Get-Content -LiteralPath (Join-Path $source "package.json") -Raw | ConvertFrom-Json
$version = [string]$package.version
$name = [string]$package.name
$publisher = [string]$package.publisher
$displayName = [Security.SecurityElement]::Escape([string]$package.displayName)
$description = [Security.SecurityElement]::Escape([string]$package.description)
$stage = Join-Path $outputRoot ("mips-vsix-stage-" + [Guid]::NewGuid().ToString("N"))
$extensionStage = Join-Path $stage "extension"
$output = Join-Path $outputRoot ("$name-$version.vsix")

try {
    New-Item -ItemType Directory -Path $extensionStage -Force | Out-Null
    Get-ChildItem -LiteralPath $source -Force | Where-Object {
        $_.Name -notin @("node_modules", ".git", ".vsix", ".vscodeignore") -and
        $_.Extension -ne ".vsix"
    } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $extensionStage -Recurse -Force
    }

    if (Test-Path -LiteralPath $server -PathType Container) {
        $serverDestination = Join-Path $extensionStage "mips-language-server"
        New-Item -ItemType Directory -Path $serverDestination -Force | Out-Null
        Copy-Item -Path (Join-Path $server "*") -Destination $serverDestination -Recurse -Force
    }

    $manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="$name" Version="$version" Publisher="$publisher" />
    <DisplayName>$displayName</DisplayName>
    <Description xml:space="preserve">$description</Description>
    <Tags>mipssharp,Mips,MipsSharp,MipsyncMips,mips,__ext_mips</Tags>
    <Categories>Programming Languages</Categories>
    <GalleryFlags>Public</GalleryFlags>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="^1.88.0" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
      <Property Id="Microsoft.VisualStudio.Code.LocalizedLanguages" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.EnabledApiProposals" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExecutesCode" Value="true" />
      <Property Id="Microsoft.VisualStudio.Services.GitHubFlavoredMarkdown" Value="true" />
      <Property Id="Microsoft.VisualStudio.Services.Content.Pricing" Value="Free" />
    </Properties>
  </Metadata>
  <Installation><InstallationTarget Id="Microsoft.VisualStudio.Code" /></Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
  </Assets>
</PackageManifest>
"@
    $contentTypes = @"
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension=".js" ContentType="application/javascript"/><Default Extension=".json" ContentType="application/json"/><Default Extension=".md" ContentType="text/markdown"/><Default Extension=".vsixmanifest" ContentType="text/xml"/></Types>
"@
    Set-Content -LiteralPath (Join-Path $stage "extension.vsixmanifest") -Value $manifest -Encoding utf8NoBOM
    Set-Content -LiteralPath (Join-Path $stage "[Content_Types].xml") -Value $contentTypes -Encoding utf8NoBOM

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $output -PathType Leaf) {
        Remove-Item -LiteralPath $output -Force
    }
    [IO.Compression.ZipFile]::CreateFromDirectory(
        $stage, $output, [IO.Compression.CompressionLevel]::Optimal, $false)
    Write-Output $output
}
finally {
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    if ($resolvedStage.StartsWith($outputRoot + [IO.Path]::DirectorySeparatorChar,
                                  [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedStage)) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
