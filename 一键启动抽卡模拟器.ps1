$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$url = "http://127.0.0.1:8765"
$apiUrl = "$url/api/simulators"
$embeddedPython = Join-Path $root "python_env\python.exe"

function Resolve-Python {
    if (Test-Path $embeddedPython) {
        return $embeddedPython
    }
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) {
        return "python"
    }
    return $null
}

function Test-Server {
    try {
        Invoke-WebRequest -Uri $apiUrl -UseBasicParsing -TimeoutSec 2 | Out-Null
        return $true
    } catch {
        return $false
    }
}

if (-not (Test-Server)) {
    $pythonExe = Resolve-Python
    if (-not $pythonExe) {
        throw "未找到可用 Python。请确认项目下存在 python_env\\python.exe，或系统 PATH 中有 python。"
    }

    if (-not (Test-Path "bin\gacha_sim.exe")) {
        Write-Host "未找到 bin\gacha_sim.exe，正在编译..."
        powershell -ExecutionPolicy Bypass -File "cpp\build.ps1"
    }

    if ($pythonExe -eq "python") {
        Start-Process -FilePath "cmd.exe" -ArgumentList "/k python backend\server.py" -WorkingDirectory $root
    } else {
        Start-Process -FilePath "cmd.exe" -ArgumentList "/k `"$pythonExe`" backend\server.py" -WorkingDirectory $root
    }

    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Milliseconds 500
        if (Test-Server) { break }
    }
}

Start-Process $url
Write-Host "已打开 $url"
