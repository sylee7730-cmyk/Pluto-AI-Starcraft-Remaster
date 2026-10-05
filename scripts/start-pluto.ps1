[CmdletBinding()]
param(
  [string]$StarCraft = (Join-Path ${env:ProgramFiles(x86)} 'StarCraft\x86\StarCraft.exe'),
  [string]$Pluto = 'C:\StarCraft1161\bwapi-data\AI\pluto.dll',
  [ValidateRange(-1,1000)][int]$SpeedMs = 0,
  [switch]$DrawGraph,
  [switch]$Multiplayer,
  [switch]$Challenge,
  [switch]$Allies,
  [ValidateSet('own','stasis','hide')][string]$AllyView='own',
  [ValidateSet('off','army','kills','all')][string]$TeamStats='off',
  [switch]$AnyGame,
  [switch]$KeepOnlineHold,
  [ValidateRange(0,255)][int]$PauseKey=120,
  [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
if ($TeamStats -ne 'off' -and -not ($Allies -or $AnyGame)) { throw '-TeamStats requires -Allies.' }
if ($Allies -and -not $Challenge) { throw '-Allies requires -Challenge (allied play builds on the 1-v-many admission rules).' }
$projectRoot = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $projectRoot 'bin'
if ($AnyGame) { Write-Output 'Play-anyway mode: Pluto starts whatever game is created, even if it differs from the selected mode (replays, observing and games without an opponent excluded).' }
$runtime = Join-Path $projectRoot $(if ($Allies) { 'runtime-allies' } elseif ($Challenge) { 'runtime-challenge' } elseif ($Multiplayer) { 'runtime-multiplayer' } else { 'runtime' })
function Assert-Hash([string]$Path, [string[]]$Expected) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path" }
  # Use the built-in .NET implementation, including on Windows PowerShell 5.1
  # installations where the Get-FileHash function is not automatically loaded.
  $algorithm = [System.Security.Cryptography.SHA256]::Create()
  $stream = $null
  try {
    $stream = [System.IO.File]::OpenRead($Path)
    $actual = [System.BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
  } finally {
    if ($null -ne $stream) { $stream.Dispose() }
    $algorithm.Dispose()
  }
  if (@($Expected) -notcontains $actual) {
    throw "Unsupported or damaged file: $Path. See README.md for supported versions."
  }
}
$StarCraft = (Resolve-Path -LiteralPath $StarCraft).Path
$Pluto = (Resolve-Path -LiteralPath $Pluto).Path
Assert-Hash $StarCraft '32dbbdd001dd381cb1b3a719b7ad1fc918a9d4bc99661c675e00254efecca827'
# Original CoG 2026 DLL, or the same DLL with the launcher's 4-byte no-resign patch.
Assert-Hash $Pluto @('7e360b643c8c0156c03fe0cad9972a3058138ccfe22f921c5b4e0cd0aaf0abef','ed19fff2ff212fcbf8e7b286aebb825754ce8d747cda45e03a75d70f146e0224')
$modelDirectory = Join-Path (Split-Path $Pluto -Parent) 'pluto'
Assert-Hash (Join-Path $modelDirectory 'pluto_infer.exe') 'ad880d8be52a6ef03893fa20644b6627e04fcd55e030178c6d52486b82340f2b'
Assert-Hash (Join-Path $modelDirectory 'pluto_weights.bin') '00b400eace6e4782202ebdcb3c30db76054aaa6a08c6a7dcb59575abc2d0a26e'
foreach ($name in @('pluto-scr.dll','scr-loader.exe')) {
  if (-not (Test-Path -LiteralPath (Join-Path $bin $name))) { throw "Missing bin/$name. Run scripts/build.ps1 first." }
}
if ($VerifyOnly) { Write-Output 'Verified StarCraft, Pluto DLL, inference engine, weights and bridge files.'; return }
if (Get-Process -Name StarCraft -ErrorAction SilentlyContinue) {
  throw 'Exit the existing StarCraft process before starting Pluto. No existing game was changed.'
}
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
$previousLogs = @('bridge.log','pluto.log','pluto_infer.log') | ForEach-Object { Join-Path $runtime $_ } | Where-Object { Test-Path -LiteralPath $_ }
if ($previousLogs) {
  $archive = Join-Path $runtime ('logs/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
  New-Item -ItemType Directory -Path $archive -Force | Out-Null
  foreach ($path in $previousLogs) { Move-Item -LiteralPath $path -Destination $archive }
}
Copy-Item -LiteralPath (Join-Path $bin 'pluto-scr.dll') -Destination $runtime -Force
@('[pluto]',"module=$Pluto","speed_ms=$SpeedMs","multiplayer=$([int]$Multiplayer.IsPresent)","challenge=$([int]$Challenge.IsPresent)","allies=$([int]$Allies.IsPresent)","ally_view=$AllyView","team_stats=$TeamStats","pause_key=$PauseKey","any_game=$([int]$AnyGame.IsPresent)","latency_hold_online=$([int]$KeepOnlineHold.IsPresent)") | Set-Content -LiteralPath (Join-Path $runtime 'bridge.ini') -Encoding Unicode
$previousDraw = $env:BWRL_DRAW
$previousStraddle = $env:BWRL_STRADDLE
$previousBudget = $env:BWRL_FRAME_BUDGET_MS
try {
  if ($DrawGraph) { $env:BWRL_DRAW = '1' }
  if ($Multiplayer) {
    $env:BWRL_STRADDLE = '1'
    if (-not $env:BWRL_FRAME_BUDGET_MS) { $env:BWRL_FRAME_BUDGET_MS = '10' }
  }
  $game = Start-Process -FilePath $StarCraft -ArgumentList '-launch' -WorkingDirectory $runtime -WindowStyle Hidden -PassThru
} finally {
  $env:BWRL_DRAW = $previousDraw
  $env:BWRL_STRADDLE = $previousStraddle
  $env:BWRL_FRAME_BUDGET_MS = $previousBudget
}
try {
  $ready = $false
  for ($attempt=0; $attempt -lt 60; $attempt++) {
    $game.Refresh()
    if ($game.HasExited) { throw 'StarCraft exited before initialization.' }
    if ($game.MainWindowHandle -ne 0) { $ready=$true; break }
    Start-Sleep -Milliseconds 500
  }
  if (-not $ready) { throw 'StarCraft did not create its window within 30 seconds.' }
  & (Join-Path $bin 'scr-loader.exe') $game.Id (Join-Path $runtime 'pluto-scr.dll')
  if ($LASTEXITCODE -ne 0) { throw 'Bridge loading failed.' }
  $log = Join-Path $runtime 'bridge.log'
  $initialized = $false
  for ($attempt=0; $attempt -lt 30; $attempt++) {
    if ((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -SimpleMatch '"result":"MH_OK"' -Quiet)) { $initialized=$true; break }
    Start-Sleep -Milliseconds 200
  }
  if (-not $initialized) { throw "Bridge initialization failed. Read $log" }
  if ($Allies) {
    Write-Output "Experimental allied-team bridge ready in StarCraft process $($game.Id). Choose Top vs Bottom (or Free For All) and put Pluto on a team with allies."
    switch ($AllyView) {
      'hide'   { Write-Output 'Allied units are hidden from Pluto, so it plays alone and cannot coordinate with them.' }
      'stasis' { Write-Output 'Allied units are shown to Pluto as its own forces held in stasis; orders for them are dropped.' }
      default  { Write-Output 'Allied units are shown to Pluto as its own forces; orders for them are dropped.' }
    }
    if ($TeamStats -ne 'off') { Write-Output "Team strength experiment ($TeamStats): statistics Pluto reads include its allies'." }
    Write-Output 'The original 1v1 model is used; match quality is unverified.'
  } elseif ($Challenge) {
    Write-Output "Experimental 1-v-many bridge ready in StarCraft process $($game.Id). Choose Melee, Free For All or Top vs Bottom."
    Write-Output 'Put Pluto alone on its side, with 1-7 opponents. This uses the original 1v1 model; match quality is unverified.'
  } elseif ($Multiplayer) {
    Write-Output "Experimental multiplayer bridge ready in StarCraft process $($game.Id). Choose a custom 1v1 Melee game."
    Write-Output 'Network synchronization with a second client still requires validation. The game controls multiplayer speed.'
  } else {
    Write-Output "Pluto ready in StarCraft process $($game.Id). Choose Single Player > Play Custom, Melee, one Computer opponent."
  }
  Write-Output "Logs and persistent opening statistics: $runtime"
} catch {
  # Only this launch's process is owned by the script; never stop an unrelated game.
  $game.Refresh()
  if (-not $game.HasExited) { Stop-Process -Id $game.Id }
  throw
}
