[CmdletBinding()]
param(
    [ValidateSet('All', 'Engine', 'Game')][string]$Project = 'All',
    [ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Development',
    [string]$File,
    [string]$ToolPath
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/code_quality_common.ps1"

try {
    $analyzer = Find-CodeQualityTool -Name 'clang-tidy.exe' -ToolPath $ToolPath
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio / vswhere.exe was not found.' }
    $msbuild = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.Component.MSBuild -find 'MSBuild\Current\Bin\MSBuild.exe'
    if (-not $msbuild) {
        $msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\Current\Bin\MSBuild.exe'
    }
    if (-not $msbuild) { throw 'MSBuild was not found. Install the Visual Studio C++ workload.' }
    $msbuild = @($msbuild)[0]

    $sourceFile = $null
    if ($File) {
        $sourceFile = @(Get-CodeQualityFiles -Files @($File))[0]
        $relativeSource = $sourceFile.Substring($script:CodeQualityRoot.Length + 1)
        if ($relativeSource -notmatch '^(Engine|Game)[\\/]') {
            throw 'Standalone tests are not part of the Visual Studio projects. Specify an Engine or Game source.'
        }
        if ([IO.Path]::GetExtension($sourceFile) -notin @('.cpp', '.cc', '.cxx', '.c')) {
            throw 'Specify a compiled C/C++ source file. Headers are analyzed through their source files.'
        }
        $owner = if ($sourceFile.StartsWith((Join-Path $script:CodeQualityRoot 'Engine/'), [StringComparison]::OrdinalIgnoreCase)) { 'Engine' } else { 'Game' }
        if ($Project -ne 'All' -and $Project -ne $owner) { throw '-File does not belong to -Project.' }
        $Project = $owner
    }

    $projects = if ($Project -eq 'All') { @('DirectXGameEngine', 'DirectXGameApp') }
                elseif ($Project -eq 'Engine') { @('DirectXGameEngine') }
                else { @('DirectXGameApp') }
    & $analyzer --version
    Push-Location $script:CodeQualityRoot
    try {
        foreach ($projectName in $projects) {
            $outputDirectory = Join-Path $script:CodeQualityRoot "artifacts/code-quality/$Configuration/$projectName/"
            $buildArguments = @(
                "$projectName.vcxproj", '-nologo', '-verbosity:minimal', '-target:PrepareForBuild;ClangTidy',
                "-property:Configuration=$Configuration", '-property:Platform=x64',
                '-property:PreferredToolArchitecture=x64',
                "-property:SolutionDir=$script:CodeQualityRoot\",
                "-property:IntDir=$outputDirectory", "-property:OutDir=$outputDirectory",
                "-property:ClangTidyToolPath=$(Split-Path $analyzer -Parent)",
                '-property:EnableMicrosoftCodeAnalysis=false'
            )
            if ($sourceFile) { $buildArguments += "-property:NeoClangTidyFile=$sourceFile" }
            & $msbuild @buildArguments
            if ($LASTEXITCODE -ne 0) { throw "clang-tidy failed for $projectName (exit $LASTEXITCODE)." }
        }
    } finally {
        Pop-Location
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
