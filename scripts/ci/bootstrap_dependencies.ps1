[CmdletBinding()]
param([ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Development')

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$dependency = (Get-Content "$PSScriptRoot/dependencies.json" -Raw | ConvertFrom-Json).assimp
$dependencyRoot = Join-Path $repoRoot 'artifacts/ci/deps'
$dependencyConfiguration = if ($Configuration -eq 'Debug') { 'Debug' } else { 'Release' }
$installName = if ($Configuration -eq 'Debug') { 'assimp-Debug' } else { 'assimp' }
$assimpLibrary = if ($Configuration -eq 'Debug') { 'assimp-vc143-mtd.lib' } else { 'assimp-vc143-mt.lib' }
$zlibLibrary = if ($Configuration -eq 'Debug') { 'zlibstaticd.lib' } else { 'zlibstatic.lib' }
$installRoot = Join-Path $dependencyRoot $installName
$stamp = Join-Path $installRoot 'source.sha256'
if ((Test-Path $stamp) -and ([IO.File]::ReadAllText($stamp).Trim() -eq $dependency.sha256) -and
    (Test-Path "$installRoot/lib/$assimpLibrary") -and (Test-Path "$installRoot/lib/$zlibLibrary")) {
    Write-Host "The CI Assimp $dependencyConfiguration dependency is already built."
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
$logs = Join-Path $repoRoot "artifacts/ci/logs/$Configuration"
New-Item -ItemType Directory -Force $logs | Out-Null

& $cmake -S $source -B $build -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_INSTALL_PREFIX=$installRoot" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 `
    -DBUILD_SHARED_LIBS=OFF -DUSE_STATIC_CRT=ON -DASSIMP_BUILD_ZLIB=ON `
    -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_SAMPLES=OFF `
    -DASSIMP_WARNINGS_AS_ERRORS=OFF -DASSIMP_INSTALL=ON -DASSIMP_INSTALL_PDB=OFF 2>&1 |
    Tee-Object -FilePath "$logs/assimp-configure.log"
if ($LASTEXITCODE -ne 0) { throw 'Assimp CMake configuration failed.' }
& $cmake --build $build --config $dependencyConfiguration --parallel 2 2>&1 |
    Tee-Object -FilePath "$logs/assimp-build.log"
if ($LASTEXITCODE -ne 0) { throw 'Assimp build failed.' }
& $cmake --install $build --config $dependencyConfiguration 2>&1 | Tee-Object -FilePath "$logs/assimp-install.log"
if ($LASTEXITCODE -ne 0) { throw 'Assimp installation failed.' }

# Assimp's static archive depends on its bundled zlib archive as well.
if (-not (Test-Path "$installRoot/lib/$zlibLibrary")) {
    Copy-Item -LiteralPath "$build/contrib/zlib/$dependencyConfiguration/$zlibLibrary" -Destination "$installRoot/lib/$zlibLibrary"
}
if (-not (Test-Path "$installRoot/lib/$assimpLibrary")) { throw 'Expected the v143 static Assimp library.' }
[IO.File]::WriteAllText($stamp, $dependency.sha256 + "`n")
Write-Host "CI dependencies installed in $installRoot"
