# 部屋のステージのカメラの確認
#   1. 家具の脚・壁際・部屋の角などに体を置いて撮る (tests/out/room-camera-*.png、距離は room-camera.txt)
#   2. チュートリアルの後: 目標の発表 → ステージの紹介 → 収納ケースの上からスタート (tests/out/room-intro-*.png、room-intro.txt)
param([string]$Executable = '../Generated/Outputs/Debug/DirectXGameApp.exe')
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
Push-Location $repoPath
try {
    New-Item -ItemType Directory -Force -Path 'tests/out' | Out-Null
    $exe = (Resolve-Path -LiteralPath $Executable).Path
    function Invoke-Check([string[]]$arguments, [string]$report) {
        if (Test-Path $report) { Remove-Item $report }
        $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $repoPath -WindowStyle Hidden -PassThru
        if (-not $process.WaitForExit(240000)) { Stop-Process -Id $process.Id; throw "$report exceeded 240 seconds" }
        if (!(Test-Path $report)) { throw "No report: $report" }
        Get-Content $report
        if ($process.ExitCode -ne 0) { throw "$report failed (exit $($process.ExitCode))" }
    }
    Invoke-Check @('--slime-life-smoke', '--slime-life-room-camera', '--devour-stage=0', '--devour-no-tutorial') 'tests/out/room-camera.txt'
    Invoke-Check @('--slime-life-smoke', '--slime-life-room-intro', '--devour-stage=0') 'tests/out/room-intro.txt'
} finally { Pop-Location }
