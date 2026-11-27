[CmdletBinding(DefaultParameterSetName = 'Project')]
param(
    [Parameter(ParameterSetName = 'Project')][ValidateSet('All', 'Engine', 'Game')][string]$Project = 'All',
    [Parameter(ParameterSetName = 'Project')][switch]$IncludeTests,
    [Parameter(ParameterSetName = 'Files', Mandatory)][string[]]$Files,
    [ValidateRange(1, 1000000)][int]$MinimumTokens = 100,
    [string]$OutputDirectory,
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/readability_common.ps1"
try {
    $runtime = Get-CpdRuntime
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $script:CodeQualityRoot 'artifacts/code-quality/readability' }
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
    $sourceFiles = @(Get-ReadabilityFiles -Files $Files -Project $Project -IncludeTests:$IncludeTests)
    # CPD filters unknown suffixes and its C++ lexer needs a final newline.
    # Stage all inputs as C++ files, preserving line numbers and source text otherwise.
    $sourceMap = @{}
    $stagedFiles = @(foreach ($source in $sourceFiles) {
        $relative = $source.Substring($script:CodeQualityRoot.Length + 1)
        $staged = [IO.Path]::GetFullPath((Join-Path $OutputDirectory "cpd-input/$relative.cpp"))
        New-Item -ItemType Directory -Force (Split-Path $staged -Parent) | Out-Null
        $content = [IO.File]::ReadAllText($source)
        if (-not $content.EndsWith("`n")) { $content += "`n" }
        [IO.File]::WriteAllText($staged, $content, [Text.UTF8Encoding]::new($false))
        $sourceMap[$staged] = $source
        $staged
    })
    $fileList = Join-Path $OutputDirectory 'cpd-files.txt'
    Write-ReadabilityFileList -Files $stagedFiles -Path $fileList
    $reportPath = Join-Path $OutputDirectory 'cpd.xml'
    $temporaryReport = "$reportPath.partial"
    & $runtime.Java '-Dfile.encoding=UTF-8' -cp $runtime.ClassPath net.sourceforge.pmd.cli.PmdCli cpd `
        --language cpp --minimum-tokens $MinimumTokens --encoding UTF-8 --file-list $fileList `
        --format xml --report-file $temporaryReport
    $cpdExit = $LASTEXITCODE
    # 4 means duplicates, while 5 means some source files could not be analyzed.
    if ($cpdExit -notin @(0, 4)) { throw "PMD CPD failed (exit $cpdExit). See diagnostics and $temporaryReport" }
    [xml]$report = [IO.File]::ReadAllText($temporaryReport)
    $namespaces = [Xml.XmlNamespaceManager]::new($report.NameTable)
    $namespaces.AddNamespace('cpd', $report.DocumentElement.NamespaceURI)
    foreach ($fileNode in $report.SelectNodes('//cpd:file', $namespaces)) {
        $path = [string]$fileNode.path
        if (-not $sourceMap.ContainsKey($path)) { throw "CPD reported an unexpected source: $path" }
        $fileNode.SetAttribute('path', $sourceMap[$path])
    }
    $analyzedFiles = @($report.SelectNodes('/cpd:pmd-cpd/cpd:file', $namespaces))
    if ($analyzedFiles.Count -ne $sourceFiles.Count) {
        throw "CPD processed $($analyzedFiles.Count) of $($sourceFiles.Count) selected files. See $temporaryReport"
    }
    $report.Save($temporaryReport)
    Move-Item -LiteralPath $temporaryReport -Destination $reportPath -Force
    $groups = @($report.SelectNodes('/cpd:pmd-cpd/cpd:duplication', $namespaces))
    $rows = @(for ($groupIndex = 0; $groupIndex -lt $groups.Count; ++$groupIndex) {
        $group = $groups[$groupIndex]
        foreach ($file in $group.SelectNodes('cpd:file', $namespaces)) {
            [pscustomobject]@{
                group = $groupIndex + 1
                tokens = [int]$group.tokens
                lines = [int]$group.lines
                file = [string]$file.path
                start_line = [int]$file.line
                end_line = [int]$file.endline
            }
        }
    })
    $csvPath = Join-Path $OutputDirectory 'duplicates.csv'
    if ($rows.Count) { $rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8 }
    else { [IO.File]::WriteAllText($csvPath, '"group","tokens","lines","file","start_line","end_line"' + "`n", [Text.UTF8Encoding]::new($true)) }
    [pscustomobject]@{
        tool = "PMD CPD $($runtime.Version)"
        files_selected = $sourceFiles.Count
        files_analyzed = $analyzedFiles.Count
        minimum_tokens = $MinimumTokens
        duplicate_groups = $groups.Count
        occurrences = $rows.Count
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'duplicates.json') -Encoding UTF8
    Write-Host "PMD CPD: $($sourceFiles.Count) files, $($groups.Count) duplicate group(s)."
    Write-Host "Reports: $reportPath and $csvPath"
    if ($Check -and $groups.Count) { exit 1 }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
