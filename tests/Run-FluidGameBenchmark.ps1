param([string]$Executable = '', [ValidateSet('game', 'editor', 'collision')][string]$Mode = 'game')
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
if (!$Executable) { $Executable = Join-Path $repoPath '../Generated/Outputs/Development/DirectXGameApp.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (Get-Process -Name 'DirectXGameApp' -ErrorAction SilentlyContinue) {
    throw 'Close the running game first; this benchmark will not interrupt another game process.'
}
$outputPath = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$backupPath = Join-Path $outputPath ('benchmark-backup-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backupPath | Out-Null
$logs = @('error_log.txt', 'd3d12_validation_log.txt', 'black_screen_log.txt', 'run_log.txt', 'fluid_debug.txt')
$validationPath = Join-Path $repoPath 'd3d12_validation_log.txt'
$previousValidationLines = if (Test-Path -LiteralPath $validationPath) { @(Get-Content -LiteralPath $validationPath).Count } else { 0 }
foreach ($name in $logs) {
    $source = Join-Path $repoPath $name
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $backupPath $name) }
}
$reportPath = Join-Path $outputPath 'fluid-game-benchmark.txt'
if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath }
$process = $null
try {
    $argument = if ($Mode -eq 'editor') { '--fluid-game-benchmark-editor' } elseif ($Mode -eq 'collision') { '--fluid-collision-smoke' } else { '--fluid-game-benchmark' }
    $process = Start-Process -FilePath $Executable -ArgumentList $argument -WorkingDirectory $repoPath -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(180000)) { throw 'Fluid game benchmark exceeded 180 seconds.' }
    if (!(Test-Path -LiteralPath $reportPath)) { throw 'Fluid game benchmark did not produce a report.' }
    $report = Get-Content -Raw -LiteralPath $reportPath
    if ($report -notmatch 'sampled=1') { throw "Invalid fluid GPU sample: $report" }
    if ($Mode -eq 'collision') {
        if ($report -notmatch 'collision_dispatches=[1-9]') { throw "GPU collision path was not exercised: $report" }
    } elseif ($report -notmatch 'particles=(2[7-9][0-9]{3}|3[0-2][0-9]{3})') {
        throw "Game fluid did not reach the expected particle load: $report"
    }
    $newValidation = @(Get-Content -LiteralPath $validationPath -ErrorAction SilentlyContinue | Select-Object -Skip $previousValidationLines)
    $newValidation | Set-Content -LiteralPath (Join-Path $outputPath 'fluid-game-d3d12-validation.txt')
    if ($newValidation -match '\[(ERROR|CORRUPTION)\]|Device removed') { throw 'D3D12 validation errors; see tests/out/fluid-game-d3d12-validation.txt.' }
    Copy-Item -LiteralPath $reportPath -Destination (Join-Path $outputPath "fluid-game-benchmark-$Mode.txt")
    Write-Output $report
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id }
    Copy-Item -LiteralPath (Join-Path $repoPath 'error_log.txt') -Destination (Join-Path $outputPath 'fluid-game-run.txt') -ErrorAction SilentlyContinue
    foreach ($name in $logs) {
        $backup = Join-Path $backupPath $name
        if (Test-Path -LiteralPath $backup) { Copy-Item -LiteralPath $backup -Destination (Join-Path $repoPath $name) }
    }
}
