param(
    [string]$EnginePath,
    [string]$SdkRoot = $env:PSN00BSDK,
    [string]$EmulatorPath,
    [string]$WorkRoot
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $EnginePath) { $EnginePath = Join-Path $repo 'build/src/MipsyncEngine.exe' }
$EnginePath = (Resolve-Path -LiteralPath $EnginePath).Path
if (-not $SdkRoot) { throw 'Pass -SdkRoot or set PSN00BSDK to the PSn00bSDK installation.' }
$SdkRoot = (Resolve-Path -LiteralPath $SdkRoot).Path
if (-not $WorkRoot) { $WorkRoot = Join-Path $repo ('tmp/native-pipeline-' + [guid]::NewGuid().ToString('N')) }
if (Test-Path -LiteralPath $WorkRoot) { throw 'WorkRoot must be a new directory; existing data is never removed.' }
New-Item -ItemType Directory -Path $WorkRoot | Out-Null
$WorkRoot = (Resolve-Path -LiteralPath $WorkRoot).Path
$fixture = Join-Path $PSScriptRoot 'fixtures/ps1-native'
$project = Join-Path $WorkRoot 'project'
New-Item -ItemType Directory -Path $project | Out-Null
foreach ($name in @('assets', 'scenes', 'nostalty.project')) {
    Copy-Item -LiteralPath (Join-Path $fixture $name) -Destination $project -Recurse
}
$previousSdk = $env:PSN00BSDK
$previousPath = $env:PATH
try {
    $env:PSN00BSDK = $SdkRoot
    $env:PATH = (Join-Path $SdkRoot 'bin') + ';' + $env:PATH
    & $EnginePath --export-ps1 $project *> (Join-Path $WorkRoot 'export.log')
    if ($LASTEXITCODE -ne 0) { throw "Native scene export failed; see $WorkRoot/export.log" }
    $source = Join-Path $project 'Builds/PS1/NativeAotQA/ps1_src'
    $build = Join-Path $WorkRoot 'mips-build'
    $toolchain = Join-Path $SdkRoot 'lib/libpsn00b/cmake/sdk.cmake'
    $ninja = Join-Path $repo 'tools/ninja.exe'
    & cmake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$toolchain" *> (Join-Path $WorkRoot 'configure.log')
    if ($LASTEXITCODE -ne 0) { throw "PS1 configure failed; see $WorkRoot/configure.log" }
    & cmake --build $build --parallel 2 *> (Join-Path $WorkRoot 'build.log')
    if ($LASTEXITCODE -ne 0) { throw "MIPS compilation failed; see $WorkRoot/build.log" }
    $nm = Join-Path $SdkRoot 'bin/mipsel-none-elf-nm.exe'
    $symbols = @()
    foreach ($image in @('mipsync', 'scene_1')) {
        $text = (& $nm -n (Join-Path $build "$image.elf")) -join "`n"
        if ($LASTEXITCODE -ne 0) { throw "Could not inspect $image.elf" }
        $text | Set-Content -LiteralPath (Join-Path $WorkRoot "$image.symbols.txt") -Encoding utf8
        if ($text -match '\b(vm_execute|mbc_decode)\b' -or $text -notmatch 'mipsync_native_0_') {
            throw "$image does not contain the expected native-only script backend"
        }
        $symbols += $text
    }
    # Derive field offsets from the actual MIPS ABI rather than host pointer sizes.
    $probe = Join-Path $WorkRoot 'abi.c'
    @'
#include <stddef.h>
#include "vm.h"
const unsigned qa_fields_offset=offsetof(script_instance,runtime_fields)+offsetof(host_value,ival);
const unsigned qa_value_size=sizeof(host_value);
'@ | Set-Content -LiteralPath $probe -Encoding ascii
    $assembly = Join-Path $WorkRoot 'abi.s'
    & (Join-Path $SdkRoot 'bin/mipsel-none-elf-gcc.exe') -ffreestanding -DMIPSYNC_SCRIPT_AOT=1 -I (Join-Path $source 'runtime') -S $probe -o $assembly
    if ($LASTEXITCODE -ne 0) { throw 'MIPS ABI probe failed' }
    $abi = Get-Content -LiteralPath $assembly -Raw
    $fieldOffset = [regex]::Match($abi, 'qa_fields_offset:\s*\.word\s+(\d+)').Groups[1].Value
    $valueSize = [regex]::Match($abi, 'qa_value_size:\s*\.word\s+(\d+)').Groups[1].Value
    if (-not $fieldOffset -or -not $valueSize) { throw 'Cannot read target ABI offsets' }
    $status = 'MIPS_BUILD_PASS'
    if ($EmulatorPath) {
        $EmulatorPath = (Resolve-Path -LiteralPath $EmulatorPath).Path
        $lua = Get-Content -LiteralPath (Join-Path $fixture 'check.lua.in') -Raw
        for ($i = 0; $i -lt 2; $i++) {
            foreach ($entry in @(@("@INSTANCE$i@", 'g_instances'), @("@MODULE$i@", 'g_mipsync_scripts'))) {
                $match = [regex]::Match($symbols[$i], '(?m)^([0-9a-f]+)\s+\w\s+' + $entry[1] + '$')
                if (-not $match.Success) { throw "Missing native test symbol $($entry[1])" }
                $hex = $match.Groups[1].Value
                $address = [Convert]::ToUInt32($hex.Substring($hex.Length - 8), 16)
                $lua = $lua.Replace($entry[0], $address.ToString())
            }
        }
        $result = Join-Path $WorkRoot 'emulator-result.txt'
        $lua = $lua.Replace('@FIELD_OFFSET@', $fieldOffset).Replace('@VALUE_SIZE@', $valueSize).Replace('@RESULT@', $result.Replace('\', '/'))
        $luaPath = Join-Path $WorkRoot 'check.lua'
        [System.IO.File]::WriteAllText($luaPath, $lua, [System.Text.UTF8Encoding]::new($false))
        $portable = Join-Path $WorkRoot 'emulator'
        New-Item -ItemType Directory -Path $portable | Out-Null
        $bios = Join-Path $repo 'third_party/ps1_runtime/bios/openbios.bin'
        $arguments = @('-cli', '-testmode', '-portable', $portable, '-bios', $bios,
            '-iso', (Join-Path $build 'mipsync.cue'), '-run', '-fastboot', '-interpreter', '-softgpu', '-2mb', '-dofile', $luaPath)
        $quoted = $arguments | ForEach-Object { '"' + $_ + '"' }
        $process = Start-Process -FilePath $EmulatorPath -ArgumentList $quoted -WorkingDirectory $WorkRoot -WindowStyle Hidden -PassThru
        if (-not $process.WaitForExit(45000)) {
            Get-CimInstance Win32_Process -Filter "ParentProcessId=$($process.Id)" | ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
            Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
            throw 'Native emulator integration timed out'
        }
        $status = (Get-Content -LiteralPath $result -Raw).Trim()
        if ($status -notmatch '^PASS ') { throw "Native MIPS execution failed: $status" }
    }
    Write-Output "$status; artifacts: $WorkRoot"
} finally {
    $env:PSN00BSDK = $previousSdk
    $env:PATH = $previousPath
}
