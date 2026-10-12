param([Parameter(Mandatory=$true)][string]$EngineExe,
      [Parameter(Mandatory=$true)][string]$WorkDir)
$ErrorActionPreference='Stop'
$project=Join-Path $WorkDir 'midi audio project'
New-Item -ItemType Directory -Force -Path "$project/assets", "$project/scenes" | Out-Null
# Format 0, 96 PPQ: program, note on, one quarter note, note off, end.
[byte[]]$midi=77,84,104,100,0,0,0,6,0,0,0,1,0,96,77,84,114,107,0,0,0,15,0,192,16,0,144,72,100,96,128,72,0,0,255,47,0
[IO.File]::WriteAllBytes("$project/assets/menu.mid",$midi)
$wav=[IO.BinaryWriter]::new([IO.File]::Create("$project/assets/bgm.wav"))
try {
    $wav.Write([Text.Encoding]::ASCII.GetBytes('RIFF'));$wav.Write([uint32](36+2048*2))
    $wav.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '));$wav.Write([uint32]16)
    $wav.Write([uint16]1);$wav.Write([uint16]1);$wav.Write([uint32]22050);$wav.Write([uint32]44100)
    $wav.Write([uint16]2);$wav.Write([uint16]16);$wav.Write([Text.Encoding]::ASCII.GetBytes('data'));$wav.Write([uint32](2048*2))
    for($i=0;$i -lt 2048;++$i){$wav.Write([int16]([Math]::Sin($i*2*[Math]::PI*440/22050)*12000))}
} finally {$wav.Dispose()}
@{name='MidiExport';defaultScene='scenes/Title.nscene';playerSettings=@{productName='MidiExport';scenesInBuild=@('scenes/Title.nscene')}} |
    ConvertTo-Json -Depth 10 | Set-Content "$project/nostalty.project" -Encoding utf8
$entities=@(
    @{id=11;name='Camera';camera=@{primary=$true}},
    @{id=100;name='Canvas';canvas=@{referenceResolution=@(320,240)}},
    @{id=101;name='Menu';parent=100;rectTransform=@{sizeDelta=@(320,240)};uiButtonGroup=@{navigationAudioSource=99999;confirmAudioSource=5000}},
    @{id=103;name='Play';parent=101;rectTransform=@{sizeDelta=@(100,30)};uiButton=@{interactable=$true}},
    @{id=99999;name='Move';audioSource=@{type=1;clip='assets/menu.mid';playOnAwake=$false}},
    @{id=5000;name='Press';audioSource=@{type=1;clip='assets/menu.mid';playOnAwake=$false}},
    # No type means the existing BGM behavior.
    @{id=99991;name='BGM';audioSource=@{clip='assets/bgm.wav';loop=$true}}
)
function Export-Scene {
    @{entities=$entities} | ConvertTo-Json -Depth 20 | Set-Content "$project/scenes/Title.nscene" -Encoding utf8
    $output=& $EngineExe --export-ps1 $project 2>&1
    return @{code=$LASTEXITCODE;output=$output}
}
$export=Export-Scene
if($export.code -ne 0){throw "MIDI export failed: $($export.output)"}
$source="$project/Builds/PS1/MidiExport/ps1_src"
$audio=Get-Content -Raw "$source/generated/audio_data.c"
$scene=Get-Content -Raw "$source/generated/scene_data.c"
if($audio -notmatch 'midi_events_0' -or $audio -notmatch '\{500000u,128,72,0\}' -or
    $audio -notmatch 'g_ps1_midi_bank_size = 2048u' -or $audio -notmatch 'g_ps1_audio_clip_count = 2u'){
    throw 'MIDI notes, tempo, resident tone bank or duplicate-clip sharing were lost'
}
$group=[regex]::Match($scene,'(?s)k_ps1_ui_button_groups\[\] = \{(.*?)\};').Groups[1].Value
if($group -notmatch '5u, 6u \}') {throw "Source IDs were not resolved to runtime handles: $group"}
$iso=Get-Content -Raw "$source/iso.xml"
if($iso -match 'S000A00.VAG' -or $iso -notmatch 'S000A01.VAG') {throw 'MIDI SFX should be resident; only BGM should be added to the disc'}
$entities[4].audioSource.type=0
$invalid=Export-Scene
if($invalid.code -eq 0 -or ($invalid.output -join "`n") -notmatch 'SFX requires') {throw 'Mismatched clip type must fail with an actionable error'}
Write-Output 'PS1 MIDI event export, resident bank, source references, BGM compatibility and clip validation passed.'
