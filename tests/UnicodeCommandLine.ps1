param([Parameter(Mandatory=$true)][string]$EngineExe,[Parameter(Mandatory=$true)][string]$WorkDir)
$ErrorActionPreference='Stop'
$name='Review '+[char]0x5BE9+[char]0x67FB+[char]0x7528+' '+[char]::ConvertFromUtf32(0x1F3AE)
$project=Join-Path $WorkDir $name
while($project.Length -lt 240){$project=Join-Path $project 'Nested_Review_Snapshot'}
$scripts=Join-Path $project 'assets/scripts'
$scenes=Join-Path $project 'scenes'
New-Item -ItemType Directory -Force -Path $scripts,$scenes | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
$script=Join-Path $scripts ([char]0x79FB+[char]0x52D5+'.mips')
[IO.File]::WriteAllText($script,'class Player : MipsBehaviour { public float speed=3; void Update(){ transform.position.x = transform.position.x + speed * Time.deltaTime; } }',$utf8)
$result=& $EngineExe --validate-mips $script 2>&1
if($LASTEXITCODE -ne 0){throw ('Unicode script argument failed: '+($result -join "`n"))}
[IO.File]::WriteAllText((Join-Path $scenes 'Title.nscene'),'{"version":1,"entities":[]}',$utf8)
$settings=@{name=$name;defaultScene='scenes/Title.nscene';playerSettings=@{productName='UnicodeReview';companyName='';scenesInBuild=@('scenes/Title.nscene');startupSceneIndex=0}}
[IO.File]::WriteAllText((Join-Path $project 'nostalty.project'),($settings|ConvertTo-Json -Depth 8),$utf8)
$result=& $EngineExe --build-pc $project 2>&1
if($LASTEXITCODE -ne 0){throw ('Unicode project argument failed: '+($result -join "`n"))}
if(-not (Test-Path -LiteralPath (Join-Path $project 'Builds/Windows/UnicodeReview/UnicodeReview_Data/game.mpak'))){throw 'Unicode build output missing'}
$result=& $EngineExe --build-pc $project 2>&1
if($LASTEXITCODE -ne 0){throw ('Unicode rebuild failed: '+($result -join "`n"))}
Write-Output 'Unicode script/project arguments passed: Japanese, spaces and characters outside the ANSI code page.'
