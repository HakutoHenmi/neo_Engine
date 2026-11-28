[CmdletBinding()]
param(
    [ValidateSet('All', 'Build', 'Tests', 'Package')][string]$Stage = 'All',
    [ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Development'
)

$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$logs = Join-Path $repoRoot "artifacts/ci/logs/$Configuration"
New-Item -ItemType Directory -Force $logs | Out-Null

Push-Location $repoRoot
try {
    if ($Stage -in @('All', 'Build')) {
        $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
        $msbuild = @(& $vswhere -latest -products '*' -version '[17.0,18.0)' `
            -requires Microsoft.Component.MSBuild -find 'MSBuild/Current/Bin/amd64/MSBuild.exe')[0]
        if (-not $msbuild) { throw 'Visual Studio 2022 MSBuild was not found.' }
        $assimp = if ($Configuration -eq 'Debug') { 'assimp-Debug' } else { 'assimp' }
        if (-not (Test-Path "artifacts/ci/deps/$assimp/source.sha256")) {
            throw "Run scripts/ci/bootstrap_dependencies.ps1 -Configuration $Configuration before building."
        }
        & $msbuild DirectXGame_New.sln -nologo -m:2 -nodeReuse:false -verbosity:minimal `
            "-p:Configuration=$Configuration" -p:Platform=x64 -p:PreferredToolArchitecture=x64 -p:CL_MPCount=2 `
            -p:EnableCodeQualityLogging=false -p:RunCodeAnalysis=false `
            "-p:ForceImportBeforeCppTargets=$PSScriptRoot/build.props" "-bl:$logs/build.binlog" 2>&1 |
            Tee-Object -FilePath "$logs/build.log"
        if ($LASTEXITCODE -ne 0) { throw "CI solution build failed. See $logs/build.log." }
        if (-not (Test-Path "artifacts/ci/bin/$Configuration/DirectXGameApp.exe")) {
            throw 'The solution build did not produce DirectXGameApp.exe.'
        }
    }

    if ($Stage -in @('All', 'Tests')) {
        $suites = @(
            'Chrono', 'DataDriven', 'DesignPattern', 'Encapsulation', 'ControllerMenu',
            'SlimeTuning', 'WeaponShape', 'CodeQuality', 'ReadabilityTools', 'QualityLogging'
        )
        $results = @()
        foreach ($suite in $suites) {
            $testArguments = @()
            if ($suite -notin @('CodeQuality', 'ReadabilityTools', 'QualityLogging')) {
                $testArguments = @('-Configuration', $Configuration)
            }
            Write-Host "Running $suite tests ($Configuration)..."
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "tests/Run-${suite}Tests.ps1" @testArguments 2>&1 |
                Tee-Object -FilePath "$logs/test-$suite.log"
            $results += [pscustomobject]@{ configuration = $Configuration; suite = $suite; exit_code = $LASTEXITCODE }
        }
        $results | ConvertTo-Json | Set-Content "$logs/tests.json" -Encoding UTF8
        $failed = @($results | Where-Object { $_.exit_code -ne 0 })
        if ($failed.Count) { throw "CI test suites failed: $($failed.suite -join ', ')" }
        Write-Host "Passed all $($suites.Count) CPU/tool test suites."
    }

    if ($Stage -eq 'Package' -or ($Stage -eq 'All' -and $Configuration -eq 'Release')) {
        if ($Configuration -ne 'Release') { throw 'Package verification requires -Configuration Release.' }
        & "$PSScriptRoot/verify_release.ps1" 2>&1 | Tee-Object -FilePath "$logs/package.log"
    }
} finally {
    Pop-Location
}
