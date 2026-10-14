param([string]$Executable = '', [switch]$Sandbox)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
if (!$Executable) { $Executable = Join-Path $repoPath '../Generated/Outputs/Debug/DirectXGameApp.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (Get-Process -Name 'DirectXGameApp' -ErrorAction SilentlyContinue) {
    throw 'Close the running game first; this test will not interrupt another game process.'
}
$outputPath = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$backupPath = Join-Path $outputPath ('smoke-backup-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backupPath | Out-Null
$logs = @('error_log.txt', 'd3d12_validation_log.txt', 'black_screen_log.txt', 'run_log.txt', 'fluid_debug.txt', 'imgui.ini')
$validationPath = Join-Path $repoPath 'd3d12_validation_log.txt'
$previousLines = if (Test-Path -LiteralPath $validationPath) { @(Get-Content -LiteralPath $validationPath).Count } else { 0 }
foreach ($name in $logs) {
    $source = Join-Path $repoPath $name
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $backupPath $name) }
}
$process = $null
try {
    $argument = if ($Sandbox) { '--fluid-sandbox-smoke' } else { '--fluid-smoke' }
    $process = Start-Process -FilePath $Executable -ArgumentList $argument -WorkingDirectory $repoPath -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(60000)) { throw 'Fluid smoke test exceeded 60 seconds.' }
    $process.Refresh()
    if ($process.ExitCode -ne 0) { throw "Fluid smoke test exited with code $($process.ExitCode)." }
    $newValidation = @(Get-Content -LiteralPath $validationPath -ErrorAction SilentlyContinue | Select-Object -Skip $previousLines)
    $newValidation | Set-Content -LiteralPath (Join-Path $outputPath 'fluid-d3d12-validation.txt')
    Copy-Item -LiteralPath (Join-Path $repoPath 'error_log.txt') -Destination (Join-Path $outputPath 'fluid-smoke-run.txt')
    $runLog = Get-Content -Raw -LiteralPath (Join-Path $repoPath 'error_log.txt')
    $marker = if ($Sandbox) { 'Fluid sandbox smoke scene requested' } else { 'Fluid smoke scene requested' }
    if ($runLog -notmatch $marker -or $runLog -notmatch 'Shutting down') { throw 'Diagnostic scene or normal shutdown was not reached.' }
    if ($newValidation -match '\[(ERROR|CORRUPTION)\]|Device removed') { throw 'D3D12 validation errors: see tests/out/fluid-d3d12-validation.txt.' }
    if ($Sandbox) {
        $report = Get-Content -Raw -LiteralPath (Join-Path $outputPath 'fluid-sandbox-validation.txt')
        if ($report -match '(?m)^FAIL' -or $report -notmatch '(?m)^PASS fluid sandbox integration') { throw 'Fluid sandbox integration failed: see tests/out/fluid-sandbox-validation.txt.' }
        Write-Output 'PASS D3D12 fluid sandbox: player-free tank; glass + water render; scene roundtrip; emission limit + reset; no D3D12 errors reported.'
    } else {
        Write-Output 'PASS D3D12 fluid smoke: volume initialized; solve + render + debug modes; normal shutdown; no D3D12 errors reported.'
    }
} finally {
    # Stop only the test process we created, never the user's other processes.
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id }
    Copy-Item -LiteralPath (Join-Path $repoPath 'error_log.txt') -Destination (Join-Path $outputPath 'fluid-smoke-run.txt')
    @(Get-Content -LiteralPath $validationPath -ErrorAction SilentlyContinue | Select-Object -Skip $previousLines) | Set-Content -LiteralPath (Join-Path $outputPath 'fluid-d3d12-validation.txt')
    foreach ($name in $logs) {
        $backup = Join-Path $backupPath $name
        if (Test-Path -LiteralPath $backup) { Copy-Item -LiteralPath $backup -Destination (Join-Path $repoPath $name) }
    }
    # Backups are intentionally retained in ignored tests/out for recovery.
}
