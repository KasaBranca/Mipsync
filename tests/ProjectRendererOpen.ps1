param(
    [Parameter(Mandatory=$true)][string]$EngineExe,
    [Parameter(Mandatory=$true)][string]$CliExe,
    [Parameter(Mandatory=$true)][string]$WorkDir
)
$ErrorActionPreference = 'Stop'
$project = Join-Path ([IO.Path]::GetFullPath($WorkDir)) 'existing game'
New-Item -ItemType Directory -Force -Path "$project/scenes" | Out-Null
[IO.File]::WriteAllText("$project/nostalty.project", '{"name":"RendererMigration","engineVersion":"0.1.0","defaultScene":"scenes/start.nscene"}')
[IO.File]::WriteAllText("$project/scenes/start.nscene", '{"entities":[]}')
[IO.File]::WriteAllText("$project/AGENTS.md", "# Personal instructions`nKeep my project rules.`n")
function Open-And-Check([bool]$reopen) {
    $editor = Start-Process -FilePath $EngineExe -ArgumentList @('--no-splash', '--project', "`"$project`"") -WindowStyle Hidden -PassThru
    try {
        $ready = $false
        for ($i=0; $i -lt 60; ++$i) {
            $instances = & $CliExe instances --json | ConvertFrom-Json
            if (@($instances.result.instances | Where-Object { $_.processId -eq $editor.Id }).Count -eq 1) { $ready=$true; break }
            if ($editor.HasExited) { throw 'Migration Editor exited before CLI registration' }
            Start-Sleep -Milliseconds 500
        }
        if (!$ready) { throw 'Migration Editor did not become ready' }
        foreach ($file in @('renderer/pc/mesh.vert','renderer/pc/mesh.frag','renderer/pc/screen.frag','renderer/ps1/runtime/render.c','renderer/README.md','.nostalty/renderer-defaults/ps1/runtime/render.c')) {
            if (!(Test-Path -LiteralPath "$project/$file")) { throw "Opening old project did not install $file" }
        }
        $bundled = [IO.File]::ReadAllText((Join-Path (Split-Path $EngineExe -Parent) 'skills/mipsync-cli/SKILL.md'))
        $actual = [IO.File]::ReadAllText("$project/.agents/skills/mipsync-cli/SKILL.md")
        if ($actual -cne $bundled) { throw 'Opening project did not replace CLI Skill with bundled version' }
        if (Test-Path -LiteralPath "$project/.agents/skills/mipsync-cli/references/obsolete.md") { throw 'Retired Skill reference remained' }
        if (![IO.File]::ReadAllText("$project/AGENTS.md").Contains('Keep my project rules.')) { throw 'Personal instructions were replaced' }
        if ($reopen -and ![IO.File]::ReadAllText("$project/renderer/ps1/runtime/render.c").Contains('GAME_RENDERER_PRESERVED')) { throw 'Reopening replaced custom renderer' }
    } finally {
        if (!$editor.HasExited) {
            $editor.CloseMainWindow() | Out-Null
            if (!$editor.WaitForExit(5000)) { Stop-Process -Id $editor.Id }
        }
    }
}
Open-And-Check $false
[IO.File]::AppendAllText("$project/renderer/ps1/runtime/render.c", "`n/* GAME_RENDERER_PRESERVED */`n")
[IO.File]::WriteAllText("$project/.agents/skills/mipsync-cli/SKILL.md", 'old Skill from previous version')
[IO.File]::WriteAllText("$project/.agents/skills/mipsync-cli/references/obsolete.md", 'obsolete commands')
Remove-Item -LiteralPath "$project/renderer/pc/screen.frag"
Open-And-Check $true
Write-Output 'Real Editor open/reopen: old project migration, custom renderer preservation, Skill replacement and retired reference removal passed.'
