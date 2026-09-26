param([ValidateSet('', '--chrono-only', '--volume-only', '--reversal-only', '--liquefy-only')][string]$Case = '')
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
Push-Location $repoPath
try {
    New-Item -ItemType Directory -Force -Path 'tests/out' | Out-Null
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsPath) { throw 'Visual Studio C++ tools not found.' }
    $devCmd = Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
    $buildCommand = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++17 /EHsc /O2 /W4 tests/FluidGpuTests.cpp /Fo:tests/out/FluidGpuTests.obj /Fe:tests/out/FluidGpuTests.exe /link d3d11.lib d3dcompiler.lib windowscodecs.lib ole32.lib' -f $devCmd
    & $env:ComSpec /d /s /c $buildCommand
    if ($LASTEXITCODE) { throw 'Fluid GPU test build failed.' }
    if ($Case) { & './tests/out/FluidGpuTests.exe' $Case } else { & './tests/out/FluidGpuTests.exe' }
    if ($LASTEXITCODE) { throw 'Fluid GPU regression failed.' }
} finally { Pop-Location }
