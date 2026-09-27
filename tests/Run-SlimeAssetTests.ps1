$ErrorActionPreference='Stop'
$repoPath=Split-Path $PSScriptRoot -Parent
Push-Location $repoPath
$previousPath=$env:PATH
try {
    New-Item -ItemType Directory -Force tests/out | Out-Null
    $vsPath=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(!$vsPath){throw 'Visual Studio C++ tools not found.'}
    $devCmd=Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
    $command='call "{0}" -arch=x64 >nul && cl /nologo /EHsc /std:c++17 /Iexternals/assimp/include tests/SlimeAssetTests.cpp /Fo:tests/out/SlimeAssetTests.obj /Fe:tests/out/SlimeAssetTests.exe /link externals/assimp/lib/Release/assimp-vc143-mt.lib' -f $devCmd
    & $env:ComSpec /d /s /c $command
    if($LASTEXITCODE){throw 'Asset test build failed.'}
    $env:PATH=(Join-Path $repoPath '../Generated/Outputs/Development')+';'+(Join-Path $repoPath 'externals/assimp/bin/Release')+';'+$env:PATH
    & ./tests/out/SlimeAssetTests.exe
    if($LASTEXITCODE){throw 'Asset test failed.'}
} finally {$env:PATH=$previousPath;Pop-Location}
