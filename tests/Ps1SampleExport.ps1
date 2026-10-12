param([Parameter(Mandatory=$true)][string]$EngineExe,
      [Parameter(Mandatory=$true)][string]$WorkDir)
$ErrorActionPreference='Stop'
$project=Join-Path $WorkDir 'sample audio project'
New-Item -ItemType Directory -Force -Path "$project/assets", "$project/scenes" | Out-Null
function Write-Wav([string]$path,[int]$samples) {
    $wav=[IO.BinaryWriter]::new([IO.File]::Create($path))
    try {
        $wav.Write([Text.Encoding]::ASCII.GetBytes('RIFF'));$wav.Write([uint32](36+$samples*2))
        $wav.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '));$wav.Write([uint32]16)
        $wav.Write([uint16]1);$wav.Write([uint16]1);$wav.Write([uint32]22050);$wav.Write([uint32]44100)
        $wav.Write([uint16]2);$wav.Write([uint16]16);$wav.Write([Text.Encoding]::ASCII.GetBytes('data'));$wav.Write([uint32]($samples*2))
        for($i=0;$i -lt $samples;++$i){$wav.Write([int16]([Math]::Sin($i*2*[Math]::PI*440/22050)*12000))}
    } finally {$wav.Dispose()}
}
Write-Wav "$project/assets/step.wav" 2205
$script='class SampleTrigger : MipsBehaviour {'
for($i=0;$i -lt 38;++$i){$script+=" public float setting$i = 0;"}
$script+=' public AudioSource sound; void Update() { if(Input.GetKeyDown("Jump")) sound.Play(); } }'
[IO.File]::WriteAllText("$project/assets/Trigger.mips",$script,[Text.UTF8Encoding]::new($false))
@{name='SampleExport';defaultScene='scenes/Title.nscene';playerSettings=@{productName='SampleExport';scenesInBuild=@('scenes/Title.nscene')}} |
    ConvertTo-Json -Depth 10 | Set-Content "$project/nostalty.project" -Encoding utf8
$entities=@(
    @{id=11;name='Camera';camera=@{primary=$true}},
    @{id=100;name='Canvas';canvas=@{referenceResolution=@(320,240)}},
    @{id=101;name='Menu';parent=100;rectTransform=@{sizeDelta=@(320,240)};uiButtonGroup=@{confirmAudioSource=99999}},
    @{id=103;name='Play';parent=101;rectTransform=@{sizeDelta=@(100,30)};uiButton=@{interactable=$true};mipsScripts=@(@{path='assets/Trigger.mips';fields=@{sound=5000}})},
    @{id=99999;name='Step';audioSource=@{type=2;clip='assets/step.wav';playOnAwake=$false}},
    @{id=5000;name='Same sample';audioSource=@{type=2;clip='assets/step.wav';playOnAwake=$false}}
)
function Export-Scene {
    @{entities=$entities} | ConvertTo-Json -Depth 20 | Set-Content "$project/scenes/Title.nscene" -Encoding utf8
    $output=& $EngineExe --export-ps1 $project 2>&1
    return @{code=$LASTEXITCODE;output=$output}
}
$export=Export-Scene
if($export.code -ne 0){throw "Sample export failed: $($export.output)"}
$source="$project/Builds/PS1/SampleExport/ps1_src"
$audio=Get-Content -Raw "$source/generated/audio_data.c"
$scene=Get-Content -Raw "$source/generated/scene_data.c"
if($audio -notmatch 'sample_0\[\]' -or $audio -notmatch '22050u,0,sample_0' -or
    $audio -notmatch 'g_ps1_midi_bank_size = 0u' -or $audio -notmatch 'g_ps1_audio_clip_count = 1u') {
    throw 'Recorded SFX must be resident, share duplicate clips and work without a MIDI tone bank'
}
$bytes=[regex]::Match($audio,'(?s)sample_0\[\].*?= \{(.*?)\};').Groups[1].Value -split ',' |
    Where-Object {$_.Trim() -ne ''} | ForEach-Object {[int]$_.Trim()}
$endBlocks=0
for($i=1;$i -lt $bytes.Count;$i+=16) {
    if(($bytes[$i] -band 4) -ne 0){throw 'Sample must not overwrite the explicit voice loop address'}
    if($bytes[$i] -eq 3){++$endBlocks}
}
if(($bytes.Count % 64) -ne 0 -or $endBlocks -ne 1){throw 'Sample DMA alignment or one-shot termination flags are invalid'}
$group=[regex]::Match($scene,'(?s)k_ps1_ui_button_groups\[\] = \{(.*?)\};').Groups[1].Value
if($group -notmatch '0u, 5u \}') {throw "Sample AudioSource ID was not resolved: $group"}
$iso=Get-Content -Raw "$source/iso.xml"
if($iso -match 'S000A00.VAG') {throw 'Resident sample must not require a CD audio file'}
Write-Wav "$project/assets/long.wav" (22050*6)
$entities[4].audioSource.clip='assets/long.wav'
$invalid=Export-Scene
if($invalid.code -eq 0 -or ($invalid.output -join "`n") -notmatch 'exceeds 64 KiB') {throw 'Oversized sample must fail before build with an actionable SPU limit error'}
# Distinct clips below the per-clip limit must still obey the shared SPU budget.
$originalEntities=$entities
Write-Wav "$project/assets/budget.wav" (22050*5)
for($i=0;$i -lt 8;++$i) {
    Copy-Item "$project/assets/budget.wav" "$project/assets/budget$i.wav"
    $entities+=@{id=7000+$i;name="Budget $i";audioSource=@{type=2;clip="assets/budget$i.wav";playOnAwake=$false}}
}
$entities[4].audioSource.clip='assets/step.wav'
$invalid=Export-Scene
if($invalid.code -eq 0 -or ($invalid.output -join "`n") -notmatch 'shared 440 KiB'){throw 'Shared SPU budget must be validated across distinct resident clips'}
$entities=$originalEntities
# Restore the valid fixture for emulator one-shot tests.
$entities[4].audioSource.clip='assets/step.wav'
$export=Export-Scene
if($export.code -ne 0){throw 'Could not restore sample fixture'}
Write-Output 'PS1 resident sample export, clip sharing, sample-only bank, source handles, no-CD playback and SPU size validation passed.'
