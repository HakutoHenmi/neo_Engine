param(
    [Parameter(Mandatory)][ValidateSet('format', 'lizard', 'cpd', 'clang-tidy')][string]$Tool,
    [Parameter(Mandatory)][string]$RequestFile,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
$env:PYTHONUTF8 = '1'
. "$PSScriptRoot/readability_common.ps1"
$request = [IO.File]::ReadAllText($RequestFile) | ConvertFrom-Json
switch ($Tool) {
    'format' {
        $files = if ($request.trigger -eq 'Save' -and $request.files.Count) {
            @($request.files | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
        } else { @(Get-ReadabilityFiles) }
        if ($files.Count) { & "$PSScriptRoot/format_code.ps1" -Files $files -Check }
    }
    'lizard' { & "$PSScriptRoot/measure_complexity.ps1" -OutputDirectory $OutputDirectory }
    'cpd' { & "$PSScriptRoot/find_duplicates.ps1" -OutputDirectory $OutputDirectory }
    'clang-tidy' {
        $options = @{ Project = $request.project; Configuration = $request.configuration; OutputDirectory = $OutputDirectory }
        if ($request.files.Count -eq 1 -and [IO.Path]::GetExtension($request.files[0]) -in @('.cpp', '.cc', '.cxx', '.c')) {
            $options.File = $request.files[0]
            $options.Project = 'All'
        }
        & "$PSScriptRoot/analyze_code.ps1" @options
    }
}
