param([string]$Executable = 'C:/Users/k024g/source/repos/Generated/Outputs/Release/DirectXGameApp.exe')
$ErrorActionPreference = 'Stop'
if (Get-Process -Name DirectXGameApp -ErrorAction SilentlyContinue) { throw 'A game is already running; it will not be interrupted.' }
$packageExecutable = (Resolve-Path -LiteralPath $Executable).Path
$packageDirectory = Split-Path $packageExecutable -Parent
$packageOutput = Join-Path $PSScriptRoot 'out'
$packageBackup = Join-Path $packageOutput ('package-backup-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $packageBackup | Out-Null
$packageLogDirectory=Split-Path $PSScriptRoot -Parent
$packageValidationPath=Join-Path $packageLogDirectory 'd3d12_validation_log.txt'
$packagePreviousLines=if(Test-Path -LiteralPath $packageValidationPath){@(Get-Content -LiteralPath $packageValidationPath).Count}else{0}
$packageLogs = @('error_log.txt','d3d12_validation_log.txt','black_screen_log.txt','run_log.txt','fluid_debug.txt')
foreach ($name in $packageLogs) {
    $path = Join-Path $packageLogDirectory $name
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $packageBackup $name) }
}
$packageProcess = $null
try {
    $packageProcess = Start-Process -FilePath $packageExecutable -ArgumentList '--package-smoke' -WorkingDirectory $packageDirectory -WindowStyle Hidden -PassThru
    if (!$packageProcess.WaitForExit(60000)) { throw 'Release package validation exceeded 60 seconds.' }
    if ($packageProcess.ExitCode -ne 0) { throw "Release validation exited with code $($packageProcess.ExitCode)." }
    $report = Get-Content -Raw -LiteralPath (Join-Path $packageDirectory 'PackageCheck/result.txt')
    if ($report -notmatch '^PASS') { throw $report }
    $validation = @(Get-Content -LiteralPath $packageValidationPath -ErrorAction SilentlyContinue | Select-Object -Skip $packagePreviousLines)
    if ($validation -match '\[(ERROR|CORRUPTION)\]|Device removed') { throw 'D3D12 validation reported an error.' }
    Write-Output $report
} finally {
    if ($packageProcess -and !$packageProcess.HasExited) { Stop-Process -Id $packageProcess.Id }
    foreach ($name in $packageLogs) {
        $path = Join-Path $packageLogDirectory $name
        if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $packageOutput ('release-rogue-' + $name)) }
        $backup = Join-Path $packageBackup $name
        if (Test-Path -LiteralPath $backup) { Copy-Item -LiteralPath $backup -Destination $path }
    }
}
