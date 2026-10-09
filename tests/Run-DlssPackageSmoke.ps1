param([string]$Executable='')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
if(!$Executable){$Executable=Join-Path $repoRoot '../Generated/Outputs/Release/DirectXGameApp.exe'}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$runtime=Split-Path $Executable -Parent
if(Get-Process DirectXGameApp -ErrorAction SilentlyContinue){throw 'Close the running game first.'}
$backup=Join-Path $PSScriptRoot ('out/dlss-package-backup-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($backup)
$logs=@('error_log.txt','d3d12_validation_log.txt','black_screen_log.txt','fluid_debug.txt','run_log.txt')
foreach($name in $logs){$source=Join-Path $repoRoot $name;if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination $backup}}
$validation=Join-Path $repoRoot 'd3d12_validation_log.txt'
$lines=@(Get-Content -LiteralPath $validation -ErrorAction SilentlyContinue).Count
$process=$null
try{
    # Release deliberately changes CWD to its own directory. DLLs and resources
    # must therefore be found in this package, not the source checkout.
    $process=Start-Process $Executable -ArgumentList '--dlss-package-smoke --dlss-smoke --graphics-validation' -WorkingDirectory $runtime -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(95000)){throw 'Release DLSS check timed out.'}
    $process.Refresh()
    $report=Get-Content -Raw -LiteralPath (Join-Path $runtime 'DlssCheck/result.txt')
    $newValidation=@(Get-Content -LiteralPath $validation | Select-Object -Skip $lines)
    $newValidation | Set-Content (Join-Path $PSScriptRoot 'out/dlss-package-validation.txt')
    if($process.ExitCode -ne 0 -or $report -notmatch '^PASS'){throw "Release DLSS failed: $report"}
    if($newValidation -match '\[(ERROR|CORRUPTION)\]|Device removed'){throw 'D3D12 validation failed.'}
    Copy-Item -LiteralPath (Join-Path $runtime 'DlssCheck/settings.png') -Destination (Join-Path $PSScriptRoot 'out/dlss-release-settings.png')
    Copy-Item -LiteralPath (Join-Path $runtime 'DlssCheck/game.png') -Destination (Join-Path $PSScriptRoot 'out/dlss-release-game.png')
    $report | Set-Content (Join-Path $PSScriptRoot 'out/dlss-package-smoke.txt')
    Write-Output $report
}finally{
    if($process -and !$process.HasExited){Stop-Process -Id $process.Id}
    foreach($name in $logs){$source=Join-Path $backup $name;if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $repoRoot $name)}}
}
