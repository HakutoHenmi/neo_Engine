param([Parameter(Mandatory=$true)][string]$Dxc)
$ErrorActionPreference='Stop'
$repoPath=Split-Path $PSScriptRoot -Parent
$compiler=(Resolve-Path -LiteralPath $Dxc).Path
Push-Location $repoPath
try {
    & $compiler -T cs_6_5 -E main -O3 -Fo Resources/shaders/RtShadow.cso Resources/shaders/RtShadow.hlsl
    if($LASTEXITCODE){throw 'RT shadow shader compilation failed.'}
    & $compiler -T cs_6_5 -E main -O3 -Fo Resources/shaders/RtLighting.cso Resources/shaders/RtLighting.hlsl
    if($LASTEXITCODE){throw 'RT lighting shader compilation failed.'}
    Get-FileHash -LiteralPath Resources/shaders/RtShadow.cso -Algorithm SHA256
} finally { Pop-Location }
