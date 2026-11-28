[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1')
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$package = Join-Path $repoRoot 'artifacts/ci/bin/Release'
$logs = Join-Path $repoRoot 'artifacts/ci/logs/Release'
$exe = Join-Path $package 'DirectXGameApp.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Build Release before verifying the package.' }
New-Item -ItemType Directory -Force $logs | Out-Null
$reportPath = Join-Path $logs 'package.json'
if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath }

& "$repoRoot/scripts/Package-SlimeResources.ps1" -Destination "$package/Resources" -Verify `
    -DirectXTexLibrary "$package/DirectXTex.lib" -WorkDirectory "$repoRoot/artifacts/ci/package"
& "$repoRoot/scripts/Package-Ntc.ps1" -Destination "$package/NTC" -Verify `
    -RuntimePath "$repoRoot/artifacts/ci/deps/ntc/libntc.dll"
& "$repoRoot/scripts/Package-Streamline.ps1" -Destination "$package/Streamline" -Verify

$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$dumpbin = @(& $vswhere -latest -products '*' -version '[17.0,18.0)' `
    -find 'VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe')[0]
if (-not $dumpbin) { throw 'Visual Studio 2022 dumpbin was not found.' }
# Windows 10 supplies these DLLs; any new non-system import must be packaged explicitly.
$windowsDlls = @(
    'KERNEL32.dll', 'USER32.dll', 'COMDLG32.dll', 'SHELL32.dll', 'ole32.dll', 'XINPUT1_4.dll',
    'D3DCOMPILER_47.dll', 'MFPlat.dll', 'MFReadWrite.dll', 'WINMM.dll', 'DINPUT8.dll',
    'd3d12.dll', 'dxgi.dll', 'WINTRUST.dll', 'CRYPT32.dll', 'IMM32.dll', 'WS2_32.dll', 'GDI32.dll',
    'ADVAPI32.dll', 'OLEAUT32.dll', 'VERSION.dll', 'windowscodecs.dll', 'dbghelp.dll'
)
$files = @((Get-Item -LiteralPath $exe))
foreach ($directory in @('Resources', 'NTC', 'Streamline')) {
    $path = Join-Path $package $directory
    if (Test-Path -LiteralPath $path) { $files += Get-ChildItem -LiteralPath $path -File -Recurse }
}
$binaryImports = @()
$importLog = @()
foreach ($binary in @($files | Where-Object { $_.Extension -in @('.exe', '.dll') })) {
    $imports = & $dumpbin /nologo /dependents $binary.FullName
    if ($LASTEXITCODE -ne 0) { throw "Failed to inspect DLL dependencies: $($binary.Name)" }
    $importLog += $imports
    $dlls = @()
    $delayedDlls = @()
    $delaySection = $false
    foreach ($line in $imports) {
        if ($line -match 'following delay load dependencies:') { $delaySection = $true }
        if ($line -match '^\s+([\w.-]+\.dll)\s*$') {
            if ($delaySection) { $delayedDlls += $Matches[1] } else { $dlls += $Matches[1] }
        }
    }
    if (-not $dlls.Count) { throw "The DLL import list is empty: $($binary.Name)" }
    foreach ($dll in @($dlls + $delayedDlls | Sort-Object -Unique)) {
        if ($dll -match '(?i)^(?:msvcp\d+d|vcruntime\d+(?:_\d+)?d|ucrtbased)\.dll$') {
            throw "Release imports a Debug runtime DLL: $($binary.Name) -> $dll"
        }
        # The optional Vulkan latency driver is not used by this D3D12 application.
        if ($binary.Name -eq 'sl.common.dll' -and $dll -eq 'NvLowLatencyVk.dll' -and $dll -in $delayedDlls) { continue }
        if ($dll -notin $windowsDlls -and $dll -notmatch '^(api|ext)-ms-win-' -and
            -not (Test-Path -LiteralPath (Join-Path $binary.DirectoryName $dll) -PathType Leaf) -and
            -not (Test-Path -LiteralPath (Join-Path $package $dll) -PathType Leaf)) {
            throw "Release is missing an imported DLL: $($binary.Name) -> $dll"
        }
    }
    $binaryImports += [pscustomobject]@{
        path = $binary.FullName.Substring($package.Length + 1).Replace('\', '/')
        dlls = $dlls
        delay_load_dlls = $delayedDlls
    }
}
$importLog | Set-Content "$logs/dll-imports.txt" -Encoding UTF8
$manifest = @($files | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{
        path = $_.FullName.Substring($package.Length + 1).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
[pscustomobject]@{
    configuration = 'Release'
    binary_imports = $binaryImports
    streamline = if (Test-Path "$package/Streamline/sl.interposer.dll") { 'DLSS' } else { 'native rendering' }
    files = $manifest
} | ConvertTo-Json -Depth 5 | Set-Content $reportPath -Encoding UTF8
Write-Host "Verified Release package: $($manifest.Count) files."
