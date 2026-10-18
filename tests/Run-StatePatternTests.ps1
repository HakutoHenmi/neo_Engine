param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
Push-Location $repoPath
try {
    # Keep all generated files inside this checkout. Do not copy game resources
    # or launch the interactive application during these CPU regression tests.
    $buildPath = Join-Path $repoPath 'build/state-pattern'
    New-Item -ItemType Directory -Force -Path $buildPath, 'tests/out' | Out-Null
    $propsPath = Join-Path $buildPath 'Build.props'
    @'
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <OutDir>$(MSBuildThisFileDirectory)bin\$(Configuration)\</OutDir>
    <IntDir>$(MSBuildThisFileDirectory)obj\$(MSBuildProjectName)\$(Configuration)\</IntDir>
    <PostBuildEventUseInBuild>false</PostBuildEventUseInBuild>
    <TrackFileAccess>false</TrackFileAccess>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <DebugInformationFormat>OldStyle</DebugInformationFormat>
      <MultiProcessorCompilation>false</MultiProcessorCompilation>
    </ClCompile>
    <Link><GenerateDebugInformation>false</GenerateDebugInformation></Link>
  </ItemDefinitionGroup>
</Project>
'@ | Set-Content -LiteralPath $propsPath -Encoding utf8
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vsPath = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsPath) { throw 'Visual Studio C++ tools not found.' }
    $devCmd = Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
    $buildCommand = 'call "{0}" -arch=x64 -host_arch=x64 >nul && msbuild DirectXGame_New.sln /nologo /m:1 /nr:false /verbosity:minimal /p:Configuration=Debug /p:Platform=x64 /p:ForceImportBeforeCppTargets="{1}" /fl "/flp:logfile=build/state-pattern/build-debug.log;verbosity=normal;encoding=UTF-8"' -f $devCmd, $propsPath
    if (!$SkipBuild) {
        & $env:ComSpec /d /s /c $buildCommand
        if ($LASTEXITCODE) { throw 'State pattern application build failed.' }
    }

    # Link the actual game implementations (excluding WinMain), rather than
    # substituting rendering/scene mocks for the state and combat code.
    $objects = @(Get-ChildItem (Join-Path $buildPath 'obj/DirectXGameApp/Debug') -Filter '*.obj' | Where-Object Name -ne 'main.obj')
    if (!$objects.Count) { throw 'Game object files not found.' }
    $arguments = @(
        '/nologo', '/utf-8', '/std:c++20', '/EHsc', '/W4', '/WX', '/MTd', '/D_DEBUG', '/DUSE_IMGUI', '/D_UNICODE', '/DUNICODE',
        '/I Engine', '/I Engine/ThirdParty', '/I Game', '/I Game/Scenes', '/I Game/Editor', '/I externals/imgui', '/I externals/DirectXTex',
        'tests/StatePatternTests.cpp', '/Fo:tests/out/StatePatternTests.obj', '/Fe:tests/out/StatePatternTests.exe'
    )
    $arguments += $objects | ForEach-Object { '"{0}"' -f $_.FullName }
    $arguments += @(
        ('"{0}"' -f (Join-Path $buildPath 'bin/Debug/DirectXGameEngine.lib')),
        ('"{0}"' -f (Join-Path $buildPath 'bin/Debug/DirectXTex.lib')),
        'externals/assimp/lib/Debug/assimp-vc143-mtd.lib',
        'comdlg32.lib',
        '/link /SUBSYSTEM:CONSOLE /INCREMENTAL:NO /IGNORE:4099'
    )
    $arguments | Set-Content -Encoding utf8 'tests/out/StatePatternTests.rsp'
    $testCommand = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl @tests/out/StatePatternTests.rsp' -f $devCmd
    & $env:ComSpec /d /s /c $testCommand
    if ($LASTEXITCODE) { throw 'State pattern test build failed.' }
    & './tests/out/StatePatternTests.exe'
    if ($LASTEXITCODE) { throw 'State pattern regression failed.' }
} finally { Pop-Location }
