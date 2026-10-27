param([Parameter(Mandatory=$true)][string]$Destination, [switch]$CheckOnly)
$ErrorActionPreference='Stop'
# libntc.dll is not Authenticode-signed; accept only the official RTXNTC v0.10.0-beta binary.
$expectedHash='DB97F3AC6EF032525C29C87278568A8C67DEF7F36FC90AA918675E74C3989C71'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$target=[IO.Path]::GetFullPath($Destination).TrimEnd('\','/')
$outputs=[IO.Path]::GetFullPath((Join-Path $repoRoot '../Generated/Outputs')).TrimEnd('\','/')
if([IO.Path]::GetFileName($target) -ne 'NTC' -or
    !($target.StartsWith($outputs+'\',[StringComparison]::OrdinalIgnoreCase) -or
      $target.StartsWith($repoRoot+'\build\',[StringComparison]::OrdinalIgnoreCase) -or
      $target.StartsWith($repoRoot+'\dist\',[StringComparison]::OrdinalIgnoreCase) -or
      $target.StartsWith($repoRoot+'\tests\out\',[StringComparison]::OrdinalIgnoreCase))){throw 'Invalid NTC package destination.'}
$dll=Join-Path $repoRoot 'externals/RTXNTC/bin/libntc.dll'
# The road material needs the NTC runtime: without it the package would ship untextured roads.
if(!(Test-Path -LiteralPath $dll -PathType Leaf)){throw 'Missing externals/RTXNTC/bin/libntc.dll (see externals/RTXNTC/README.md).'}
if((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $expectedHash){throw 'Unexpected libntc.dll hash.'}
if(!$CheckOnly){
    [void][IO.Directory]::CreateDirectory($target)
    Copy-Item -LiteralPath $dll -Destination (Join-Path $target 'libntc.dll') -Force
    Copy-Item -LiteralPath (Join-Path $repoRoot 'externals/RTXNTC/LICENSE.txt') -Destination (Join-Path $target 'NVIDIA-RTX-SDK-LICENSE.txt') -Force
    Copy-Item -LiteralPath (Join-Path $repoRoot 'externals/RTXNTC/ThirdPartyLicenses.txt') -Destination $target -Force
}
Write-Output "Verified NTC runtime package: $target"
