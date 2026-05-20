$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $PSScriptRoot "src\gacha_sim.cpp"
$binDir = Join-Path $root "bin"
$exe = Join-Path $binDir "gacha_sim.exe"

New-Item -ItemType Directory -Force $binDir | Out-Null

g++ -O2 -std=c++17 $src -o $exe

Write-Host "Built $exe"
