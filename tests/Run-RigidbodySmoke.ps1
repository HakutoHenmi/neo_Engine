param([string]$Executable = '')
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
if (!$Executable) { $Executable = Join-Path $repoPath 'bld/FluidSandbox/DirectXGameApp.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (Get-Process -Name 'DirectXGameApp' -ErrorAction SilentlyContinue) { throw 'Close the running game first; this test does not interrupt it.' }
$outputPath = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$backupPath = Join-Path $outputPath ('rigidbody-backup-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backupPath | Out-Null
$logs = @('error_log.txt','d3d12_validation_log.txt','black_screen_log.txt','run_log.txt','fluid_debug.txt','imgui.ini')
foreach ($name in $logs) {
    $source = Join-Path $repoPath $name
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $backupPath $name) }
}
$process = $null
try {
    foreach ($mode in @('chrono','legacy')) {
        $argument = if ($mode -eq 'chrono') { '--rigidbody-smoke' } else { '--rigidbody-legacy-smoke' }
        $validationPath = Join-Path $repoPath 'd3d12_validation_log.txt'
        $previousLines = if (Test-Path -LiteralPath $validationPath) { @(Get-Content -LiteralPath $validationPath).Count } else { 0 }
        $process = Start-Process -FilePath $Executable -ArgumentList $argument -WorkingDirectory $repoPath -WindowStyle Hidden -PassThru
        if (!$process.WaitForExit(60000)) { throw "Rigidbody $mode test exceeded 60 seconds." }
        $process.Refresh()
        if ($process.ExitCode -ne 0) { throw "Rigidbody $mode exited with code $($process.ExitCode)." }
        $report = Get-Content -Raw -LiteralPath (Join-Path $outputPath "rigidbody-$mode-validation.txt")
        if ($report -match '(?m)^FAIL' -or $report -notmatch '(?m)^PASS rigidbody integration') { throw "Rigidbody $mode assertions failed; see tests/out/rigidbody-$mode-validation.txt." }
        $validation = @(Get-Content -LiteralPath $validationPath -ErrorAction SilentlyContinue | Select-Object -Skip $previousLines)
        $validation | Set-Content -LiteralPath (Join-Path $outputPath "rigidbody-$mode-d3d12.txt")
        if ($validation -match '\[(ERROR|CORRUPTION)\]|Device removed') { throw "D3D12 error in Rigidbody $mode test." }
        Write-Output "PASS Rigidbody $mode scene: gravity without collider; static floor + trigger behavior; Play/Pause/Stop; render transform follows physics."
    }
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id }
    foreach ($name in $logs) {
        $backup = Join-Path $backupPath $name
        if (Test-Path -LiteralPath $backup) { Copy-Item -LiteralPath $backup -Destination (Join-Path $repoPath $name) }
    }
}
