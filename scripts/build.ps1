param([string]$BuildDirectory = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'build' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ tools are required.' }
$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
& $cmake -S $projectRoot -B $BuildDirectory -A Win32 "-DCMAKE_GENERATOR_INSTANCE=$vsPath"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $BuildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
& $cmake --build $BuildDirectory --config Release --target RUN_TESTS
if ($LASTEXITCODE -ne 0) { throw 'Command translation tests failed.' }
$bin = Join-Path $projectRoot 'bin'
New-Item -ItemType Directory -Path $bin -Force | Out-Null
foreach ($name in @('pluto-scr.dll','scr-loader.exe')) {
  Copy-Item -LiteralPath (Join-Path $BuildDirectory "Release/$name") -Destination $bin -Force
}
