param(
    [ValidateSet('All','Configure','Build','Test')][string]$Step = 'All',
    [ValidateSet('default','debug')][string]$Preset = 'default',
    [string]$WorkRoot = (Join-Path ([IO.Path]::GetTempPath()) 'MipsyncSourceBuilds'),
    [string]$CompilerRoot = '',
    [ValidateRange(1,64)][int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
# Resolve tools before configuring so missing prerequisites produce an actionable
# error instead of CMake's cascading "compiler not set" diagnostics.
$cmakeCommand = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue
if (!$cmakeCommand) { throw 'CMake 3.25 or newer is required. Install CMake and add its bin directory to PATH.' }
$cmake = $cmakeCommand.Source
$ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
if (!(Test-Path -LiteralPath $ctest)) { throw "ctest.exe is missing beside CMake: $cmake" }
$compilerCandidates = @()
if ($CompilerRoot) {
    $compilerCandidates = @([IO.Path]::GetFullPath($CompilerRoot))
} else {
    $gccCommand = Get-Command gcc -CommandType Application -ErrorAction SilentlyContinue
    if ($gccCommand) { $compilerCandidates += Split-Path $gccCommand.Source -Parent }
    $compilerCandidates += @('C:\msys64\mingw64\bin', 'C:\msys64\ucrt64\bin')
}
$compilerBin = $null
foreach ($candidate in $compilerCandidates) {
    if ((Test-Path -LiteralPath (Join-Path $candidate 'gcc.exe')) -and
        (Test-Path -LiteralPath (Join-Path $candidate 'g++.exe'))) {
        $compilerBin = $candidate
        break
    }
    if ((Test-Path -LiteralPath (Join-Path $candidate 'bin/gcc.exe')) -and
        (Test-Path -LiteralPath (Join-Path $candidate 'bin/g++.exe'))) {
        $compilerBin = Join-Path $candidate 'bin'
        break
    }
}
if (!$compilerBin) {
    throw 'MinGW-w64 GCC/G++ with C++20 support is required. Install the MSYS2 MinGW-w64 toolchain, add its bin directory to PATH, or pass -CompilerRoot C:\Tools\mingw64\bin.'
}
$WorkRoot = [IO.Path]::GetFullPath($WorkRoot)
if ($WorkRoot -match '[^\x20-\x7e]' -or $WorkRoot.Length -gt 100) {
    throw 'Use -WorkRoot with a short ASCII path (for example C:\MipsyncBuilds).'
}
$hash = [Security.Cryptography.SHA256]::Create()
try { $id = [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($source.ToLowerInvariant()))).Replace('-','').Substring(0,16) }
finally { $hash.Dispose() }
$alias = Join-Path $WorkRoot $id
New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
if (Test-Path -LiteralPath $alias) {
    $link = Get-Item -LiteralPath $alias -Force
    if ($link.LinkType -ne 'Junction' -or [IO.Path]::GetFullPath([string]$link.Target).TrimEnd('\') -ne $source.TrimEnd('\')) {
        throw "Build alias already belongs to another directory: $alias"
    }
} else {
    New-Item -ItemType Junction -Path $alias -Target $source | Out-Null
}
$ninja = Join-Path $alias 'tools/ninja.exe'
if (!(Test-Path -LiteralPath $ninja)) {
    $ninjaCommand = Get-Command ninja -CommandType Application -ErrorAction SilentlyContinue
    if (!$ninjaCommand) { throw 'Ninja is missing. Restore the bundled tools/ninja.exe or install Ninja and add it to PATH.' }
    $ninja = $ninjaCommand.Source
}
$binary = Join-Path $alias $(if ($Preset -eq 'debug') {'build-debug'} else {'build'})
# A build cache embeds its source path and generator. Preserve incompatible
# caches rather than trying to reuse them or recursively deleting user files.
if (($Step -eq 'All' -or $Step -eq 'Configure') -and (Test-Path -LiteralPath "$binary\CMakeCache.txt")) {
    $cache = Get-Content -LiteralPath "$binary\CMakeCache.txt" -Raw -Encoding UTF8
    $expected = $alias.Replace('\','/')
    if ($cache -notmatch 'CMAKE_GENERATOR:INTERNAL=Ninja\r?\n' -or
        $cache -notmatch ('CMAKE_HOME_DIRECTORY:INTERNAL='+[regex]::Escape($expected)+'\r?\n')) {
        $backup = $binary + '.previous-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff')
        foreach ($path in @($binary, $backup)) {
            if (![IO.Path]::GetFullPath($path).StartsWith($alias + '\', [StringComparison]::OrdinalIgnoreCase)) {
                throw "Build directory is outside the source alias: $path"
            }
        }
        Move-Item -LiteralPath $binary -Destination $backup
        Write-Host "Previous build preserved: $backup"
    }
}
Push-Location -LiteralPath $alias
$previousPath = $env:PATH
try {
    # GCC also needs its companion executables and runtime DLLs on PATH.
    $env:PATH = $compilerBin + ';' + (Split-Path $ninja -Parent) + ';' + $previousPath
    Write-Host "Ninja: $ninja"
    Write-Host "Compiler: $compilerBin"
    if ($Step -eq 'All' -or $Step -eq 'Configure') {
        & $cmake --preset $Preset -DMIPSYNC_DEPLOY_LATEST_INSTALL=OFF `
            "-DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja" `
            "-DCMAKE_C_COMPILER:FILEPATH=$(Join-Path $compilerBin 'gcc.exe')" `
            "-DCMAKE_CXX_COMPILER:FILEPATH=$(Join-Path $compilerBin 'g++.exe')"
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }
    }
    if ($Step -eq 'All' -or $Step -eq 'Build') {
        & $cmake --build --preset $Preset --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw "Engine build failed ($LASTEXITCODE)." }
    }
    if ($Step -eq 'All' -or $Step -eq 'Test') {
        & $ctest --test-dir $binary --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Engine tests failed ($LASTEXITCODE)." }
    }
    Write-Host "Build output: $source\$(Split-Path $binary -Leaf)\src"
} finally {
    $env:PATH = $previousPath
    Pop-Location
}
