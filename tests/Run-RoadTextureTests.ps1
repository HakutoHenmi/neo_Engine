param(
    [switch]$AcquireSdk,
    [switch]$Prepare,
    [switch]$ValidateGpu,
    [switch]$BuildOnly,
    [string]$DirectXTexLibrary = 'build/editor2/bin/Release/DirectXTex.lib'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$version = 'v0.10.0-beta'
$sdkHash = '2d349fae349a14d0f35e856963f2c53d1cbbc4bb5fb1fe129ea6159c93d9e39d'
$sourceCommit = '1830798aa54c38069f335cecab2406af97bda1d9'
$libraryCommit = '35ec039503ef5e151c661beccc82390948dd5218'
Push-Location $repo
try {
    New-Item -ItemType Directory -Force 'build/ntc', 'tests/out/road-texture' | Out-Null
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $cli = Join-Path $repo 'build/ntc/sdk/bin/windows-x64/ntc-cli.exe'
    if ($AcquireSdk) {
        $archive = Join-Path $repo 'build/ntc/RTXNTC_windows_x64_v0.10.0b.zip'
        if (!(Test-Path $archive)) {
            Invoke-WebRequest "https://github.com/NVIDIA-RTX/RTXNTC/releases/download/$version/RTXNTC_windows_x64_v0.10.0b.zip" -OutFile $archive
        }
        if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sdkHash) { throw 'Official SDK checksum mismatch.' }
        if (!(Test-Path $cli)) { Expand-Archive $archive -DestinationPath 'build/ntc/sdk' }
        if (!(Test-Path 'build/ntc/source/.git')) {
            $oldLfs = $env:GIT_LFS_SKIP_SMUDGE
            try {
                $env:GIT_LFS_SKIP_SMUDGE = '1'
                & git clone --depth 1 --branch $version --filter=blob:none https://github.com/NVIDIA-RTX/RTXNTC.git build/ntc/source
                if ($LASTEXITCODE) { throw 'NTC source acquisition failed.' }
            } finally { $env:GIT_LFS_SKIP_SMUDGE = $oldLfs }
        }
        & git -C build/ntc/source submodule update --init --depth 1 libraries/RTXNTC-Library
        if ($LASTEXITCODE) { throw 'NTC header acquisition failed.' }
    }
    if (!(Test-Path $cli) -or !(Test-Path 'build/ntc/source/libraries/RTXNTC-Library/include/libntc/ntc.h')) { throw 'Run with -AcquireSdk to acquire the official SDK and matching headers.' }
    if ((& git -C build/ntc/source rev-parse HEAD) -ne $sourceCommit -or (& git -C build/ntc/source/libraries/RTXNTC-Library rev-parse HEAD) -ne $libraryCommit) { throw 'NTC source version mismatch.' }
    if (!(Test-Path $DirectXTexLibrary)) { throw 'Build the engine Release configuration first, or pass -DirectXTexLibrary with its Release static library.' }
    $suffixes = @('diff', 'nor_dx', 'Rough', 'AO')
    $names = @('albedo', 'normal', 'roughness', 'ao')
    $formats = @('BC7', 'BC5', 'BC4', 'BC4')
    $channels = @('RGB', 'RGB', 'R', 'R')
    $semantics = @('Albedo', 'Normal', 'Roughness', 'AO')
    $textures = @(); $inputs = @()
    for ($i = 0; $i -lt 4; $i++) {
        $path = Join-Path $repo "Resources/Textures/PolyHaven/asphalt_02_$($suffixes[$i])_2k.jpg"
        $semantic = @{}; $semantic[$semantics[$i]] = $channels[$i]
        $textures += @{fileName=$path; name=$names[$i]; channelSwizzle=$channels[$i]; bcFormat=$formats[$i]; isSRGB=($i -eq 0); semantics=$semantic}
        $inputs += [ordered]@{fileName=[IO.Path]::GetFileName($path); bytes=(Get-Item $path).Length; sha256=(Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $fingerprint = [ordered]@{sdk=$version; sdkSha256=$sdkHash; sourceCommit=$sourceCommit; libraryCommit=$libraryCommit; inputs=$inputs} | ConvertTo-Json -Depth 8
    $inputsFile = Join-Path $repo 'tests/out/road-texture/inputs.json'
    $ntcFile = 'tests/out/road-texture/asphalt_02.ntc'
    if ($Prepare) {
        [IO.File]::WriteAllText((Join-Path $repo 'tests/out/road-texture/manifest.json'), (@{textures=$textures} | ConvertTo-Json -Depth 8), $utf8)
        Write-Host 'Preparing BC7 / BC5 / BC4 / BC4 baseline on the hardware GPU...'
        & $cli --dx12 --no-coopVec --loadManifest tests/out/road-texture/manifest.json --generateMips --saveMips --saveImages tests/out/road-texture/bcn --benchmark 32 *> tests/out/road-texture/bcn-cli.txt
        if ($LASTEXITCODE) { throw 'BCn baseline preparation failed. See bcn-cli.txt.' }
        Write-Host 'Training the road material offline (5 requested bpp, 10000 steps, seed 1234)...'
        & $cli --dx12 --no-coopVec --loadManifest tests/out/road-texture/manifest.json --generateMips --compress --bitsPerPixel 5 --trainingSteps 10000 --randomSeed 1234 --decompress --optimizeBC --gdeflate off --saveCompressed $ntcFile *> tests/out/road-texture/train-cli.txt
        if ($LASTEXITCODE) { throw 'Offline NTC training failed. See train-cli.txt.' }
        & $cli --dx12 --no-coopVec $ntcFile --decompress --saveMips --saveImages tests/out/road-texture/ntc-bcn --benchmark 32 *> tests/out/road-texture/decode-cli.txt
        if ($LASTEXITCODE) { throw 'Official reference decoder failed. See decode-cli.txt.' }
        [IO.File]::WriteAllText($inputsFile, $fingerprint, $utf8)
    }
    if (!(Test-Path $ntcFile) -or !(Test-Path $inputsFile)) { throw 'Run with -Prepare once to generate the road NTC and BCn assets.' }
    if ((Get-Content -Raw $inputsFile).Trim() -ne $fingerprint.Trim()) { throw 'Road inputs or SDK changed; rerun with -Prepare.' }
    & nvidia-smi --query-gpu=name,driver_version,memory.total,pstate,clocks.current.graphics,clocks.current.memory,temperature.gpu,power.draw,pcie.link.gen.current,pcie.link.width.current --format=csv *> tests/out/road-texture/gpu-environment.csv
    if ($LASTEXITCODE) { throw 'NVIDIA GPU telemetry unavailable.' }
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsPath) { throw 'Visual Studio C++ tools not found.' }
    $devCmd = Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
    $command = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /utf-8 /std:c++17 /EHsc /O2 /MT /W4 /I externals/DirectXTex /I build/ntc/source/libraries/RTXNTC-Library/include tests/RoadTextureGpuTests.cpp /Fo:tests/out/RoadTextureGpuTests.obj /Fe:tests/out/RoadTextureGpuTests.exe /link "{1}" d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib windowscodecs.lib ole32.lib' -f $devCmd,$DirectXTexLibrary
    & $env:ComSpec /d /s /c $command *> tests/out/road-texture/build.txt
    if ($LASTEXITCODE) { throw 'Road GPU test build failed. See build.txt.' }
    if ($BuildOnly) { Write-Host 'Road GPU test executable built.'; return }
    if ($ValidateGpu) {
        Write-Host 'Checking native inference, BC encoding, GPU copies and rendering with the D3D12 debug layer...'
        & tests/out/RoadTextureGpuTests.exe --debug *> tests/out/road-texture/native-debug.txt
        if ($LASTEXITCODE) { throw 'D3D12 validation failed. See native-debug.txt.' }
    }
    Write-Host 'Measuring hardware GPU timestamps without the debug layer (3 warmups + 32 samples)...'
    & tests/out/RoadTextureGpuTests.exe *> tests/out/road-texture/native.txt
    if ($LASTEXITCODE) { throw 'Road GPU measurement failed. See native.txt.' }
    Get-Content tests/out/road-texture/native.txt
    Write-Host 'Results: tests/out/road-texture/gpu.json, quality.json and *-preview.png'
} finally { Pop-Location }
