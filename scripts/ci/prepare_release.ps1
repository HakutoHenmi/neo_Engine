[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$dependency = (Get-Content "$PSScriptRoot/dependencies.json" -Raw | ConvertFrom-Json).ntc
$runtime = Join-Path $repoRoot 'artifacts/ci/deps/ntc/libntc.dll'
if ((Test-Path -LiteralPath $runtime) -and
    (Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -eq $dependency.runtime_sha256) {
    Write-Host 'The verified CI NTC runtime is already available.'
    return
}

$archive = Join-Path $repoRoot "artifacts/ci/downloads/RTXNTC-$($dependency.version).zip"
New-Item -ItemType Directory -Force (Split-Path $archive -Parent), (Split-Path $runtime -Parent) | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -UseBasicParsing -Uri $dependency.url -OutFile "$archive.part"
    Move-Item -LiteralPath "$archive.part" -Destination $archive -Force
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $dependency.sha256) {
    throw "NTC SDK archive SHA256 mismatch: $archive"
}

# Only the pinned runtime is needed for packaging; do not run the SDK tools.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    $entry = $zip.GetEntry('bin/windows-x64/libntc.dll')
    if (-not $entry) { throw 'The NTC archive does not contain the expected runtime.' }
    [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $runtime, $true)
} finally {
    $zip.Dispose()
}
if ((Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -ne $dependency.runtime_sha256) {
    throw 'NTC runtime SHA256 mismatch.'
}
Write-Host "Verified CI runtime: $runtime"
