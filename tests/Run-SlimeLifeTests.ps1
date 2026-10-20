$ErrorActionPreference='Stop'
$repoPath=Split-Path $PSScriptRoot -Parent
Push-Location $repoPath
try {
    New-Item -ItemType Directory -Force tests/out | Out-Null
    $vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $devCmd=Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
    $command='call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX tests/SlimeLifeRulesTests.cpp /Fo:tests/out/SlimeLifeRulesTests.obj /Fe:tests/out/SlimeLifeRulesTests.exe' -f $devCmd
    & $env:ComSpec /d /s /c $command
    if($LASTEXITCODE){throw 'Slime life rule test build failed'}
    & ./tests/out/SlimeLifeRulesTests.exe
    if($LASTEXITCODE){throw 'Slime life rule tests failed'}
    & ./tests/Run-FluidTests.ps1 -Case '--morph-only'
    if($LASTEXITCODE){throw 'Point-cloud GPU tests failed'}
    & ./tests/Run-FluidTests.ps1 -Case '--surface-only'
    if($LASTEXITCODE){throw 'Fluid surface rendering tests failed'}
} finally {Pop-Location}
