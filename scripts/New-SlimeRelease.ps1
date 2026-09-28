$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe = [IO.Path]::GetFullPath((Join-Path $repoRoot '../Generated/Outputs/Release/DirectXGameApp.exe'))
if (!(Test-Path -LiteralPath $exe)) { throw 'Build Release|x64 first.' }
$bundle = Join-Path $repoRoot 'dist/SlimeAssault'
$zip = Join-Path $repoRoot 'dist/SlimeAssault-Release.zip'
[void][IO.Directory]::CreateDirectory($bundle)
& (Join-Path $PSScriptRoot 'Package-SlimeResources.ps1') -Destination (Join-Path $bundle 'Resources')
Copy-Item -LiteralPath $exe -Destination (Join-Path $bundle 'SlimeAssault.exe') -Force
Compress-Archive -LiteralPath (Join-Path $bundle 'SlimeAssault.exe'),(Join-Path $bundle 'Resources') -DestinationPath $zip -Force
Write-Output $zip
