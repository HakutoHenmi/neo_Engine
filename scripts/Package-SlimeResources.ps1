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

# Model textures: Engine/Model.cpp reads only the colour image each FBX names, so the other maps
# beside the models (normal, roughness, metal, AO, ...) are left out. Colour PNGs are packaged as
# BCn DDS with mips (Model.cpp prefers a .dds next to the named image). Converted files are cached.
$modelTextures = 'Models/PolyHaven/textures/'
$toolSource = Join-Path $PSScriptRoot 'Compress-Textures.cpp'
$toolRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '../Generated/Tools'))
$tool = Join-Path $toolRoot 'Compress-Textures.exe'
$cacheRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '../Generated/TextureCache'))
if (!(Test-Path -LiteralPath $tool) -or (Get-Item -LiteralPath $tool).LastWriteTime -lt (Get-Item -LiteralPath $toolSource).LastWriteTime) {
    $library = Join-Path $repoRoot 'externals/Generated/Outputs/Release/DirectXTex.lib'
    if (!(Test-Path -LiteralPath $library)) { throw "DirectXTex Release library not found: $library" }
    $vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'Visual Studio C++ tools not found.' }
    [void][IO.Directory]::CreateDirectory($toolRoot)
    $command = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++17 /EHsc /O2 /MT /W3 "{1}" "{2}" /Fo"{3}\\" /Fe"{4}" /link "{5}" d3d11.lib dxgi.lib ole32.lib windowscodecs.lib >nul' -f (Join-Path $vs 'Common7/Tools/VsDevCmd.bat'),$toolSource,(Join-Path $repoRoot 'externals/ufbx/ufbx.c'),$toolRoot,$tool,$library
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE) { throw 'Texture tool build failed.' }
}
$models = @($manifest | Where-Object { $_ -like 'Models/PolyHaven/*.fbx' } | ForEach-Object { Join-Path $sourceRoot $_ })
$referenced = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
if ($models.Count) {
    $lines = & $tool refs @models
    if ($LASTEXITCODE) { throw 'Reading model texture references failed.' }
    $prefix = $sourceRoot.Replace('\','/') + '/'
    foreach ($line in $lines) { if ($line.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { [void]$referenced.Add($line.Substring($prefix.Length)) } }
}
# Each packaged file: where it comes from and where it goes
$entries = foreach ($relative in $manifest) {
    $source = [IO.Path]::GetFullPath((Join-Path $sourceRoot $relative))
    if (!$source.StartsWith($sourceRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing or invalid resource: $relative" }
    if ($relative.StartsWith($modelTextures,[StringComparison]::OrdinalIgnoreCase)) {
        if (!$referenced.Contains($relative)) { continue }
        if ([IO.Path]::GetExtension($relative) -ieq '.png') {
            $relative = [IO.Path]::ChangeExtension($relative,'.dds')
            $cached = Join-Path $cacheRoot $relative
            if (!(Test-Path -LiteralPath $cached) -or (Get-Item -LiteralPath $cached).LastWriteTime -lt (Get-Item -LiteralPath $source).LastWriteTime -or
                (Get-Item -LiteralPath $cached).LastWriteTime -lt (Get-Item -LiteralPath $tool).LastWriteTime) {
                & $tool dds $source $cached
                if ($LASTEXITCODE) { throw "Texture compression failed: $relative" }
            }
            $source = $cached
        }
    }
    [pscustomobject]@{ Source=$source; Relative=$relative }
}

$keep = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$totalBytes = 0L
foreach ($entry in $entries) {
    $totalBytes += (Get-Item -LiteralPath $entry.Source).Length
    [void]$keep.Add([IO.Path]::GetFullPath((Join-Path $targetRoot $entry.Relative)))
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
    foreach ($entry in $entries) {
        $dest = [IO.Path]::GetFullPath((Join-Path $targetRoot $entry.Relative))
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        Copy-Item -LiteralPath $entry.Source -Destination $dest -Force
    }
    foreach ($item in $obsolete) { Remove-Item -LiteralPath $item.FullName -Force }
    # Empty directories only. No recursive deletion is needed.
    foreach ($dir in @($existing | Where-Object PSIsContainer | Sort-Object { $_.FullName.Length } -Descending)) {
        if (!(Get-ChildItem -LiteralPath $dir.FullName -Force | Select-Object -First 1)) { Remove-Item -LiteralPath $dir.FullName -Force }
    }
}
[pscustomobject]@{ Destination=$targetRoot; Files=@($entries).Count; RemovedFiles=$obsolete.Count; BeforeMB=[Math]::Round($beforeBytes/1MB,2); AfterMB=[Math]::Round($totalBytes/1MB,2); CheckOnly=[bool]$CheckOnly } | ConvertTo-Json
