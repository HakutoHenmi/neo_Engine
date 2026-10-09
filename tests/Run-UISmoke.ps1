param([string]$Executable = '', [switch]$Boss, [switch]$Dodge, [switch]$BeamCamera, [switch]$Graphics, [switch]$Shadows, [switch]$Oblique)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
if (!$Executable) { $Executable = Join-Path $repoPath '../Generated/Outputs/Development/DirectXGameApp.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (Get-Process DirectXGameApp -ErrorAction SilentlyContinue) { throw 'Close the running game first.' }
$outputPath = Join-Path $PSScriptRoot 'out'
$backupPath = Join-Path $outputPath ('ui-backup-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $backupPath | Out-Null
$logs = @('error_log.txt','d3d12_validation_log.txt','black_screen_log.txt','fluid_debug.txt','run_log.txt')
foreach ($name in $logs) {
    $source = Join-Path $repoPath $name
    if (Test-Path $source) { Copy-Item -LiteralPath $source -Destination $backupPath }
}
$validation = Join-Path $repoPath 'd3d12_validation_log.txt'
$previousLines = @(Get-Content $validation -ErrorAction SilentlyContinue).Count
$process = $null
try {
    'RUNNING' | Set-Content (Join-Path $outputPath 'ui-smoke.txt')
    $argument = if ($Shadows) { '--shadow-band-smoke --dlss-smoke --rt-smoke --graphics-validation' } elseif ($Graphics) { '--graphics-ui-smoke' } elseif ($BeamCamera) { '--beam-camera-smoke' } elseif ($Dodge) { '--dodge-smoke' } elseif ($Boss) { '--boss-smoke' } else { '--ui-smoke' }
    if($Oblique){$argument+=' --shadow-oblique'}
    $process = Start-Process $Executable -ArgumentList $argument -WorkingDirectory $repoPath -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(95000)) { throw 'UI smoke timed out.' }
    $process.Refresh()
    $report = Get-Content -Raw (Join-Path $outputPath 'ui-smoke.txt')
    $newValidation = @(Get-Content $validation -ErrorAction SilentlyContinue | Select-Object -Skip $previousLines)
    $newValidation | Set-Content (Join-Path $outputPath 'ui-validation.txt')
    if ($newValidation -match '\[(ERROR|CORRUPTION)\]|Device removed') { throw 'D3D12 validation failed.' }
    if ($process.ExitCode -ne 0 -or $report -notmatch '^PASS') { throw "UI smoke failed: $report" }
    Write-Output $report
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id }
    Copy-Item (Join-Path $repoPath 'error_log.txt') (Join-Path $outputPath 'ui-run.txt')
    foreach ($name in $logs) {
        $backup = Join-Path $backupPath $name
        if (Test-Path $backup) { Copy-Item -LiteralPath $backup -Destination (Join-Path $repoPath $name) }
    }
}
