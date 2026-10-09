param([Parameter(Mandatory=$true)][string]$Destination, [switch]$CheckOnly)
$ErrorActionPreference='Stop'
# MSBuild may inherit PowerShell 7's PSModulePath while invoking Windows
# PowerShell 5. Load the signature cmdlet from this host's own built-in module.
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Security/Microsoft.PowerShell.Security.psd1')
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$target=[IO.Path]::GetFullPath($Destination).TrimEnd('\','/')
$outputs=[IO.Path]::GetFullPath((Join-Path $repoRoot '../Generated/Outputs')).TrimEnd('\','/')
if([IO.Path]::GetFileName($target) -ne 'Streamline' -or
    !($target.StartsWith($outputs+'\',[StringComparison]::OrdinalIgnoreCase) -or
      $target.StartsWith($repoRoot+'\dist\',[StringComparison]::OrdinalIgnoreCase) -or
      $target.StartsWith($repoRoot+'\tests\out\',[StringComparison]::OrdinalIgnoreCase))){throw 'Invalid Streamline package destination.'}
$source=Join-Path $repoRoot 'externals/Streamline/bin'
if(!(Test-Path -LiteralPath (Join-Path $source 'sl.interposer.dll'))){
    Write-Warning 'Streamline SDK not installed; this build will use native rendering.'
    return
}
$names=@('sl.interposer.dll','sl.common.dll','sl.dlss.dll','nvngx_dlss.dll','nvngx_dlss.license.txt')
foreach($name in $names){
    $file=Join-Path $source $name
    if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing DLSS runtime file: $name"}
    if($name.EndsWith('.dll')){
        $signature=Get-AuthenticodeSignature -LiteralPath $file
        if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=NVIDIA Corporation,'){throw "Invalid NVIDIA signature: $name"}
    }
}
if(!$CheckOnly){
    [void][IO.Directory]::CreateDirectory($target)
    foreach($name in $names){Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $target $name) -Force}
    Copy-Item -LiteralPath (Join-Path $repoRoot 'Docs/NVIDIA-RTX-SDK-LICENSE.txt') -Destination $target -Force
}
Write-Output "Verified DLSS runtime package: $target"
