param([string]$Destination = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Destination) { $Destination = Join-Path (Split-Path $projectRoot -Parent) 'pluto-remastered-multiplayer-dev-13515-x86.zip' }
# Exclude local logs, settings, opening statistics, build products and upstream
# game/model inputs. Keep the complete bridge and library sources for rebuilding.
$names = @('bin','scripts','src','tests','validation','vendor','CMakeLists.txt','README.md','THIRD_PARTY.md','Start Pluto.cmd','Start Pluto Multiplayer.cmd','Start Pluto Challenge.cmd')
$paths = $names | ForEach-Object { Join-Path $projectRoot $_ }
Compress-Archive -LiteralPath $paths -DestinationPath $Destination -CompressionLevel Optimal -Force
Write-Output "Created $Destination"
