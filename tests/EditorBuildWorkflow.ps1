param(
 [Parameter(Mandatory=$true)][string]$EngineExe,
 [Parameter(Mandatory=$true)][string]$CliExe,
 [Parameter(Mandatory=$true)][string]$Project,
 [Parameter(Mandatory=$true)][string]$EvidenceDir
)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
$p=$null
function Invoke-Cli([string[]]$command){
 $r=& $CliExe --project $Project --instance ([string]$p.Id) @command --json | ConvertFrom-Json
 if(-not $r.success){throw ($r|ConvertTo-Json -Depth 10)}
 return $r
}
try {
 $p=Start-Process -FilePath $EngineExe -ArgumentList @('--project',('"'+$Project+'"'),'--no-splash') -WorkingDirectory (Split-Path $EngineExe) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $EvidenceDir 'editor-stdout.txt') -RedirectStandardError (Join-Path $EvidenceDir 'editor-stderr.txt')
 $ready=$false
 for($i=0;$i -lt 100;$i++){
  $p.Refresh();if($p.HasExited){throw ('Editor exited '+$p.ExitCode)}
  $instances=& $CliExe instances --json|ConvertFrom-Json
  if(@($instances.result.instances|Where-Object processId -eq $p.Id).Count){$ready=$true;break}
  Start-Sleep -Milliseconds 200
 }
 if(-not $ready){throw 'Editor command platform unavailable'}
 $null=& $CliExe describe build.start --json
 $null=& $CliExe describe build.inspect --json
 $invalid=& $CliExe --project $Project --instance ([string]$p.Id) build start invalid --json|ConvertFrom-Json
 if($invalid.success){throw 'Unknown build target was accepted'}
 $initial=Invoke-Cli @('scene','inspect')
 if($initial.result.path -notmatch 'Title.nscene$'){throw 'Sample did not open Title.nscene'}
 $null=Invoke-Cli @('runtime','play');Start-Sleep -Milliseconds 500
 $duringPlay=& $CliExe --project $Project --instance ([string]$p.Id) build start pc --json|ConvertFrom-Json
 if($duringPlay.success){throw 'Build was accepted while playing'}
 $null=Invoke-Cli @('runtime','stop')
 $results=@()
 foreach($target in @('pc','pc','ps1','disc')) {
  $null=Invoke-Cli @('build','start',$target)
  $deadline=[DateTime]::UtcNow.AddSeconds(100)
  do {
   Start-Sleep -Milliseconds 200
   $state=Invoke-Cli @('build','inspect')
   if([DateTime]::UtcNow -gt $deadline){throw ('Editor build timed out: '+$target)}
  }while($state.result.running)
  if(-not $state.result.finished -or -not $state.result.success){throw ($state|ConvertTo-Json -Depth 10)}
  if(-not (Test-Path -LiteralPath $state.result.executable)){throw ('Build executable missing: '+$target)}
  if($target -ne 'pc' -and -not (Test-Path -LiteralPath $state.result.cue)){throw 'CUE missing'}
  if($target -eq 'disc' -and -not (Test-Path -LiteralPath (Join-Path $state.result.discFolder 'SCN00001.EXE'))){throw 'Disc folder scene executable missing'}
  $results+=@{target=$target;success=$true;output=$state.result.output}
 }
 $null=Invoke-Cli @('scene','open','scenes/Game.nscene','--save-current','true')
 $null=Invoke-Cli @('runtime','play');Start-Sleep -Seconds 1;$null=Invoke-Cli @('runtime','stop')
 $null=Invoke-Cli @('scene','open','scenes/Title.nscene','--save-current','true')
 $p.Refresh();if($p.HasExited){throw 'Editor exited during build workflow'}
 $results|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $EvidenceDir 'result.json') -Encoding utf8
 Write-Output 'Editor Build Settings service: PC build/rebuild, PS1 build, disc folder, validation and scene play passed.'
}finally{if($p){$p.Refresh();if(-not $p.HasExited){$p.CloseMainWindow()|Out-Null;if(-not $p.WaitForExit(5000)){Stop-Process -Id $p.Id}}}}
