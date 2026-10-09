$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe = [IO.Path]::GetFullPath((Join-Path $repoRoot '../Generated/Outputs/Release/DirectXGameApp.exe'))
if (!(Test-Path -LiteralPath $exe)) { throw 'Build Release|x64 first.' }
$bundle = Join-Path $repoRoot 'dist/SlimeAssault'
$zip = Join-Path $repoRoot 'dist/SlimeAssault-Release.zip'
[void][IO.Directory]::CreateDirectory($bundle)
& (Join-Path $PSScriptRoot 'Package-SlimeResources.ps1') -Destination (Join-Path $bundle 'Resources')
Copy-Item -LiteralPath $exe -Destination (Join-Path $bundle 'SlimeAssault.exe') -Force
& (Join-Path $PSScriptRoot 'Package-Streamline.ps1') -Destination (Join-Path $bundle 'Streamline')
$content=@((Join-Path $bundle 'SlimeAssault.exe'),(Join-Path $bundle 'Resources'))
if(Test-Path -LiteralPath (Join-Path $bundle 'Streamline')){$content+=Join-Path $bundle 'Streamline'}
Compress-Archive -LiteralPath $content -DestinationPath $zip -Force
Write-Output $zip
