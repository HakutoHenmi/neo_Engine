$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
Push-Location $repoPath
try {
    New-Item -ItemType Directory -Force -Path 'tests/out' | Out-Null
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsPath) { throw 'Visual Studio C++ tools not found.' }
    $devCmd = Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
    $command = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++17 /EHsc /W4 /WX tests/ChronoRulesTests.cpp /Fo:tests/out/ChronoRulesTests.obj /Fe:tests/out/ChronoRulesTests.exe' -f $devCmd
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE) { throw 'Chrono rule test build failed.' }
    & './tests/out/ChronoRulesTests.exe'
    if ($LASTEXITCODE) { throw 'Chrono rule tests failed.' }
} finally { Pop-Location }
