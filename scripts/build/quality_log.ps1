[CmdletBinding()]
param(
    [ValidateSet('Collect', 'Watch', 'Start', 'Stop', 'Status', 'Build')][string]$Action = 'Collect',
    [ValidateSet('Manual', 'Save', 'Build')][string]$Trigger = 'Manual',
    [ValidateSet('All', 'Engine', 'Game')][string]$Project = 'All',
    [ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Development',
    [string]$File,
    [string]$LogDirectory,
    [switch]$SkipClangTidy,
    [switch]$NoStartWatcher
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/readability_common.ps1"
try {
    $python = Join-Path $script:ReadabilityToolsRoot 'python/Scripts/python.exe'
    if (-not (Test-Path -LiteralPath $python)) {
        $python = (Get-Command python -CommandType Application -ErrorAction Stop).Source
    }
    $command = switch ($Action) {
        'Collect' { 'collect' }
        'Watch' { 'watch' }
        'Start' { 'start' }
        'Stop' { 'stop' }
        'Status' { 'status' }
        'Build' { 'enqueue' }
    }
    $arguments = @("$PSScriptRoot/quality_logging.py", $command, '--trigger', $Trigger,
        '--project', $Project, '--configuration', $Configuration)
    if ($Action -eq 'Build') { $arguments += @('--event', 'build-start') }
    if ($File) { $arguments += @('--file', $File) }
    if ($LogDirectory) { $arguments += @('--log-directory', [IO.Path]::GetFullPath($LogDirectory)) }
    if ($SkipClangTidy) { $arguments += '--skip-clang-tidy' }
    if ($NoStartWatcher) { $arguments += '--no-start-watcher' }
    & $python @arguments
    if ($LASTEXITCODE -ne 0) { throw "Quality logging failed (exit $LASTEXITCODE)." }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
