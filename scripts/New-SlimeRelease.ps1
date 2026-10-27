param([string]$Executable = '')
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe = if ($Executable) { (Resolve-Path -LiteralPath $Executable).Path } else { [IO.Path]::GetFullPath((Join-Path $repoRoot '../Generated/Outputs/Release/DirectXGameApp.exe')) }
if (!(Test-Path -LiteralPath $exe)) { throw 'Build Release|x64 first.' }
$bundle = Join-Path $repoRoot 'dist/SlimeAssault'
$zip = Join-Path $repoRoot 'dist/SlimeAssault-Release.zip'
[void][IO.Directory]::CreateDirectory($bundle)
& (Join-Path $PSScriptRoot 'Package-SlimeResources.ps1') -Destination (Join-Path $bundle 'Resources')
Copy-Item -LiteralPath $exe -Destination (Join-Path $bundle 'SlimeAssault.exe') -Force
& (Join-Path $PSScriptRoot 'Package-Streamline.ps1') -Destination (Join-Path $bundle 'Streamline')
& (Join-Path $PSScriptRoot 'Package-Ntc.ps1') -Destination (Join-Path $bundle 'NTC')
# Pre-compile the shaders used from startup through the first Game scene into Resources/ShaderCache,
# so a first launch on another PC skips ~16 s of runtime HLSL compilation. The bundled EXE itself
# drives Title -> Select -> Game without input and exits; keys include every source, so stale entries are never used.
$cache = Join-Path $bundle 'Resources/ShaderCache'
if (Test-Path -LiteralPath $cache) { Remove-Item -LiteralPath $cache -Recurse -Force }
$warm = Start-Process -FilePath (Join-Path $bundle 'SlimeAssault.exe') -ArgumentList '--load-profile-auto --write-shader-cache' -WorkingDirectory $bundle -PassThru
if (!$warm.WaitForExit(300000)) { Stop-Process -Id $warm.Id -Force; throw 'Shader cache warm-up timed out.' }
if ($warm.ExitCode -ne 0 -or @(Get-ChildItem -LiteralPath $cache -Filter '*.cso' -ErrorAction SilentlyContinue).Count -lt 50) { throw 'Shader cache warm-up failed.' }
Remove-Item -LiteralPath (Join-Path $bundle 'load-profile.txt') -Force -ErrorAction SilentlyContinue
$content=@((Join-Path $bundle 'SlimeAssault.exe'),(Join-Path $bundle 'Resources'),(Join-Path $bundle 'NTC'))
if(Test-Path -LiteralPath (Join-Path $bundle 'Streamline')){$content+=Join-Path $bundle 'Streamline'}
Compress-Archive -LiteralPath $content -DestinationPath $zip -Force
Write-Output $zip
