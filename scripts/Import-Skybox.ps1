param([string]$DirectXTexLibrary = '')
$ErrorActionPreference = 'Stop'
$taskRepo = Split-Path $PSScriptRoot -Parent
Push-Location $taskRepo
try {
    New-Item -ItemType Directory -Force -Path 'tests/out' | Out-Null
    $taskSource = 'tests/out/kloppenheim_06_puresky_8k.hdr'
    $taskChecksum = '4e853a7134635b333c1bde6d1019f452'
    if (!(Test-Path -LiteralPath $taskSource) -or (Get-FileHash -LiteralPath $taskSource -Algorithm MD5).Hash -ine $taskChecksum) {
        Invoke-WebRequest -Uri 'https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/8k/kloppenheim_06_puresky_8k.hdr' -Headers @{'User-Agent'='neo_Engine-SkyImport/1.0'} -OutFile $taskSource
    }
    if ((Get-FileHash -LiteralPath $taskSource -Algorithm MD5).Hash -ine $taskChecksum) { throw 'HDR source checksum mismatch.' }
    if (!$DirectXTexLibrary) { $DirectXTexLibrary = Join-Path $taskRepo '../Generated/Outputs/Release/DirectXTex.lib' }
    $DirectXTexLibrary = (Resolve-Path -LiteralPath $DirectXTexLibrary).Path
    $taskVS = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$taskVS) { throw 'Visual Studio C++ tools not found.' }
    $taskDev = Join-Path $taskVS 'Common7/Tools/VsDevCmd.bat'
    $taskCommand = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++17 /EHsc /O2 /MT scripts/Convert-Skybox.cpp /Fo:tests/out/Convert-Skybox.obj /Fe:tests/out/Convert-Skybox.exe /link "{1}" d3d11.lib dxgi.lib ole32.lib windowscodecs.lib' -f $taskDev,$DirectXTexLibrary
    & $env:ComSpec /d /s /c $taskCommand
    if ($LASTEXITCODE) { throw 'Skybox converter build failed.' }
    & ./tests/out/Convert-Skybox.exe $taskSource 'Resources/Textures/PolyHaven/kloppenheim_06_puresky_8k_cube.dds'
    if ($LASTEXITCODE) { throw 'Skybox conversion failed.' }
} finally { Pop-Location }
