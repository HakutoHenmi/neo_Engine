param([Parameter(Mandatory=$true)][string]$Executable,[switch]$LabOnly,[switch]$TurnOnly,[switch]$MixtureOnly)
$ErrorActionPreference='Stop'
$repoPath=Split-Path $PSScriptRoot -Parent
$outPath=Join-Path $PSScriptRoot 'out'
$backupPath=Join-Path $outPath ('slime-life-log-backup-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $backupPath | Out-Null
$logs=@('error_log.txt','d3d12_validation_log.txt','black_screen_log.txt')
foreach($name in $logs){$path=Join-Path $repoPath $name;if(Test-Path -LiteralPath $path){Copy-Item -LiteralPath $path -Destination (Join-Path $backupPath $name)}}
$process=$null
$validationBefore=@(Get-Content -LiteralPath (Join-Path $repoPath 'd3d12_validation_log.txt') -ErrorAction SilentlyContinue).Count
$runtimeOutPath=Join-Path (Split-Path (Resolve-Path -LiteralPath $Executable).Path -Parent) 'tests/out'
New-Item -ItemType Directory -Force $runtimeOutPath | Out-Null
try {
    $arguments=@('--slime-life-smoke');if($LabOnly){$arguments+='--slime-life-lab-only'}
    if($TurnOnly){$arguments+='--slime-life-turn-only'}
    if($MixtureOnly){$arguments+='--slime-life-mixture-only'}
    $process=Start-Process -FilePath (Resolve-Path -LiteralPath $Executable).Path -ArgumentList $arguments -WorkingDirectory $repoPath -WindowStyle Hidden -PassThru
    while(-not $process.WaitForExit(1000)){
        if((Get-Date)-$process.StartTime -gt [TimeSpan]::FromSeconds(240)){throw 'Slime life smoke test exceeded 240 seconds'}
    }
    Copy-Item -LiteralPath (Join-Path $repoPath 'error_log.txt') -Destination (Join-Path $outPath 'slime-life-run.txt')
    $newValidation=@(Get-Content -LiteralPath (Join-Path $repoPath 'd3d12_validation_log.txt') | Select-Object -Skip $validationBefore)
    $newValidation | Set-Content -LiteralPath (Join-Path $outPath 'slime-life-d3d12.txt')
    if((Test-Path -LiteralPath (Join-Path $runtimeOutPath 'slime-life-smoke.txt')) -and [IO.Path]::GetFullPath($runtimeOutPath) -ne [IO.Path]::GetFullPath($outPath)){
        Get-ChildItem -LiteralPath $runtimeOutPath -Filter 'slime-life-*' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $outPath $_.Name) }
    }
    $report=Get-Content -LiteralPath (Join-Path $outPath 'slime-life-smoke.txt') -Raw
    if($process.ExitCode -ne 0 -or $report -notmatch '^PASS'){throw "Slime life smoke failed: $report"}
    if(($newValidation -join "`n") -match '\[(ERROR|CORRUPTION)\]|Device removed'){throw 'Slime life D3D12 error: see tests/out/slime-life-d3d12.txt'}
    Write-Output $report
} finally {
    if($process -and -not $process.HasExited){Stop-Process -Id $process.Id}
    foreach($name in $logs){$backup=Join-Path $backupPath $name;if(Test-Path -LiteralPath $backup){Copy-Item -LiteralPath $backup -Destination (Join-Path $repoPath $name)}}
}
