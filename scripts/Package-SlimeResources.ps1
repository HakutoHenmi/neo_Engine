param([string]$Destination = '', [switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceRoot = Join-Path $repoRoot 'Resources'
$releaseRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '../Generated/Outputs/Release/Resources'))
if (!$Destination) { $Destination = $releaseRoot }
$targetRoot = [IO.Path]::GetFullPath($Destination).TrimEnd('\','/')
$testRoot = Join-Path $repoRoot 'tests/out'
$distRoot = Join-Path $repoRoot 'dist'
if ([IO.Path]::GetFileName($targetRoot) -ne 'Resources' -or
    ($targetRoot -ne $releaseRoot -and !$targetRoot.StartsWith($testRoot+'\',[StringComparison]::OrdinalIgnoreCase) -and !$targetRoot.StartsWith($distRoot+'\',[StringComparison]::OrdinalIgnoreCase))) {
    throw 'Destination must be Release/Resources or a Resources directory under this project tests/out or dist.'
}
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'SlimeResources.json') -Raw -Encoding UTF8 | ConvertFrom-Json
# Runtime-compiled shaders and their includes must never be silently omitted
# when gameplay adds a pipeline. Keep all shader sources in the Release package.
$shaderFiles = @(Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'shaders') -File -Recurse | Where-Object { $_.Extension -in '.hlsl','.hlsli' } | ForEach-Object { $_.FullName.Substring($sourceRoot.Length+1).Replace('\','/') })
$manifest = @(($manifest + $shaderFiles) | Sort-Object -Unique)
if ($manifest.Count -lt 20) { throw 'Resource manifest is unexpectedly empty.' }
$keep = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$totalBytes = 0L
foreach ($relative in $manifest) {
    $source = [IO.Path]::GetFullPath((Join-Path $sourceRoot $relative))
    if (!$source.StartsWith($sourceRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing or invalid resource: $relative" }
    $totalBytes += (Get-Item -LiteralPath $source).Length
    [void]$keep.Add([IO.Path]::GetFullPath((Join-Path $targetRoot $relative)))
}
# Validate the absolute target and every entry before any removal; never traverse links.
$parent = $targetRoot
while ($parent) {
    if ((Test-Path -LiteralPath $parent) -and ((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Linked destination is not supported: $parent" }
    $parent = [IO.Path]::GetDirectoryName($parent)
}
$existing = @()
if (Test-Path -LiteralPath $targetRoot) {
    $existing = @(Get-ChildItem -LiteralPath $targetRoot -Recurse -Force)
    foreach ($item in $existing) {
        if (!$item.FullName.StartsWith($targetRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Unsafe resource entry: $($item.FullName)" }
    }
}
$beforeBytes = ($existing | Where-Object {!$_.PSIsContainer} | Measure-Object Length -Sum).Sum
$obsolete = @($existing | Where-Object {!$_.PSIsContainer -and !$keep.Contains($_.FullName)})
if (!$CheckOnly) {
    foreach ($relative in $manifest) {
        $dest = [IO.Path]::GetFullPath((Join-Path $targetRoot $relative))
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        Copy-Item -LiteralPath (Join-Path $sourceRoot $relative) -Destination $dest -Force
    }
    foreach ($item in $obsolete) { Remove-Item -LiteralPath $item.FullName -Force }
    # Empty directories only. No recursive deletion is needed.
    foreach ($dir in @($existing | Where-Object PSIsContainer | Sort-Object { $_.FullName.Length } -Descending)) {
        if (!(Get-ChildItem -LiteralPath $dir.FullName -Force | Select-Object -First 1)) { Remove-Item -LiteralPath $dir.FullName -Force }
    }
}
[pscustomobject]@{ Destination=$targetRoot; Files=$manifest.Count; RemovedFiles=$obsolete.Count; BeforeMB=[Math]::Round($beforeBytes/1MB,2); AfterMB=[Math]::Round($totalBytes/1MB,2); CheckOnly=[bool]$CheckOnly } | ConvertTo-Json
