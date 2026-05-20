$ErrorActionPreference = "Stop"

$port = 8765
$stoppedAny = $false

# 1) Prefer precise stop by port owner.
try {
    $conn = Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($conn) {
        $pid = $conn.OwningProcess
        if ($pid -and $pid -gt 0) {
            Stop-Process -Id $pid -Force -ErrorAction SilentlyContinue
            Write-Host "已停止监听端口 $port 的进程 PID=$pid"
            $stoppedAny = $true
        }
    }
} catch {
}

# 2) Fallback: stop python processes whose command line points to backend/server.py in this repo.
try {
    $root = Split-Path -Parent $MyInvocation.MyCommand.Path
    $escapedRoot = [Regex]::Escape($root)
    $candidates = Get-CimInstance Win32_Process -Filter "Name='python.exe'" |
        Where-Object { $_.CommandLine -match "backend\\server\.py" -and $_.CommandLine -match $escapedRoot }

    foreach ($p in $candidates) {
        Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue
        Write-Host "已停止后端进程 PID=$($p.ProcessId)"
        $stoppedAny = $true
    }
} catch {
}

if (-not $stoppedAny) {
    Write-Host "未发现可停止的本地抽卡模拟器服务进程。"
} else {
    Write-Host "抽卡模拟器后台服务已停止。"
}
