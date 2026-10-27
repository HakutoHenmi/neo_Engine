param(
    [switch]$Prepare,
    [switch]$ValidateGpu,
    [string]$DirectXTexLibrary = 'build/editor2/bin/Release/DirectXTex.lib'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
    # Uses the same SDK, checked source revision, original input hashes and compiler as the full experiment.
    & "$PSScriptRoot/Run-RoadTextureTests.ps1" -BuildOnly -DirectXTexLibrary $DirectXTexLibrary
    $out = Join-Path $repo 'tests/out/road-texture'
    $cli = Join-Path $repo 'build/ntc/sdk/bin/windows-x64/ntc-cli.exe'
    $baseInputs = Get-Content -Raw (Join-Path $out 'inputs.json') | ConvertFrom-Json
    $settings = [ordered]@{
        sdk = $baseInputs.sdk
        inputs = $baseInputs.inputs
        normalBc5Sha256 = (Get-FileHash (Join-Path $out 'bcn/normal.dds') -Algorithm SHA256).Hash.ToLowerInvariant()
        requestedBpp = @(3,5,8,12,16)
        trainingSteps = 30000
        randomSeed = 1234
        gdeflate = 'off'
        coopVec = $false
    } | ConvertTo-Json -Depth 8
    $settingsPath = Join-Path $out 'hybrid-inputs.json'
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    if ($Prepare) {
        $manifest = Get-Content -Raw (Join-Path $out 'manifest.json') | ConvertFrom-Json
        $manifest.textures = @($manifest.textures | Where-Object { $_.name -ne 'normal' })
        [IO.File]::WriteAllText((Join-Path $out 'hybrid-manifest.json'), ($manifest | ConvertTo-Json -Depth 8), $utf8)
        foreach ($bpp in @(3,5,8,12,16)) {
            $dir = Join-Path $out "hybrid-$bpp"
            New-Item -ItemType Directory -Force $dir | Out-Null
            Write-Host "Training color / roughness / AO at requested $bpp bpp; fixed BC5 normal excluded..."
            & $cli --dx12 --no-coopVec --loadManifest (Join-Path $out 'hybrid-manifest.json') --generateMips --compress --bitsPerPixel $bpp --trainingSteps 30000 --randomSeed 1234 --decompress --optimizeBC --gdeflate off --saveCompressed (Join-Path $dir 'material.ntc') *> (Join-Path $dir 'train.txt')
            if ($LASTEXITCODE) { throw "Hybrid training failed ($bpp). See $dir/train.txt." }
        }
        [IO.File]::WriteAllText($settingsPath, $settings, $utf8)
    }
    if (!(Test-Path $settingsPath)) { throw 'Run with -Prepare once to train the hybrid profiles.' }
    if ((Get-Content -Raw $settingsPath).Trim() -ne $settings.Trim()) { throw 'Inputs or training settings changed; run with -Prepare.' }
    if ($ValidateGpu) {
        & tests/out/RoadTextureGpuTests.exe --hybrid-sweep --debug *> (Join-Path $out 'hybrid-native-debug.txt')
        if ($LASTEXITCODE) { throw 'Hybrid D3D12 validation failed. See hybrid-native-debug.txt.' }
    }
    & nvidia-smi --query-gpu=name,driver_version,memory.total,pstate,clocks.current.graphics,clocks.current.memory,temperature.gpu,power.draw,pcie.link.gen.current,pcie.link.width.current --format=csv *> (Join-Path $out 'hybrid-gpu-environment.csv')
    if ($LASTEXITCODE) { throw 'NVIDIA GPU telemetry unavailable.' }
    & tests/out/RoadTextureGpuTests.exe --hybrid-sweep *> (Join-Path $out 'hybrid-native.txt')
    if ($LASTEXITCODE) { throw 'Hybrid measurement failed. See hybrid-native.txt.' }
    $normal = (Get-FileHash (Join-Path $out 'bcn-readback/normal.dds') -Algorithm SHA256).Hash
    foreach ($bpp in @(3,5,8,12,16)) {
        if ((Get-FileHash (Join-Path $out "hybrid-$bpp/normal.dds") -Algorithm SHA256).Hash -ne $normal) { throw "BC5 normal changed in hybrid-$bpp." }
    }
    Get-Content (Join-Path $out 'hybrid-native.txt')
    Write-Host 'All hybrid normal maps are byte-identical to the BC5 baseline (all 12 mips).'
    Write-Host 'Results: hybrid-gpu.json, hybrid-quality.json, hybrid-*-preview.png'
} finally { Pop-Location }
