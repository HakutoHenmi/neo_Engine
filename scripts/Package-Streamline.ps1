param([Parameter(Mandatory=$true)][string]$Destination, [switch]$CheckOnly, [switch]$Verify)
$ErrorActionPreference='Stop'
if($CheckOnly -and $Verify){throw 'Choose either CheckOnly or Verify.'}
# MSBuild may inherit PowerShell 7's PSModulePath while invoking Windows
# PowerShell 5. Load the signature cmdlet from this host's own built-in module.
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Security/Microsoft.PowerShell.Security.psd1')
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1')
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$target=[IO.Path]::GetFullPath($Destination).TrimEnd('\','/')
$outputs=[IO.Path]::GetFullPath((Join-Path $repoRoot '../Generated/Outputs')).TrimEnd('\','/')
if([IO.Path]::GetFileName($target) -ne 'Streamline' -or
    !($target.StartsWith($outputs+'\',[StringComparison]::OrdinalIgnoreCase) -or
      $target.StartsWith($repoRoot+'\dist\',[StringComparison]::OrdinalIgnoreCase) -or
      $target.StartsWith($repoRoot+'\tests\out\',[StringComparison]::OrdinalIgnoreCase) -or
      $target -eq (Join-Path $repoRoot 'artifacts/ci/bin/Release/Streamline'))){throw 'Invalid Streamline package destination.'}
$source=Join-Path $repoRoot 'externals/Streamline/bin'
if(!(Test-Path -LiteralPath (Join-Path $source 'sl.interposer.dll'))){
    if($Verify -and (Test-Path -LiteralPath $target)){throw 'Unexpected Streamline package without a verified SDK source.'}
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
# Streamline uses the dynamic VC runtime; keep it beside the SDK DLLs for LoadLibraryEx.
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$crtFile=@(& $vswhere -latest -products '*' -version '[17.0,18.0)' -find 'VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT/msvcp140.dll')[0]
if(!$crtFile){throw 'Visual Studio 2022 x64 VC runtime redistributables were not found.'}
$crtRoot=Split-Path $crtFile -Parent
$crtNames=@('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')
foreach($name in $crtNames){
    $file=Join-Path $crtRoot $name
    if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing VC runtime redistributable: $name"}
    $signature=Get-AuthenticodeSignature -LiteralPath $file
    if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation,'){throw "Invalid Microsoft signature: $name"}
}
if($Verify){
    foreach($name in @($names)+@($crtNames)+@('NVIDIA-RTX-SDK-LICENSE.txt')){
        $original=if($name -eq 'NVIDIA-RTX-SDK-LICENSE.txt'){Join-Path $repoRoot 'Docs/NVIDIA-RTX-SDK-LICENSE.txt'}elseif($name -in $crtNames){Join-Path $crtRoot $name}else{Join-Path $source $name}
        $packaged=Join-Path $target $name
        if(!(Test-Path -LiteralPath $packaged -PathType Leaf) -or
            (Get-FileHash -LiteralPath $packaged -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash){throw "Missing or mismatched Streamline package file: $name"}
    }
}elseif(!$CheckOnly){
    [void][IO.Directory]::CreateDirectory($target)
    foreach($name in $names){Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $target $name) -Force}
    foreach($name in $crtNames){Copy-Item -LiteralPath (Join-Path $crtRoot $name) -Destination (Join-Path $target $name) -Force}
    Copy-Item -LiteralPath (Join-Path $repoRoot 'Docs/NVIDIA-RTX-SDK-LICENSE.txt') -Destination $target -Force
}
Write-Output "Verified DLSS runtime package: $target"
