[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$dependency = (Get-Content "$PSScriptRoot/dependencies.json" -Raw | ConvertFrom-Json).assimp
$dependencyRoot = Join-Path $repoRoot 'artifacts/ci/deps'
$installRoot = Join-Path $dependencyRoot 'assimp'
$stamp = Join-Path $installRoot 'source.sha256'
if ((Test-Path $stamp) -and ([IO.File]::ReadAllText($stamp).Trim() -eq $dependency.sha256) -and
    (Test-Path "$installRoot/lib/assimp-vc143-mt.lib") -and (Test-Path "$installRoot/lib/zlibstatic.lib")) {
    Write-Host 'The CI Assimp dependency is already built.'
    exit 0
}

$cmake = (Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vsPath = & $vswhere -latest -products '*' -version '[17.0,18.0)' -property installationPath
    if (-not $vsPath) { throw 'Visual Studio 2022 was not found.' }
    $cmake = Join-Path $vsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
if (-not (Test-Path $cmake)) { throw 'CMake was not found. Install the Visual Studio CMake tools.' }

$archive = Join-Path $repoRoot "artifacts/ci/downloads/assimp-$($dependency.version).zip"
New-Item -ItemType Directory -Force (Split-Path $archive -Parent) | Out-Null
if (-not (Test-Path $archive)) {
    Invoke-WebRequest -UseBasicParsing -Uri $dependency.url -OutFile "$archive.part"
    Move-Item -LiteralPath "$archive.part" -Destination $archive -Force
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $dependency.sha256) {
    throw "Assimp archive SHA256 mismatch: $archive"
}
$source = Join-Path $dependencyRoot "source/assimp-$($dependency.version)"
if (-not (Test-Path "$source/CMakeLists.txt")) {
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $dependencyRoot 'source') -Force
}
$build = Join-Path $dependencyRoot 'assimp-build'
$logs = Join-Path $repoRoot 'artifacts/ci/logs'
New-Item -ItemType Directory -Force $logs | Out-Null

& $cmake -S $source -B $build -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_INSTALL_PREFIX=$installRoot" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 `
    -DBUILD_SHARED_LIBS=OFF -DUSE_STATIC_CRT=ON -DASSIMP_BUILD_ZLIB=ON `
    -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_SAMPLES=OFF `
    -DASSIMP_WARNINGS_AS_ERRORS=OFF -DASSIMP_INSTALL=ON 2>&1 |
    Tee-Object -FilePath "$logs/assimp-configure.log"
if ($LASTEXITCODE -ne 0) { throw 'Assimp CMake configuration failed.' }
& $cmake --build $build --config Release --parallel 2 2>&1 |
    Tee-Object -FilePath "$logs/assimp-build.log"
if ($LASTEXITCODE -ne 0) { throw 'Assimp build failed.' }
& $cmake --install $build --config Release 2>&1 | Tee-Object -FilePath "$logs/assimp-install.log"
if ($LASTEXITCODE -ne 0) { throw 'Assimp installation failed.' }

# Assimp's static archive depends on its bundled zlib archive as well.
if (-not (Test-Path "$installRoot/lib/zlibstatic.lib")) {
    Copy-Item -LiteralPath "$build/contrib/zlib/Release/zlibstatic.lib" -Destination "$installRoot/lib/zlibstatic.lib"
}
if (-not (Test-Path "$installRoot/lib/assimp-vc143-mt.lib")) { throw 'Expected the v143 static Assimp library.' }
[IO.File]::WriteAllText($stamp, $dependency.sha256 + "`n")
Write-Host "CI dependencies installed in $installRoot"
