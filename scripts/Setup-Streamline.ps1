param([Parameter(Mandatory=$true)][string]$SdkArchive)
$ErrorActionPreference='Stop'
$repoPath=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$archive=(Resolve-Path -LiteralPath $SdkArchive).Path
$expected='92C4D954631A1710DA86CA3FA8D5034F2B9503838C95FC4AE977AE149319781B'
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected){throw 'Expected official Streamline 2.14.1 x64 SDK archive.'}
# The caller must accept Docs/NVIDIA-RTX-SDK-LICENSE.txt before using the DLLs.
$unpack=Join-Path $repoPath 'tests/out/streamline-setup'
Expand-Archive -LiteralPath $archive -DestinationPath $unpack -Force
$destination=Join-Path $repoPath 'externals/Streamline/bin'
[void][IO.Directory]::CreateDirectory($destination)
foreach($name in @('sl.interposer.dll','sl.common.dll','sl.dlss.dll','nvngx_dlss.dll')){
    $source=Join-Path $unpack "bin/x64/$name"
    $signature=Get-AuthenticodeSignature -LiteralPath $source
    if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=NVIDIA Corporation,'){throw "Invalid NVIDIA signature: $name"}
    Copy-Item -LiteralPath $source -Destination (Join-Path $destination $name) -Force
}
Copy-Item -LiteralPath (Join-Path $unpack 'bin/x64/nvngx_dlss.license.txt') -Destination $destination -Force
'Installed signed Streamline 2.14.1 DLSS SR binaries.'
