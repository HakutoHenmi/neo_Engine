[CmdletBinding(DefaultParameterSetName = 'Changed')]
param(
    [Parameter(ParameterSetName = 'Files', Mandatory)][string[]]$Files,
    [Parameter(ParameterSetName = 'All', Mandatory)][switch]$All,
    [switch]$Check,
    [string]$ToolPath
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/code_quality_common.ps1"

try {
    $formatter = Find-CodeQualityTool -Name 'clang-format.exe' -ToolPath $ToolPath
    $sourceFiles = @(Get-CodeQualityFiles -Files $Files -All:$All)
    if ($sourceFiles.Count -eq 0) {
        Write-Host 'No matching C/C++ files.'
        exit 0
    }

    & $formatter --version
    $formatArguments = @('--style=file', '--fallback-style=none')
    if ($Check) { $formatArguments += @('--dry-run', '--Werror', '--ferror-limit=1') }
    else { $formatArguments += '-i' }

    $failed = $false
    foreach ($file in $sourceFiles) {
        & $formatter @formatArguments $file
        if ($LASTEXITCODE -ne 0) { $failed = $true }
    }
    if ($failed) { throw 'Formatting check or formatting failed. See diagnostics above.' }
    Write-Host "Completed: $($sourceFiles.Count) file(s)."
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
