[CmdletBinding()]
param([ValidateSet('All', 'Build', 'Tests')][string]$Stage = 'All')

$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$logs = Join-Path $repoRoot 'artifacts/ci/logs'
New-Item -ItemType Directory -Force $logs | Out-Null

Push-Location $repoRoot
try {
    if ($Stage -in @('All', 'Build')) {
        $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
        $msbuild = @(& $vswhere -latest -products '*' -version '[17.0,18.0)' `
            -requires Microsoft.Component.MSBuild -find 'MSBuild/Current/Bin/amd64/MSBuild.exe')[0]
        if (-not $msbuild) { throw 'Visual Studio 2022 MSBuild was not found.' }
        if (-not (Test-Path 'artifacts/ci/deps/assimp/source.sha256')) {
            throw 'Run scripts/ci/bootstrap_dependencies.ps1 before building.'
        }
        & $msbuild DirectXGame_New.sln -nologo -m:2 -nodeReuse:false -verbosity:minimal `
            -p:Configuration=Development -p:Platform=x64 -p:PreferredToolArchitecture=x64 -p:CL_MPCount=2 `
            -p:EnableCodeQualityLogging=false -p:RunCodeAnalysis=false `
            "-p:ForceImportBeforeCppTargets=$PSScriptRoot/build.props" "-bl:$logs/build.binlog" 2>&1 |
            Tee-Object -FilePath "$logs/build.log"
        if ($LASTEXITCODE -ne 0) { throw 'CI solution build failed. See artifacts/ci/logs/build.log.' }
        if (-not (Test-Path 'artifacts/ci/bin/Development/DirectXGameApp.exe')) {
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
            Write-Host "Running $suite tests..."
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "tests/Run-${suite}Tests.ps1" 2>&1 |
                Tee-Object -FilePath "$logs/test-$suite.log"
            $results += [pscustomobject]@{ suite = $suite; exit_code = $LASTEXITCODE }
        }
        $results | ConvertTo-Json | Set-Content "$logs/tests.json" -Encoding UTF8
        $failed = @($results | Where-Object { $_.exit_code -ne 0 })
        if ($failed.Count) { throw "CI test suites failed: $($failed.suite -join ', ')" }
        Write-Host "Passed all $($suites.Count) CPU/tool test suites."
    }
} finally {
    Pop-Location
}
