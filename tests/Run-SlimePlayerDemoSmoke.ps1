param([string]$Executable='', [switch]$CameraOnly, [switch]$FarWater, [switch]$NearWater)
$ErrorActionPreference='Stop'
if($NearWater){$FarWater=$true}
$repoPath=Split-Path $PSScriptRoot -Parent
if(!$Executable){$Executable=Join-Path $repoPath '../Generated/Outputs/Development/DirectXGameApp.exe'}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
if(Get-Process DirectXGameApp -ErrorAction SilentlyContinue){throw 'Close the running game first.'}
$outPath=Join-Path $PSScriptRoot 'out'
$backupPath=Join-Path $outPath ('demo-backup-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $backupPath | Out-Null
$logs=@('error_log.txt','d3d12_validation_log.txt','black_screen_log.txt','fluid_debug.txt')
foreach($name in $logs){$source=Join-Path $repoPath $name;if(Test-Path $source){Copy-Item -LiteralPath $source -Destination $backupPath}}
$validation=Join-Path $repoPath 'd3d12_validation_log.txt'
$previousLines=@(Get-Content $validation -ErrorAction SilentlyContinue).Count
$reportName=if($FarWater){'far-water-demo-smoke.txt'}elseif($CameraOnly){'stopped-camera-smoke.txt'}else{'slime-player-demo-smoke.txt'}
$reportPath=Join-Path $outPath $reportName
'RUNNING' | Set-Content $reportPath
$process=$null
try {
    $argument=if($FarWater){'--far-water-demo-smoke'}elseif($CameraOnly){'--stopped-camera-check'}else{'--slime-player-demo-smoke'}
    if($NearWater){$argument+=' --near-water-check'}
    $process=Start-Process $Executable -ArgumentList $argument -WorkingDirectory $repoPath -WindowStyle Hidden -PassThru
    while(!$process.WaitForExit(1000)){if((Get-Date)-$process.StartTime -gt [TimeSpan]::FromSeconds(150)){throw 'Slime player demo smoke timed out.'}}
    $newValidation=@(Get-Content $validation -ErrorAction SilentlyContinue | Select-Object -Skip $previousLines)
    $newValidation | Set-Content (Join-Path $outPath 'slime-demo-validation.txt')
    $report=Get-Content -Raw $reportPath
    if($process.ExitCode -ne 0 -or $report -notmatch '^PASS'){throw "Slime player demo smoke failed (exit $($process.ExitCode)): $report"}
    if($newValidation -match '\[(ERROR|CORRUPTION)\]|Device removed'){throw 'Slime player demo D3D12 validation failed.'}
    $report
} finally {
    if($process -and !$process.HasExited){Stop-Process -Id $process.Id}
    Copy-Item (Join-Path $repoPath 'error_log.txt') (Join-Path $outPath 'slime-demo-run.txt')
    foreach($name in $logs){$backup=Join-Path $backupPath $name;if(Test-Path $backup){Copy-Item -LiteralPath $backup -Destination (Join-Path $repoPath $name)}}
}
