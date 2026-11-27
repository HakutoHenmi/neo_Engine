[CmdletBinding(DefaultParameterSetName = 'Project')]
param(
    [Parameter(ParameterSetName = 'Project')][ValidateSet('All', 'Engine', 'Game')][string]$Project = 'All',
    [Parameter(ParameterSetName = 'Project')][switch]$IncludeTests,
    [Parameter(ParameterSetName = 'Files', Mandatory)][string[]]$Files,
    [ValidateRange(1, 1000000)][int]$MaxComplexity = 25,
    [ValidateRange(1, 1000000)][int]$MaxFunctionLines = 100,
    [ValidateRange(0, 10000)][int]$MaxParameters = 8,
    [string]$OutputDirectory,
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/readability_common.ps1"
try {
    $python = Get-LizardPython
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $script:CodeQualityRoot 'artifacts/code-quality/readability' }
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
    $sourceFiles = @(Get-ReadabilityFiles -Files $Files -Project $Project -IncludeTests:$IncludeTests)
    $fileList = Join-Path $OutputDirectory 'complexity-files.txt'
    Write-ReadabilityFileList -Files $sourceFiles -Path $fileList
    $arguments = @(
        "$PSScriptRoot/measure_complexity.py", '--file-list', $fileList, '--output-directory', $OutputDirectory,
        '--max-complexity', $MaxComplexity, '--max-lines', $MaxFunctionLines, '--max-parameters', $MaxParameters
    )
    if ($Check) { $arguments += '--check' }
    & $python @arguments
    if ($LASTEXITCODE -eq 4 -and $Check) { exit 1 }
    if ($LASTEXITCODE -ne 0) { throw "Lizard analysis failed (exit $LASTEXITCODE)." }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
