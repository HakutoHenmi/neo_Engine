param([ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Development')
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
Push-Location $repoPath
try {
    $settings = & "$PSScriptRoot/Get-CppTestSettings.ps1" -Configuration $Configuration
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vsPath = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsPath) { throw 'Visual Studio C++ tools not found.' }
    $devCmd = Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
    $command = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /utf-8 /std:c++20 /EHsc /W4 /WX {1} tests/SlimeTuningTests.cpp /Fo:{2}/SlimeTuningTests.obj /Fe:{2}/SlimeTuningTests.exe' -f $devCmd, $settings.Flags, $settings.Output
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE) { throw 'Slime tuning test build failed.' }
    & "./$($settings.Output)/SlimeTuningTests.exe"
    if ($LASTEXITCODE) { throw 'Slime tuning tests failed.' }
} finally { Pop-Location }
