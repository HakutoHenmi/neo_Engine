$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
Push-Location $repoPath
try {
    New-Item -ItemType Directory -Force -Path 'tests/out' | Out-Null
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsPath) { throw 'Visual Studio C++ tools not found.' }
    $devCmd = Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
    $command = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /utf-8 /std:c++20 /EHsc /W4 /WX /MDd tests/DesignPatternTests.cpp /Fo:tests/out/DesignPatternTests.obj /Fe:tests/out/DesignPatternTests.exe' -f $devCmd
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE) { throw 'Design pattern test build failed.' }
    & './tests/out/DesignPatternTests.exe'
    if ($LASTEXITCODE) { throw 'Design pattern tests failed.' }
} finally { Pop-Location }
