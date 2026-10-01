param([string]$Executable = '', [switch]$Ink, [switch]$Rogue, [switch]$Horde, [switch]$SceneryPerf, [switch]$DomainSkills, [switch]$DomainChain, [switch]$SlimeBattle, [switch]$SlimeLow, [switch]$SlimePolish, [switch]$SlimeField, [switch]$Creature, [switch]$Snake, [ValidateRange(0,2)][int]$AttackPattern=0, [switch]$RecordMotion, [switch]$Portfolio)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
if (!$Executable) { $Executable = Join-Path $repoPath '../Generated/Outputs/Development/DirectXGameApp.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (Get-Process -Name 'DirectXGameApp' -ErrorAction SilentlyContinue) {
    throw 'Close the running game first; this test will not interrupt another game process.'
}
$outputPath = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$backupPath = Join-Path $outputPath ('smoke-backup-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backupPath | Out-Null
$logs = @('error_log.txt', 'd3d12_validation_log.txt', 'black_screen_log.txt', 'run_log.txt', 'fluid_debug.txt')
$validationPath = Join-Path $repoPath 'd3d12_validation_log.txt'
$previousLines = if (Test-Path -LiteralPath $validationPath) { @(Get-Content -LiteralPath $validationPath).Count } else { 0 }
foreach ($name in $logs) {
    $source = Join-Path $repoPath $name
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $backupPath $name) }
}
$process = $null
$reportName = if ($Ink) { 'ink-smoke.txt' } elseif ($Snake) { 'snake-smoke.txt' } elseif ($Creature) { 'creature-smoke.txt' } else { 'chrono-smoke.txt' }
$argument = if ($Ink) { '--ink-smoke' } elseif ($Snake) { '--snake-smoke' } elseif ($Creature) { '--creature-smoke' } else { '--chrono-smoke' }
if ($Snake) { $argument += " --attack-pattern=$AttackPattern" }
if ($RecordMotion) { $argument += ' --record-motion' }
if ($SlimeBattle) { $argument = '--ink-smoke --slime-battle'; $reportName = 'ink-smoke.txt' }
if ($SlimePolish) { $argument = '--ink-smoke --slime-polish'; $reportName = 'ink-smoke.txt' }
if ($SlimeField) { $argument = '--ink-smoke --slime-field'; $reportName = 'ink-smoke.txt' }
if ($SlimeLow) { $argument = '--ink-smoke --slime-battle --slime-low'; $reportName = 'ink-smoke.txt' }
if ($Portfolio) { $argument += ' --portfolio' }
if ($DomainSkills) { $argument = '--ink-smoke --domain-skills'; $reportName = 'ink-smoke.txt' }
if ($DomainChain) { $argument = '--ink-smoke --domain-chain'; $reportName = 'ink-smoke.txt' }
if ($Horde) { $argument = '--ink-smoke --horde-smoke'; $reportName = 'ink-smoke.txt' }
if ($Rogue) { $argument = '--ink-smoke --rogue-smoke'; $reportName = 'ink-smoke.txt' }
if ($SceneryPerf) { $argument = '--ink-smoke --scenery-perf'; $reportName = 'ink-smoke.txt' }
try {
    'RUNNING' | Set-Content -LiteralPath (Join-Path $outputPath $reportName)
    $process = Start-Process -FilePath $Executable -ArgumentList $argument -WorkingDirectory $repoPath -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(240000)) { throw 'Chrono smoke test exceeded 240 seconds.' }
    $process.Refresh()
    if ($process.ExitCode -ne 0) { throw "Chrono smoke test exited with code $($process.ExitCode)." }
    $newValidation = @(Get-Content -LiteralPath $validationPath -ErrorAction SilentlyContinue | Select-Object -Skip $previousLines)
    $newValidation | Set-Content -LiteralPath (Join-Path $outputPath 'chrono-d3d12-validation.txt')
    Copy-Item -LiteralPath (Join-Path $repoPath 'error_log.txt') -Destination (Join-Path $outputPath 'chrono-smoke-run.txt')
    $runLog = Get-Content -Raw -LiteralPath (Join-Path $repoPath 'error_log.txt')
    if ($runLog -notmatch 'Chrono validation scene requested' -or $runLog -notmatch 'Shutting down') { throw 'Diagnostic scene or normal shutdown was not reached.' }
    if ($newValidation -match '\[(ERROR|CORRUPTION)\]|Device removed') { throw 'D3D12 validation errors: see tests/out/chrono-d3d12-validation.txt.' }
    $report = Get-Content -Raw (Join-Path $outputPath $reportName)
    if ($report -notmatch '^PASS') { throw $report }
    Write-Output $report
} finally {
    # Stop only the test process we created, never the user's other processes.
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id }
    Copy-Item -LiteralPath (Join-Path $repoPath 'error_log.txt') -Destination (Join-Path $outputPath 'chrono-smoke-run.txt')
    @(Get-Content -LiteralPath $validationPath -ErrorAction SilentlyContinue | Select-Object -Skip $previousLines) | Set-Content -LiteralPath (Join-Path $outputPath 'chrono-d3d12-validation.txt')
    foreach ($name in $logs) {
        $backup = Join-Path $backupPath $name
        if (Test-Path -LiteralPath $backup) { Copy-Item -LiteralPath $backup -Destination (Join-Path $repoPath $name) }
    }
    # Backups are intentionally retained in ignored tests/out for recovery.
}
