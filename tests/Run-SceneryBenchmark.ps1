param([string]$Label='current',[switch]$Dlss,[switch]$Validation,[switch]$Baseline)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$runtime=[IO.Path]::GetFullPath((Join-Path $repoRoot '../Generated/Outputs/Release'))
$exe=Join-Path $runtime 'DirectXGameApp.exe'
if(Get-Process DirectXGameApp -ErrorAction SilentlyContinue){throw 'Close the running game first.'}
$backup=Join-Path $PSScriptRoot ('out/scenery-backup-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($backup)
$logs=@('error_log.txt','d3d12_validation_log.txt','black_screen_log.txt','fluid_debug.txt','run_log.txt')
foreach($name in $logs){$path=Join-Path $repoRoot $name;if(Test-Path -LiteralPath $path){Copy-Item -LiteralPath $path -Destination $backup}}
$validationPath=Join-Path $repoRoot 'd3d12_validation_log.txt'
$validationLines=@(Get-Content -LiteralPath $validationPath -ErrorAction SilentlyContinue).Count
$process=$null
try{
    $arguments='--scenery-benchmark';if($Dlss){$arguments+=' --dlss-smoke'}else{$arguments+=' --native-smoke'}
    if($Validation){$arguments+=' --graphics-validation'}
    if($Baseline){$arguments+=' --scenery-baseline'}
    $process=Start-Process $exe -WorkingDirectory $runtime -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(300000)){throw 'Scenery benchmark timed out.'}
    $result=Get-Content -Raw -LiteralPath (Join-Path $runtime 'SceneryBenchmark/result.txt')
    $result | Set-Content -LiteralPath (Join-Path $PSScriptRoot "out/scenery-$Label.txt")
    $newValidation=@(Get-Content -LiteralPath $validationPath -ErrorAction SilentlyContinue | Select-Object -Skip $validationLines)
    $newValidation | Set-Content -LiteralPath (Join-Path $PSScriptRoot "out/scenery-$Label-validation.txt")
    Copy-Item -LiteralPath (Join-Path $repoRoot 'error_log.txt') -Destination (Join-Path $PSScriptRoot "out/scenery-$Label-run.txt")
    $result
    if($process.ExitCode -ne 0 -or $result -notmatch 'PASS'){throw 'Scenery benchmark failed.'}
    if($Validation -and ($newValidation -match '\[(ERROR|CORRUPTION)\]|Device removed')){throw 'Scenery D3D12 validation failed.'}
}finally{
    if($process -and !$process.HasExited){Stop-Process -Id $process.Id}
    foreach($name in $logs){$path=Join-Path $backup $name;if(Test-Path -LiteralPath $path){Copy-Item -LiteralPath $path -Destination (Join-Path $repoRoot $name)}}
}
