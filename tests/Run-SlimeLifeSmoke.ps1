param([Parameter(Mandatory=$true)][string]$Executable,[switch]$LabOnly,[switch]$TurnOnly,[switch]$MixtureOnly,[switch]$StressOnly,[switch]$MotionOnly,[switch]$AbsorbOnly,[switch]$ProgressionOnly,[switch]$CameraOnly,[switch]$CityTour,[switch]$TireOnly,[switch]$HydrantOnly,[switch]$AimOnly,[switch]$CrewOnly,[switch]$ShowcaseOnly,[switch]$AlleyOnly,[switch]$SignalOnly,[switch]$IntroOnly,[switch]$MorphCameraOnly)
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
$reportName=if($MorphCameraOnly){'slime-life-morph-camera.txt'}elseif($IntroOnly){'slime-life-intro.txt'}elseif($SignalOnly){'slime-life-signal.txt'}elseif($AlleyOnly){'slime-life-alley.txt'}elseif($ShowcaseOnly){'slime-life-showcase.txt'}elseif($CrewOnly){'slime-life-crew.txt'}elseif($AimOnly){'slime-life-aim.txt'}elseif($HydrantOnly){'slime-life-hydrant.txt'}elseif($TireOnly){'slime-life-tire.txt'}elseif($CameraOnly){'slime-life-camera.txt'}elseif($ProgressionOnly){'slime-life-progression.txt'}elseif($AbsorbOnly){'slime-life-absorb.txt'}elseif($MotionOnly){'slime-life-motion.txt'}elseif($StressOnly){'slime-life-stress.txt'}else{'slime-life-smoke.txt'}
$testStarted=Get-Date
try {
    $arguments=@('--slime-life-smoke');if($LabOnly){$arguments+='--slime-life-lab-only'}
    if($TurnOnly){$arguments+='--slime-life-turn-only'}
    if($MixtureOnly){$arguments+='--slime-life-mixture-only'}
    if($StressOnly){$arguments+='--slime-life-stress'}
    if($MotionOnly){$arguments+='--slime-life-motion'}
    if($AbsorbOnly){$arguments+='--slime-life-absorb-only'}
    if($CameraOnly){$arguments+='--slime-life-camera'}
    if($ProgressionOnly){$arguments+='--slime-life-progression'}
    if($CityTour){$arguments+='--slime-city-tour'}
    if($TireOnly){$arguments+='--slime-life-tire'}
    if($HydrantOnly){$arguments+='--slime-life-hydrant';$arguments+='--graphics-validation'}
    if($AimOnly){$arguments+='--slime-life-aim';$arguments+='--graphics-validation'}
    if($CrewOnly){$arguments+='--slime-life-crew'}
    if($ShowcaseOnly){$arguments+='--slime-life-showcase'}
    if($AlleyOnly){$arguments+='--slime-life-alley'}
    if($SignalOnly){$arguments+='--slime-life-signal'}
    if($IntroOnly){$arguments+='--slime-life-intro'}
    if($MorphCameraOnly){$arguments+='--slime-life-morph-camera'}
    $process=Start-Process -FilePath (Resolve-Path -LiteralPath $Executable).Path -ArgumentList $arguments -WorkingDirectory $repoPath -WindowStyle Hidden -PassThru
    while(-not $process.WaitForExit(1000)){
        if((Get-Date)-$process.StartTime -gt [TimeSpan]::FromSeconds(240)){throw 'Slime life smoke test exceeded 240 seconds'}
    }
    Copy-Item -LiteralPath (Join-Path $repoPath 'error_log.txt') -Destination (Join-Path $outPath 'slime-life-run.txt')
    $newValidation=@(Get-Content -LiteralPath (Join-Path $repoPath 'd3d12_validation_log.txt') | Select-Object -Skip $validationBefore)
    $newValidation | Set-Content -LiteralPath (Join-Path $outPath 'slime-life-d3d12.txt')
    if((Test-Path -LiteralPath (Join-Path $runtimeOutPath $reportName)) -and [IO.Path]::GetFullPath($runtimeOutPath) -ne [IO.Path]::GetFullPath($outPath) -and (Get-Item -LiteralPath (Join-Path $runtimeOutPath $reportName)).LastWriteTime -ge $testStarted){
        Get-ChildItem -LiteralPath $runtimeOutPath -Filter 'slime-life-*' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $outPath $_.Name) }
    }
    $reportPath=Join-Path $outPath $reportName
    if(!(Test-Path -LiteralPath $reportPath) -or (Get-Item -LiteralPath $reportPath).LastWriteTime -lt $testStarted){throw "No fresh report for $reportName"}
    $report=Get-Content -LiteralPath $reportPath -Raw
    if($process.ExitCode -ne 0 -or $report -notmatch '(?m)^PASS'){throw "Slime life smoke failed: $report"}
    if(($newValidation -join "`n") -match '\[(ERROR|CORRUPTION)\]|Device removed'){throw 'Slime life D3D12 error: see tests/out/slime-life-d3d12.txt'}
    Write-Output $report
} finally {
    if($process -and -not $process.HasExited){Stop-Process -Id $process.Id}
    foreach($name in $logs){$backup=Join-Path $backupPath $name;if(Test-Path -LiteralPath $backup){Copy-Item -LiteralPath $backup -Destination (Join-Path $repoPath $name)}}
}
