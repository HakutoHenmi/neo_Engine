param([string]$Executable='', [switch]$Native)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
if(!$Executable){$Executable=Join-Path $repoRoot '../Generated/Outputs/Release/DirectXGameApp.exe'}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$runtime=Split-Path $Executable -Parent
if(Get-Process DirectXGameApp -ErrorAction SilentlyContinue){throw 'Close the running game first.'}
$backup=Join-Path $PSScriptRoot ('out/rt-lighting-backup-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($backup)
$logs=@('error_log.txt','d3d12_validation_log.txt','black_screen_log.txt','fluid_debug.txt','run_log.txt')
foreach($name in $logs){$source=Join-Path $repoRoot $name;if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination $backup}}
$validation=Join-Path $repoRoot 'd3d12_validation_log.txt'
$lines=@(Get-Content -LiteralPath $validation -ErrorAction SilentlyContinue).Count
$process=$null
try{
    $arguments='--rt-lighting-smoke --rt-smoke --graphics-validation'
    if(!$Native){$arguments+=' --dlss-smoke'}else{$arguments+=' --native-smoke'}
    $process=Start-Process $Executable -ArgumentList $arguments -WorkingDirectory $runtime -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(240000)){throw 'RT lighting check timed out.'}
    $process.Refresh()
    $report=Get-Content -Raw -LiteralPath (Join-Path $runtime 'RtLightingCheck/result.txt')
    $newValidation=@(Get-Content -LiteralPath $validation | Select-Object -Skip $lines)
    $suffix=if($Native){'native'}else{'dlss'}
    $newValidation | Set-Content (Join-Path $PSScriptRoot "out/rt-lighting-$suffix-validation.txt")
    Copy-Item -LiteralPath (Join-Path $repoRoot 'error_log.txt') -Destination (Join-Path $PSScriptRoot "out/rt-lighting-$suffix-run.txt")
    foreach($name in @('off','reflections','indirect','combined','oblique','settings','scenery')){Copy-Item -LiteralPath (Join-Path $runtime "RtLightingCheck/$name.png") -Destination (Join-Path $PSScriptRoot "out/rt-lighting-$suffix-$name.png")}
    Get-ChildItem -LiteralPath (Join-Path $runtime 'RtLightingCheck') -Filter 'moving-*.png' | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $PSScriptRoot ('out/rt-lighting-'+$suffix+'-'+$_.Name))}
    Get-Content -LiteralPath (Join-Path $runtime 'RtLightingCheck/motion-result.txt') | Tee-Object -FilePath (Join-Path $PSScriptRoot "out/rt-lighting-$suffix-motion.txt")
    $report | Set-Content (Join-Path $PSScriptRoot "out/rt-lighting-$suffix.txt")
    if($process.ExitCode -ne 0 -or $report -notmatch '^PASS'){throw "RT lighting failed: $report"}
    if($newValidation -match '\[(ERROR|CORRUPTION)\]|Device removed'){throw 'D3D12 validation failed.'}
    Write-Output $report
}finally{
    if($process -and !$process.HasExited){Stop-Process -Id $process.Id}
    foreach($name in $logs){$source=Join-Path $backup $name;if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $repoRoot $name)}}
}
